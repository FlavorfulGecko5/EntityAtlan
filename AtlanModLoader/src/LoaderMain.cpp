#include "GlobalConfig.h"
#include "ModReader.h"
#include "atlan/AtlanProfiling.h"
#include "archives/PackageMapSpec.h"
#include "archives/StreamDB.h"
#include "archives/idImage.h"
#include "atlan/AtlanOodle.h"
#include "entityslayer/Oodle.h"
#include "hash/HashLib.h"
#include "io/BinaryReader.h"
#include "io/BinaryWriter.h"
#include "archives/ResourceStructs.h"
#include "archives/SlugFont.h"
#include "atlan/AtlanLogger.h"
#include "ReserialMain.h"
#include <set>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <thread>
#include <cassert>

#ifndef _DEBUG
#undef assert
#define assert(OP) (OP)
#endif

class StringTable {
	private:
	std::string blob;
	std::vector<uint64_t> offsets;
	std::unordered_map<std::string, uint64_t> offsetmap; // Maps strings to their index in the table / offset list

	public:
	StringTable() {
		blob.reserve(100000);
		offsets.reserve(1000);
		offsetmap.reserve(1000);
	}


	/*
	* Returns the offset for a string
	* Adds the string to the table if it doesn't exist
	*/
	uint64_t indexof(const std::string_view s) {
		const auto iter = offsetmap.find(std::string(s));
		if (iter == offsetmap.end())
		{
			uint64_t index = offsets.size();

			offsets.push_back(blob.size());
			blob.append(s);
			blob.push_back('\0');
			offsetmap.emplace(s, index);
			return index;
		}
		else 
		{
			return iter->second;
		}
	}

	/*
	* Copies data over to a string chunk
	* Once this is called, you can still use this object for locating
	* offsets but you shouldn't modify it any further
	*/
	void finalize(StringChunk& s, uint32_t& finalSize) const {
		s.numStrings = offsets.size();
		
		s.offsets = new uint64_t[offsets.size()];
		memcpy(s.offsets, offsets.data(), offsets.size() * sizeof(uint64_t));
		
		s.dataBlock = new char[blob.size()];
		memcpy(s.dataBlock, blob.data(), blob.size());

		// String Count + Offset Chunk + String Blob
		finalSize = static_cast<uint32_t>(sizeof(uint64_t) + s.numStrings * sizeof(uint64_t) + blob.size());

		s.paddingCount = 8 - finalSize % 8;
		finalSize += static_cast<uint32_t>(s.paddingCount);
	}
};

void BuildArchive_StringChunk(const std::vector<ModFile*>& modfiles, ResourceArchive& r) {
	StringTable stable;

	/*
	* Build String Indices
	*/
	r.header.numStringIndices = static_cast<uint32_t>(modfiles.size() * 2);
	r.stringIndex = new uint64_t[r.header.numStringIndices];

	uint64_t* ptr = r.stringIndex;
	for (const ModFile* f : modfiles) {
		*ptr = stable.indexof(f->typestring);
		*(ptr + 1) = stable.indexof(f->assetPath);
		ptr += 2;
	}

	stable.finalize(r.stringChunk, r.header.stringTableSize);
}

#include <algorithm>

// Data we'll need to share across several functions related to archive building
struct BuildArchiveStreams {
	std::vector<idStreamDB::entry_t> streamEntries;
	std::ofstream reswriter;
	std::ofstream streamwriter;
	size_t resDataOffset; // Running offset for resource data
	size_t streamDataOffset; // Running offset for streamdb data

	// Set Resource Entry's data offset and write it's data
	// Everything in the entry besides it's data offset must be set by this point
	void WriteResource(const char* buffer, ResourceEntry& e) {
		e.dataOffset = resDataOffset;
		reswriter.seekp(e.dataOffset, std::ios_base::beg);
		reswriter.write(buffer, e.dataSize);

		// TODO: There's a fair bit of padding between each resource data block.
		// At a minimum, a data block has 8-byte alignment. It's unknown what the implications of ignoring
		// these practices are
		resDataOffset += e.dataSize;
		resDataOffset += 8 - resDataOffset % 8;
		assert(resDataOffset % 8 == 0);
	}

	void WriteStreamDB(const char* buffer, const size_t length) {
		streamwriter.seekp(streamDataOffset, std::ios_base::beg);
		streamwriter.write(buffer, length);

		assert(streamDataOffset % 16 == 0);
		streamDataOffset += length + (16 - length % 16);
	}
};

bool BuildArchive_HotReload(ResourceEntry& e, const ModFile& f, BuildArchiveStreams& out) {
	atlog("Hot Reload Mode engaged for unzipped mapentities file");

	size_t HotReloadPadding;
	e.dataSize = 40000000;
	if (e.dataSize >= f.dataLength) {
		HotReloadPadding = e.dataSize - f.dataLength;
	}
	else {
		atlog("FATAL ERROR: Hot Reload padding threshold exceeded. Please report this error.");
		return false;
	}

	// This is mostly a copy of BuildArchiveStreams::WriteResource except we
	// add extra null padding to the end of the file
	e.uncompressedSize = e.dataSize;
	e.compMode = 0;
	e.dataOffset = out.resDataOffset;
	out.resDataOffset += e.dataSize;
	out.resDataOffset += 8 - out.resDataOffset % 8;
	out.reswriter.seekp(e.dataOffset, std::ios_base::beg);
	out.reswriter.write((char*)f.dataBuffer, f.dataLength);

	char* paddingbuffer = new char[HotReloadPadding];
	memset(paddingbuffer, 0, HotReloadPadding);
	out.reswriter.write(paddingbuffer, HotReloadPadding);
	delete[] paddingbuffer;

	return true;
}

bool BuildArchive_Image(ResourceEntry& e, const ModFile& f, BuildArchiveStreams& out) {

	idAtlanImage imgdef;
	const char* BufferToWrite = nullptr;

	if (!imgdef.Read((uint8_t*)f.dataBuffer, f.dataLength)) {
		atlog("FATAL ERROR: Image resource is not a valid Atlan Image File!\n"
			"Please use Atlan Mod Packager to package your texture mods!\n"
			"   Mod: %s - %s", f.parentMod->modName.c_str(), f.realPath.c_str());
		return false;
	}

	// Write the Resource Data
	e.version = imgdef.bimversion;
	e.dataSize = imgdef.entry_length;
	e.uncompressedSize = e.dataSize;
	e.compMode = 0;
	BufferToWrite = imgdef.binaryblob;
	out.WriteResource(BufferToWrite, e);

	// Write the StreamDB Data
	BufferToWrite += imgdef.entry_length;
	for (uint64_t mipindex = 0; mipindex < imgdef.streamdbmips; mipindex++) {
		idStreamDB::entry_t streamdb_entry;

		streamdb_entry.id = HashLib::streamdb_miphash(f.defaulthash, imgdef.streamdbmips - mipindex - 1, 0);
		streamdb_entry.length = imgdef.mipinfos[mipindex].compressedSize;
		streamdb_entry.offset16 = (u32)(out.streamDataOffset / 16);
		out.streamEntries.push_back(streamdb_entry);

		out.WriteStreamDB(BufferToWrite, streamdb_entry.length);
		BufferToWrite += streamdb_entry.length;
	}
	assert(BufferToWrite == (char*)f.dataBuffer + f.dataLength);

	return true;
}

// md6 mesh mod wrapper, produced by the external Md6MeshTool. Layout:
// [Md6ModWrapperHeader][Md6ModStreamInfo * numStreams][def bytes][compressed stream blobs...]
struct Md6ModStreamInfo    { uint32_t lod; uint32_t compressedSize; };
struct Md6ModWrapperHeader { uint32_t magic; uint32_t defSize; uint32_t numStreams; };
#define MD6_WRAPPER_MAGIC 0x574D364DU // 'M6MW'

bool BuildArchive_BaseModel(ResourceEntry& e, const ModFile& f, BuildArchiveStreams& out) {
	const Md6ModWrapperHeader* wh = (const Md6ModWrapperHeader*)f.dataBuffer;
	if (f.dataLength < sizeof(Md6ModWrapperHeader) || wh->magic != MD6_WRAPPER_MAGIC) {
		atlog("FATAL ERROR: baseModel mod file is not a valid md6 wrapper: %s", f.realPath.c_str());
		return false;
	}
	const size_t defOffset = sizeof(Md6ModWrapperHeader) + (size_t)wh->numStreams * sizeof(Md6ModStreamInfo);
	e.dataSize = wh->defSize;
	e.uncompressedSize = wh->defSize;
	e.compMode = 0;

	const char* BufferToWrite = (char*)f.dataBuffer + defOffset; // the def; streams follow it
	out.WriteResource(BufferToWrite, e);

	// StreamDB
	// md6 geometry: one compressed blob per LOD, keyed by streamdb_miphash(defaultHash, 4 - lod, 0)
	const Md6ModStreamInfo* streams = (const Md6ModStreamInfo*)((char*)f.dataBuffer + sizeof(Md6ModWrapperHeader));
	const char* streamPtr = BufferToWrite + wh->defSize; // BufferToWrite points at the def

	for (uint32_t i = 0; i < wh->numStreams; i++) {
		idStreamDB::entry_t streamdb_entry;
		streamdb_entry.id = HashLib::streamdb_miphash(f.defaulthash, 4 - streams[i].lod, 0);
		streamdb_entry.length = streams[i].compressedSize;
		streamdb_entry.offset16 = (u32)(out.streamDataOffset / 16);
		out.streamEntries.push_back(streamdb_entry);
		out.WriteStreamDB(streamPtr, streamdb_entry.length);
		streamPtr += streamdb_entry.length;
	}

	return true;
}

// Build the resources and streamdb archive. 
// If this returns false something went wrong and we should abort mod loading
bool BuildArchive(const std::vector<ModFile*>& modfiles, const size_t NUM_DBFILES, fspath outarchivepath, fspath outstreamdbpath) {
	// Buffer for doing just-in-time reading of large mod files
	// This way we're not loading every mod file into memory simultaneously
	// before loading it
	JustInTimeBuffer_t JIT;

	BuildArchiveStreams outstreams;

	idStreamDB streamdb;
	streamdb.header.magic = STREAMDB_MAGIC;
	streamdb.header.pad0 = 0; streamdb.header.pad1 = 0; streamdb.header.pad2 = 0;
	streamdb.header.flags = 3;
	streamdb.prefetchheader.numblocks = 0;
	streamdb.prefetchheader.totalLength = 8;

	outstreams.streamEntries.reserve(NUM_DBFILES * 8);

	// We don't know how many streamdb entries an image will have until we've loaded the data
	// Hence we must overestimate the start of the data block
	// TODO: If we ever support 3D or cubic images, we may need to overestimate even harder
	const size_t streamdb_DataOffset = sizeof(idStreamDB::header) + sizeof(idStreamDB::prefetchheader_t)
		+ 9 * NUM_DBFILES * sizeof(idStreamDB::entry_t);
	outstreams.streamDataOffset = streamdb_DataOffset + (16 - streamdb_DataOffset % 16);

	ResourceArchive archive;
	ResourceHeader& h = archive.header;
	
	/*
	* Header Constants
	*/
	h.magic[0] = 'I'; h.magic[1] = 'D'; h.magic[2] = 'C'; h.magic[3] = 'L';
	h.version = g_archiveversion;
	h.flags = 0;
	h.numSegments = 1;
	h.segmentSize = 1099511627775UL;
	h.metadataHash = 0;
	h.numSpecialHashes = 0;
	h.numMetaEntries = 0;
	h.metaEntriesSize = 0;
	h.resourceEntriesOffset = sizeof(ResourceHeader) + (g_archiveversion < 13 ? sizeof(ResourceMetaHeader) : 0);
	h.numResources = static_cast<uint32_t>(modfiles.size());
	h.stringTableOffset = h.resourceEntriesOffset + h.numResources * sizeof(ResourceEntry);
	
	BuildArchive_StringChunk(modfiles, archive);

	/*
	* "Build" Dependencies - seems to be unnecessary. Dependencies only used by idStudio?
	*/

	h.resourceDepsOffset = h.stringTableOffset + h.stringTableSize;
	h.numDependencies = 0;
	h.numDepIndices = 0;
	h.metaEntriesOffset = h.resourceDepsOffset;
	h.resourceSpecialHashOffset = h.resourceDepsOffset + h.numDependencies * sizeof(ResourceDependency);

	
	/*
	* Configure IDCL Size and Data Offset
	*/

	archive.metaheader.unknown = 0;
	archive.metaheader.metaOffset = Get_ExpectedMetaOffset(h);
	uint64_t idclsize = 4 + (archive.metaheader.metaOffset + 4) % 8; // Ensure data offset has an 8 byte alignment
	h.dataOffset = archive.metaheader.metaOffset + idclsize;
	assert(h.dataOffset % 8 == 0);

	outstreams.reswriter.open(outarchivepath, std::ios_base::binary);
	if(NUM_DBFILES)
		outstreams.streamwriter.open(outstreamdbpath, std::ios_base::binary);

	/*
	* Build the resource entries
	*/
	archive.entries = new ResourceEntry[modfiles.size()];
	outstreams.resDataOffset = h.dataOffset;
	for(size_t MODFILE_INDEX = 0; MODFILE_INDEX < modfiles.size(); MODFILE_INDEX++) {
		ResourceEntry& e = archive.entries[MODFILE_INDEX];
		const ModFile& f = *modfiles[MODFILE_INDEX];

		// Because of just-in-time loading, we can no longer filter out all invalid modfiles
		// prior to this function. For now, we'll simply abort mod loading. But perhaps
		// there's a better way we can skip over the invalid files while loading the rest?
		if (!ModReader::LoadModData(*modfiles[MODFILE_INDEX], JIT)) {
			atlog("FATAL ERROR: Just-in-time loading failed.\n"
				  "(If an unzipped image file failed to encode, this is the likely cause of this error)");
			return false; // This codepath would imply a legitimately bad error
		}

		/*
		* Set universal values
		*/
		e.resourceTypeString = 0;
		e.nameString = 1;
		e.descString = -1;
		e.strings = MODFILE_INDEX * 2;
		e.specialHashes = 0;
		e.metaEntries = 0;
		e.reserved0 = 0;
		e.reserved2 = 0;
		e.reservedForVariations = 0;
		e.numStrings = 2;
		e.numSources = 0;
		e.numSpecialHashes = 0;
		e.numMetaEntries = 0;
		e.depIndices = 0;
		e.numDependencies = 0;
		e.generationTimeStamp = -1; // Must set to -1 because Sandbox uses resource_loadMostRecent 1
		for(int i = 0; i < sizeof(e.padding); i++) 
			e.padding[i] = 0; // Indeterminant in release builds. Must set = 0 to stabilize hash for hot reloading

		// Executable patcher disables these checksums. 
		// Plus these are calculated using the uncompressed data - bad if we want to pre-compress mod files
		// HashLib::ResourceMurmurHash
		e.dataCheckSum = -1;
		e.defaultHash = f.defaulthash; // Normally, if streamdb hash is unused, it's equal to the dataChecksum
		e.version = f.resourceVersion;
		if(f.typeenum & rtc_serialized) {
			e.flags = 2; e.variation = 70;
		}
		else {
			e.flags = 0; e.variation = 0;
		}

		bool buildResult = false;
		bool IsAtlanCompressed = Oodle::IsAtlanCompFile((const char*)f.dataBuffer, f.dataLength);
		if (f.typeenum == rt_mapentities && !IsAtlanCompressed && f.parentMod->IsUnzipped) {
			buildResult = BuildArchive_HotReload(e, f, outstreams);
		}
		else if (f.typeenum == rt_image) {
			buildResult = BuildArchive_Image(e, f, outstreams);
		}
		else if (f.typeenum == rt_baseModel) {
			buildResult = BuildArchive_BaseModel(e, f, outstreams);
		}
		else if(IsAtlanCompressed) {
			const size_t ATCF_SIZE = Oodle::AtlanCompHeaderSize();
			e.dataSize = f.dataLength - ATCF_SIZE;
			e.uncompressedSize = Oodle::atcf_uncompressedSize((char*)f.dataBuffer);
			e.compMode = 2;
			outstreams.WriteResource((char*)f.dataBuffer + ATCF_SIZE, e);
			buildResult = true;
		}
		else {
			e.dataSize = f.dataLength;
			e.uncompressedSize = e.dataSize;
			e.compMode = 0;
			outstreams.WriteResource((char*)f.dataBuffer, e);
			buildResult = true;
		}

		if(!buildResult)
			return false;
	}

	Audit_ResourceArchive(archive);

	/*
	* Write the archive
	*/

	outstreams.reswriter.seekp(0, std::ios_base::beg);
	outstreams.reswriter.write((char*)&archive.header, sizeof(ResourceHeader));

	if (g_archiveversion < 13) {
		outstreams.reswriter.write((char*)&archive.metaheader, sizeof(ResourceMetaHeader));
	}

	outstreams.reswriter.write((char*)archive.entries, sizeof(ResourceEntry) * h.numResources);

	// String Chunk
	uint64_t blobSize = h.stringTableSize - sizeof(uint64_t) - sizeof(uint64_t) * archive.stringChunk.numStrings - archive.stringChunk.paddingCount;
	outstreams.reswriter.write((char*)&archive.stringChunk.numStrings, sizeof(uint64_t));
	outstreams.reswriter.write((char*)archive.stringChunk.offsets, archive.stringChunk.numStrings * sizeof(uint64_t));
	outstreams.reswriter.write(archive.stringChunk.dataBlock, blobSize);
	for(uint64_t i = 0; i < archive.stringChunk.paddingCount; i++)
		outstreams.reswriter.put('\0');
	outstreams.reswriter.write((char*)archive.stringIndex, h.numStringIndices * sizeof(uint64_t));

	// IDCL
	outstreams.reswriter.write("IDCL", 4);
	for(int i = 0; i < idclsize - 4; i++) // Todo: can probably cut this
		outstreams.reswriter.put('\0');

	outstreams.reswriter.close();

	/*
	* Finish writing the StreamDB data
	*/
	if(NUM_DBFILES == 0)
		return true;

	streamdb.header.headerLength = static_cast<u32>( sizeof(idStreamDB::header) + sizeof(idStreamDB::prefetchheader)
		+ sizeof(idStreamDB::entry_t) * outstreams.streamEntries.size());
	streamdb.header.numEntries = (u32)outstreams.streamEntries.size();
	if (streamdb.header.headerLength > streamdb_DataOffset) {
		atlog("FATAL ERROR: StreamDB Header Length > Data Offset. Please report this problem!");
		return false;
	}

	std::sort(outstreams.streamEntries.begin(), outstreams.streamEntries.end());

	outstreams.streamwriter.seekp(0, std::ios_base::beg);
	outstreams.streamwriter.write((char*)&streamdb.header, sizeof(streamdb.header));
	outstreams.streamwriter.write((char*)outstreams.streamEntries.data(), outstreams.streamEntries.size() * sizeof(idStreamDB::entry_t));
	outstreams.streamwriter.write((char*)&streamdb.prefetchheader, sizeof(streamdb.prefetchheader));
	outstreams.streamwriter.close();

	#ifdef _DEBUG
	idStreamDB audit;
	assert(audit.Read(outstreamdbpath.c_str()));
	#endif

	return true;
}

#define MODDED_TIMESTAMP 123456

void RebuildContainerMask(const fspath metapath, const fspath newarchivepath) {
	// Read the entire archive into memory
	BinaryOpener open(metapath.string());
	assert(open.Okay());

	
	// Get addresses of relevant data pieces
	char* archive = const_cast<char*>(open.ToReader().GetBuffer());
	ResourceHeader* h = reinterpret_cast<ResourceHeader*>(archive);
	ResourceEntry* e = reinterpret_cast<ResourceEntry*>(archive + sizeof(ResourceHeader) + (h->version < 13 ? sizeof(ResourceMetaHeader) : 0));
	char* compressed = archive + e->dataOffset;

	// A few checks to ensure everything is normal
	assert(h->numResources == 1);
	assert(h->dataOffset == e->dataOffset);
	//assert(e->compMode == 2); COULD CHANGE TO UNCOMPRESSED BETWEEN UPDATES
	assert(e->defaultHash == e->dataCheckSum);

	// TODO: If adding multiple archives, must do this for every archive
	// Get data we'll be inserting into container mask
	containerMaskEntry_t newentry = GetContainerMaskHash(newarchivepath);
	//atlog("Container Mask Hash: 0x%llx", newentry.hash);

	// Number of uint64_t's in our bitmask.
	// Ensure at least 1 just incase having 0 is bad
	uint32_t bitmasklongs = static_cast<uint32_t>(newentry.numResources / 64 + (newentry.numResources % 64 ? 1 : 0) + 1);

	// Hash + bitmasklongs + byte count of our bitmask
	size_t extraSize = sizeof(uint64_t) + sizeof(uint32_t) + bitmasklongs * sizeof(uint64_t);

	// Decompress the Oodle-compressed container mask
	char* decomp = new char[e->uncompressedSize + extraSize];
	if (e->compMode == 2) {
		if (!Oodle::DecompressBuffer(compressed, e->dataSize, decomp, e->uncompressedSize)) {
			atlog("ERROR: FAILED TO DECOMPRESS CONTAINER MASK");
			return;
		}
	}
	else {
		assert(e->compMode == 0);
		memcpy(decomp, compressed, e->dataSize);
	}


	// Important file offsets
	uint32_t* hashCount = reinterpret_cast<uint32_t*>(decomp);

	if(*hashCount & 0xFFFFF000)
		hashCount++; // Skip the compacted timestamp (idTech7 container masks only)

	uint64_t* newHash = reinterpret_cast<uint64_t*>(decomp + e->uncompressedSize);
	uint32_t* blobSize = reinterpret_cast<uint32_t*>(decomp + e->uncompressedSize + sizeof(uint64_t));
	uint64_t* bitmask = reinterpret_cast<uint64_t*>(decomp + e->uncompressedSize + sizeof(uint64_t) + sizeof(uint32_t));

	// Add new bitmask to the file
	*hashCount = *hashCount + 1;
	*newHash = newentry.hash;
	*blobSize = bitmasklongs;
	for(size_t i = 0; i < bitmasklongs; i++) {
		*(bitmask + i) = -1;
	}

	/*
	* - Unnecessary: Change data offset
	* - Change data size
	* - Change uncompressed size
	* - Change defaultHash and data Check Sum
	* - Change compMode to 0 (if we opt not to recompress it)
	*/
	// Modify the ResourceEntry - disabling compression on the file should be fine
	e->dataSize = e->uncompressedSize + extraSize;
	e->uncompressedSize = e->dataSize;
	e->compMode = 0;
	
	// Not necessary because of executable patch disabling this check. (Also idFile_Verified isn't used on meta.resources)
	//e->defaultHash = HashLib::ResourceMurmurHash(decomp, e->dataSize);
	//e->dataCheckSum = e->defaultHash;

	// IMPORTANT: Our gameupdate detection system relies on checking this value
	// to determine if meta.resources is modded or not.
	e->generationTimeStamp = MODDED_TIMESTAMP; 

	// Rewrite the file
	std::ofstream writer(metapath, std::ios_base::binary);
	writer.write(archive, h->dataOffset);
	writer.write(decomp, e->dataSize);

	delete[] decomp;
}

bool Modify_PackageMapSpec(const fspath& pmspath, bool includeStreamDB, GlobalConfig_t& config) {

	MapSpec_t mapspec(pmspath);
	if(!mapspec.good)
		return false;

	std::string newfiles[] = {
		"modarchives/common_mod.resources",
		"modarchives/common_mod.streamdb"
	};

	bool result = mapspec.Modify(newfiles, includeStreamDB ? 2 : 1, config.mapspec.data(), config.mapspec.size());
	if(!result)
		return false;

	mapspec.SaveToFile(pmspath);
	return true;
}

#include "archives/BuildManifest.h"

bool Modify_BuildManifest(const fspath& manifestpath) {
	if(g_game != game_eternal)
		return true;

	// We can safely add entries that don't exist, so we don't need to check
	// if we're actually building the streamdb
	const std::string newentry = R"(
		, "modarchives/common_mod.resources": {
			"fileSize": 123,
			"chunkSize": 999999999,
			"hashes": [
				"ffffffffffffffffffffffffffffffffffffffff"
			]
		},
		"modarchives/common_mod.streamdb": {
			"fileSize": 123,
			"chunkSize": 999999999,
			"hashes": [
				"ffffffffffffffffffffffffffffffffffffffff"
			]
		}
		} }		
	)";

	idcl::buildmanifest manifest;
	return manifest.modify(manifestpath.c_str(), manifestpath.c_str(), newentry.data(), newentry.length(), false);
}

#include "archives/MapResources.h"
#include "archives/Blang.h"

bool Query_Archives(std::unordered_map<std::string, ModFile*>& FileMap, GlobalConfig_t& config, ModDef& ModDef_MapResources, const fspath& path_mapspec) {

	/*
	* Step 1: Add all .mapresources and .blang files we need to the query map
	*/
	for(const auto& pair : config.mapresinfo) {
		std::string filetype = "file";
		// We're gonna do some real hacky stuff here
		FileMap[filetype + pair.first] = nullptr;
	}
	for(const auto& pair : config.blanginfo) {
		std::string filetype = "binaryFile";
		FileMap[filetype + pair.first] = nullptr;
	}

	/*
	* Nothing to query for? We're good
	*/
	if(FileMap.size() == 0)
		return true;
	atlog("Querying archives for %zu files", FileMap.size());

	/*
	* Step 2: Query for Data
	*/
	std::string lookupstring;
	idcl::ArchiveIterator iter(path_mapspec, false);
	ResourceEntryBuffers_t EntryBuffers;
	for (const ResourceEntry& e : iter) {

		lookupstring = iter.typestring;
		lookupstring.append(iter.namestring);

		const auto& mapiter = FileMap.find(lookupstring);
		if(mapiter == FileMap.end())
			continue;
		ModFile* f = mapiter->second;

		// Nullptr --> a .mapresource or .blang file that we need
		// TODO: Split this into different functions
		if(f == nullptr) {
			ResourceEntryData_t EntryData = Get_EntryData(e, iter.archive.filehandle, EntryBuffers);

			atlog("Modifying %s", iter.namestring);

			if (strcmp(iter.typestring, "binaryFile") == 0) {

				const std::vector<ModFile*>& Langcsvs = config.blanginfo[iter.namestring];
				
				idcl::blangmodargs langargs;
				langargs.blang = (char*)EntryData.buffer;
				langargs.blanglength = EntryData.length;
				langargs.blangname = iter.namestring;
				langargs.numcsvs = Langcsvs.size();

				char** p_csvs = new char*[langargs.numcsvs];
				size_t* l_csvs = new size_t[langargs.numcsvs];

				for(int i = 0; i < langargs.numcsvs; i++) {
					p_csvs[i] = Langcsvs[i]->dataBuffer;
					l_csvs[i] = Langcsvs[i]->dataLength;
				}

				langargs.csvs = p_csvs;
				langargs.csvlengths = l_csvs;

				charbuffer_t ModdedLang;
				bool Success = idcl::blang_modify(langargs, ModdedLang);
				delete[] p_csvs;
				delete[] l_csvs;

				// TODO FIXME: Must erase from map before this or it will find
				// other copies of the file, probably same thing with mapresources?
				if(!Success) {
					atlog("ERROR: Failed to modify blang file");
					return false;
				}

				ModDef_MapResources.modFiles.emplace_back();
				f = &ModDef_MapResources.modFiles.back();
				f->typestring = "binaryFile";
				f->typeenum = rt_binaryFile;
				f->parentMod = &ModDef_MapResources;
				f->assetPath = iter.namestring;
				f->realPath = "GENERATED";
				f->ownsData = true;
				f->resourceVersion = 1;

				// Transfer ownership from buffer to modfile
				f->dataLength = ModdedLang.length;
				f->dataBuffer = ModdedLang.data;
				ModdedLang.data = nullptr;
				ModdedLang.length = 0;
				ModdedLang.capacity = 0;
			}
			else {
				MapResource CurrentMap;

				if (!CurrentMap.Parse(EntryData.buffer, EntryData.length)) {
					atlog("ERROR: Failed to parse file");
					return false;
				}

				GlobalConfig_t::mapres_t& editinfo = config.mapresinfo.find(iter.namestring)->second;
				BinaryWriter finalbin;

				atlog("- New Entries: %zu, Load All: %hhu", editinfo.entries.size(), editinfo.LoadAll);
				if (!CurrentMap.AddFiles(editinfo.entries.data(), editinfo.entries.size(), editinfo.LoadAll, finalbin)) {
					atlog("ERROR: Failed to insert entries");
					return false;
				}

				ModDef_MapResources.modFiles.emplace_back();
				f = &ModDef_MapResources.modFiles.back();
				f->typestring = "file";
				f->typeenum = rt_file;
				f->parentMod = &ModDef_MapResources;
				f->assetPath = iter.namestring;
				f->realPath = "GENERATED";
				f->dataLength = finalbin.GetFilledSize();
				f->dataBuffer = finalbin.Finalize();
				f->ownsData = true;
				if(g_game == game_darkages)
					f->resourceVersion = 2;
				else f->resourceVersion = 1;
			}
		}
		else {
			f->defaulthash = e.defaultHash;
			f->resourceVersion = e.version;
		}

		FileMap.erase(mapiter);
		if (FileMap.empty()) {
			atlog("All hashes found");
			break;
		}
	}

	if (FileMap.size()) {
		atlog("ERROR: Could not find one or more required files in vanilla archives");
		return false;
	}
	return true;
}

bool IsModded_MapSpec(const fspath& path) {
	BinaryOpener open(path.string());
	BinaryReader reader = open.ToReader();

	std::string_view view(reader.GetBuffer(), reader.GetLength());
	return view.find("modarchives") != std::string_view::npos;
}

bool IsModded_Meta(const fspath& path) {
	ResourceArchive meta;
	idcl::ReadResource(meta, path.c_str(), RF_StopAfterEntries, false);
	return meta.entries[0].generationTimeStamp == MODDED_TIMESTAMP;
}

bool IsModded_SoundMeta(const fspath& path) {
	char magic[8] = {};
	std::ifstream meta(path, std::ios_base::binary);
	meta.seekg(-8, std::ios_base::end);

	meta.read(magic, 8);
	return memcmp(magic, "ATLANMOD", 8) == 0;
}

bool IsModded_BuildManifest(const fspath& path) {
	return idcl::buildmanifest::ismodded(path.c_str());
}

/*
* CREATE / RESTORE BACKUPS; CLEANUP PREVIOUS INJECTION FILES
* If returned false, a fatal error was encountered and program should abort
*/
bool CleanupLastLoad(const fspath gamedir) 	
{
	using namespace std::filesystem;

	fspath modsdir = gamedir / "mods";
	fspath basedir = gamedir / "base";
	fspath manifestpath = basedir / "build-manifest.bin";
	fspath outdir = basedir / "modarchives";
	fspath outarchivepath = outdir / "common_mod.resources";
	fspath outstreamdbpath = outdir / "common_mod.streamdb";
	fspath pmspath = basedir / "packagemapspec.json";
	fspath metapath = basedir / "meta.resources";
	fspath soundmetapath = basedir / "sound/soundbanks/pc/soundmetadata.bin";
	fspath modsndpath =    basedir / "sound/soundbanks/pc/ATLANMOD.snd";

	std::error_code lastCode;
	//atlog("Managing backups and cleaning up previous injection files.");

	#define NUM_BACKUPS 4
	const fspath backedupfiles[NUM_BACKUPS] = {pmspath, metapath, soundmetapath, manifestpath};
	bool IsModded[NUM_BACKUPS] = { IsModded_MapSpec(pmspath), IsModded_Meta(metapath), IsModded_SoundMeta(soundmetapath), IsModded_BuildManifest(manifestpath)};
	const u8 Games[NUM_BACKUPS] = {game_all, game_all, game_all, game_eternal};

	// Handle backups
	for(int i = 0; i < NUM_BACKUPS; i++) {
		if(!(g_game & Games[i]))
			continue;

		const fspath& original = backedupfiles[i];
		const fspath backup = original.string() + ".backup";

		// Ensure the original file exists
		if (!exists(original)) {
			atlog("ERROR: Could not find %ls", absolute(original).c_str());
			return false;
		}

		// If the backup doesn't exist, assume this is a first time setup
		// and copy it no matter what
		if (!exists(backup)) {
			copy_file(original, backup, copy_options::none, lastCode);
		}
		else {
			// If the file is vanilla, override the existing backup
			// (Do this to ensure backups are kept accurate across game updates)
			if (!IsModded[i]) {
				copy_file(original, backup, copy_options::overwrite_existing, lastCode);
			}
			else {
				copy_file(backup, original, copy_options::overwrite_existing, lastCode);
			}
		}
	}

	// Create input/output directories if they don't exist yet
	if (!exists(modsdir))
		create_directory(modsdir, lastCode);
	if(!exists(outdir))
		create_directory(outdir, lastCode);

	// Delete archives created from previous injections

	#if 0
	std::vector<fspath> filesToDelete;
	filesToDelete.reserve(10);
	for (const directory_entry& dirEntry : directory_iterator(outdir)) {
		if(dirEntry.is_directory())
			continue;

		if(dirEntry.path().extension() == ".resources") {
			filesToDelete.push_back(dirEntry.path());
		}
	}
	for (const fspath& fp : filesToDelete) {
		remove(fp, lastCode);
	}
	#else
	if (exists(outarchivepath)) {
		remove(outarchivepath, lastCode);
	}
	if (exists(outstreamdbpath)) {
		remove(outstreamdbpath, lastCode);
	}
	#endif

	if (exists(modsndpath)) {
		remove(modsndpath, lastCode);
	}

	return true;
}

bool InjectorLoadMods(const fspath gamedir, const int argflags) {
	fspath modsdir = gamedir / "mods";
	fspath basedir = gamedir / "base";
	fspath outdir = basedir / "modarchives";
	fspath outarchivepath = outdir / "common_mod.resources";
	fspath outstreamdbpath = outdir / "common_mod.streamdb";
	fspath pmspath = basedir / "packagemapspec.json";
	fspath metapath = basedir / "meta.resources";

	std::vector<fspath> UnzippedModFolders;
	std::vector<fspath> zipmodpaths;
	UnzippedModFolders.reserve(20);
	zipmodpaths.reserve(5);

	if(argflags & argflag_resetvanilla) {
		atlog("Uninstalled all mods");
		return true;
	}

	/*
	* GATHER MOD FILE PATHS
	*/
	{
		using namespace std::filesystem;

		// Build list of zip mod files
		for (const directory_entry& dirEntry : directory_iterator(modsdir)) {
			if (dirEntry.is_directory()) {
				
				// Interpret each folder in the mods folder as it's own unzipped mod
				// For easy debugging: Ignore folders whose first character is a $
				if(*dirEntry.path().stem().c_str() != L'$') {
					UnzippedModFolders.push_back(dirEntry.path());
				}
				continue;
			}

			if(dirEntry.path().extension() == ".zip")
				zipmodpaths.push_back(dirEntry.path());
		}

		atlog("\nMod Zips: %zu Unzipped Mod Folders: %zu", zipmodpaths.size(), UnzippedModFolders.size());
	}
	

	/*
	* READ MOD DATA
	*/

	atlog("\n\nReading Mods:\n----------");

	GlobalConfig_t globalconfig;
	globalconfig.mapresinfo.reserve(4);
	globalconfig.blanginfo.reserve(5);
	ModDef GlobalMod;

	struct modlist_t {
		ModDef* mods = nullptr;
		int totalmods = 0;

		~modlist_t() { delete[] mods;}
	} ModList;

	ModList.totalmods = static_cast<int>(zipmodpaths.size() + UnzippedModFolders.size());
	ModList.mods = new ModDef[ModList.totalmods];

	int REALMOD_INCREMENTOR = 0;
	for(const fspath& UnzippedFolder : UnzippedModFolders) {
		ModReader::ReadLooseModv2(ModList.mods[REALMOD_INCREMENTOR++], UnzippedFolder, gamedir, globalconfig);
	}
	for(const fspath& ZipPath : zipmodpaths) {
		ModReader::ReadZipMod(ModList.mods[REALMOD_INCREMENTOR++], ZipPath, globalconfig);
	}
	assert(REALMOD_INCREMENTOR == ModList.totalmods);

	/*
	* Build the supermod - the list of mod files we will actually load into the archive
	*/

	std::vector<ModFile*> supermod;
	std::vector<ModFile*> audiosupermod;
	int streamdbFileCount = 0;
	std::unordered_map<std::string, ModFile*> find_defaulthashes;
	std::unordered_map<std::string, ModFile*> priorityAssets;

	/*
	* Check for mod conflicts - eliminating any duplicate assets
	*/

	atlog("\n\nChecking for Conflicts:\n----------");
	for(int i = 0; i < ModList.totalmods; i++) {
		ModDef& current = ModList.mods[i];

		for(ModFile& file : current.modFiles) {

			// Special Case: Put string csvs into the global config data
			// These must not get loaded as regular mod files
			if (file.typeenum == rt_binaryFile) {
				file.assetPath += ".blang";
				globalconfig.blanginfo[file.assetPath].push_back(&file);
				continue;
			}

			// Must do this to prevent false conflicts between files
			// with the same path but different resource type
			std::string lookupstring(file.typestring);
			lookupstring.append(file.assetPath);

			auto iter = priorityAssets.find(lookupstring);
			if(iter == priorityAssets.end()) {
				priorityAssets.emplace(lookupstring, &file);
			} 
			else {
				bool replaceMapping = current.loadPriority < iter->second->parentMod->loadPriority;

				atlog("CONFLICT FOUND: %s"
					  "\n(A): %s - %s"
					  "\n(B): %s - %s"
					  "\nWinner: (%c)"
					  "\n---", 
					  file.assetPath.c_str(), current.modName.c_str(), file.realPath.c_str(),
					  iter->second->parentMod->modName.c_str(), iter->second->realPath.c_str(),
					  replaceMapping ? 'A' : 'B');

				if(replaceMapping) {
					iter->second = &file;
				}
			}
		}
	}

	/*
	* Second pass to further analyze the prioritized files
	*/

	atlog("\n\nCompiling Mod Files:\n----------");
	supermod.reserve(priorityAssets.size());
	uint64_t NEXT_STREAMDB_HASH = 1234;
	for (const auto& pair : priorityAssets) {
		ModFile& file = *pair.second;

		// Handle serialized files
		if (file.typeenum & rtc_serialized) {

			// We assume atlan compressed files (created via the mod packager) are serialized
			bool isAtlanCompressed = Oodle::IsAtlanCompFile((const char*)file.dataBuffer, file.dataLength);
			bool isSerialized = isAtlanCompressed || Reserializer::IsSerialized((char*)file.dataBuffer, file.dataLength, file.typeenum);

			if (!isSerialized)
			{

				// For fast iteration, we permit unzipped mod files to be unserialized and serialize them here
				if(file.parentMod->IsUnzipped) 	{
					atlog("Serializing %s", file.realPath.c_str());

					BinaryWriter writer(static_cast<size_t>(file.dataLength * 0.75));

					Reserializer::Serialize((char*)file.dataBuffer, file.dataLength, writer, file.typeenum);

					size_t newsize = writer.GetFilledSize();
					char* newbuffer = writer.Finalize();

					delete[] file.dataBuffer;
					file.dataBuffer = newbuffer;
					file.dataLength = newsize;
				}

				// Filter out zipped mod files that aren't serialized
				else {
					atlog("ERROR: Zipped file is not serialized! Please use AtlanModPackager to serialize your mod files before distributing them.\n"
						  "   Mod: %s - %s", file.parentMod->modName.c_str(), file.realPath.c_str());
					continue;
				}
			}
		}

		#if 1
		// We assume that all slug_font files are raw slug fonts and add id's header data here
		// We do this because there are still some unknowns regarding the glyph mask and streaming system
		else if(file.typeenum == rt_slug_font) {
			charbuffer_t slugoutput;
			if (!idcl::make_slugfont((char*)file.dataBuffer, file.dataLength, slugoutput)) {
				atlog("ERROR: Failed to convert slug_font");
				continue;
			}

			// Transfer ownership of finalized slug_font file to the ModFile
			delete[] file.dataBuffer;
			file.dataBuffer = slugoutput.data;
			file.dataLength = slugoutput.length;
			slugoutput.data = nullptr;
			slugoutput.length = 0;
			slugoutput.capacity = 0;
		}
		#endif

		else if (file.typeenum == rt_compfile) {
			bool isCompFile = idcl::compfile_isvalid(file.dataBuffer, file.dataLength);

			if (!isCompFile) {
				if (!file.parentMod->IsUnzipped) {
					atlog("ERROR: Zipped compfile is not compressed! Please use AtlanModPackager to compress compfiles before distributing them.\n"
						"   Mod: %s - %s", file.parentMod->modName.c_str(), file.realPath.c_str());
					continue;
				}

				charbuffer_t packagedfile;
				if(!idcl::compfile_create(file.dataBuffer, file.dataLength, packagedfile)) {
					atlog("FATAL ERROR: Failed to create compfile?");
					return false;
				}

				// Transfer ownership of finalized comp_file to the ModFile
				delete[] file.dataBuffer;
				file.dataBuffer = packagedfile.data;
				file.dataLength = packagedfile.length;
				packagedfile.data = nullptr;
				packagedfile.length = 0;
				packagedfile.capacity = 0;
			}
		}

		// If an asset requires a streamdb hash, add it to the lookup map
		if (file.typeenum & rtc_query_dbhash) {
			find_defaulthashes[pair.first] = &file;
		}
		else if (file.typeenum & rtc_assign_dbhash) {
			streamdbFileCount++;
			file.defaulthash = NEXT_STREAMDB_HASH++;
		}

		if (file.typeenum == rt_audio) {
			audiosupermod.push_back(&file);
		}
		else {
			supermod.push_back(&file);
		}
	}

	/*
	* Find streamdb hashes if necessary
	*/
	if (!Query_Archives(find_defaulthashes, globalconfig, GlobalMod, pmspath)) {
		atlog("Mod Loading aborted due to error when querying archives");
		return false;
	}
	for (ModFile& f : GlobalMod.modFiles) {
		supermod.push_back(&f);
	}

	/*
	* BUILD THE RESOURCE ARCHIVES
	*/

	if (supermod.size() > 0) {
		atlog("\n\nBuilding Archives:\n----------");

		bool okay = BuildArchive(supermod, streamdbFileCount, outarchivepath, outstreamdbpath);
		if (!okay) {
			atlog("FATAL ERROR: Resource Mod Loading aborted due to the above error");
			return false;
		}
		okay = Modify_BuildManifest(basedir / "build-manifest.bin");
		if (!okay) {
			atlog("FATAL ERROR: Failed to modify build-manifest");
			return false;
		}
		okay = Modify_PackageMapSpec(pmspath, streamdbFileCount > 0, globalconfig);
		if (!okay) {
			atlog("FATAL ERROR: Mod Loading aborted due to above error with packagemapspec");
			return false;
		}
		RebuildContainerMask(metapath, outarchivepath);
	}

	if (audiosupermod.size() > 0) {
		atlog("Constructing audio archives");

		fspath soundsdir = basedir / "sound" / "soundbanks" / "pc";
		ModBuilder::BuildAudioArchives(soundsdir, audiosupermod);
	}

	if (supermod.size() == 0 && audiosupermod.size() == 0) {
		atlog("\n\nNo mods will be loaded. All previously loaded mods are removed.");
	}

	return true;
}

extern bool Executable_Patcher_Main(const fspath& gamedir);

void InjectorMain(int argc, char* argv[], int& argflags) {

	atlog(R"(
----------------------------------------------
Atlan Mod Loader v%d.0 for DOOM: The Dark Ages
By FlavorfulGecko5
With Special Thanks to: Proteh, Zwip-Zwap-Zapony, Tjoener, and many other talented modders!
https://github.com/FlavorfulGecko5/EntityAtlan/
----------------------------------------------
)", MOD_LOADER_VERSION);
	
	// Do not end in a / or there may be problems when running system commands 
	// (it won't translate string literal slashes to the appropriate slash like it does when using the / operator)
	fspath gamedirectory = "."; 

	/*
	* Parse Command Line Arguments
	*/
	for(int i = 1; i < argc; i++) {
		std::string_view arg = argv[i];

		if(arg == "--verbose") {
			atlog("ARGS: Verbose Logging Enabled");
			argflags |= argflag_verbose;
		}

		else if (arg == "--notimer") {
			atlog("ARGS: Exit timer disabled");
			argflags |= argflag_noExitTimer;
		}

		else if (arg == "--nolaunch") {
			atlog("ARGS: Game will not launch after loading mods");
			argflags |= argflag_nolaunch;
		}

		#if 0
		else if (arg == "--neverpatch") {
			atlog("ARGS: Executable patcher will never run. This should only be used for debugging!");
			argflags |= argflag_neverpatch;
		}

		else if (arg == "--forceload") {
			atlog("ARGS: Mod loading will proceed if DarkAgesPatcher fails\n"
				"WARNING: Loading mods when DarkAgesPatcher fails may cause the game to permanently crash on startup.\n"
				"If this happens, you will need to unload all mods for the game to work again.\n"
				"Press ENTER to acknowledge this warning.");
			char c = getchar();
			argflags |= argflag_forceload;
		}
		#endif

		else if(arg == "--gamedir") { // This is for debug builds
			if(++i == argc)
				goto LABEL_EXIT_HELP;
			gamedirectory = argv[i];
			atlog("ARGS: Custom game directory accepted");
		}

		else {
			LABEL_EXIT_HELP:
			atlog("AtlanModLoader.exe [--verbose] [--notimer] [--nolaunch] [--gamedir <Dark Ages Installation Folder>]");
			return;
		}
	}

	/* Check the game directory is valid */
	if (!std::filesystem::is_directory(gamedirectory)) {
		atlog("FATAL ERROR: %ls is not a valid directory", gamedirectory.c_str());
		return;
	}

	/* Identify the version for the archive we must build */

	if (std::filesystem::exists(gamedirectory / "DOOMTheDarkAges.exe")) {
		g_archiveversion = 13;
		g_game = game_darkages;
	}
	else if (std::filesystem::exists(gamedirectory / "DOOMEternalx64vk.exe")) {
		g_archiveversion = 12;
		g_game = game_eternal;

		#if 0 // Turn this off when Eternal is supported
		atlog("FATAL ERROR: Doom Eternal not supported");
		return;
		#endif
	}
	else {
		atlog("FATAL ERROR: Could not locate valid game executable");
		return;
	}

	/* Cleanup last mod load. Moved this here so it's execution is independent of the executable patcher's success */
	if (CleanupLastLoad(gamedirectory) == false)
		return;

	if (!Executable_Patcher_Main(gamedirectory)) {
		atlog("Aborting mod loading due to executable patcher failure");
		return;
	}

	/*
	* Initialize Oodle Compression library
	*/
	if(!Oodle::AtlanOodleInit(gamedirectory))
		return;

	/*
	* Run the mod loader
	*/
	if(!InjectorLoadMods(gamedirectory, argflags)) {
		return;
	}

	/*
	* Finish up
	*/
	atlog("\nMod Loading Complete\n----------");

	if (argflags & argflag_nolaunch) {
		atlog("Game will not launch due to nolaunch argument");
		return;
	}

	const fspath absdir = std::filesystem::absolute(gamedirectory);
	if(std::filesystem::exists(gamedirectory / "steam_api64.dll")) {
		atlog("Launching Game with Steam");
		std::system("start \"\" \"steam://run/3017860//\"");
	}
	else {
		atlog("Could not determine how to automatically launch your game\n"
			  "Please launch it manually.");
	}
	
}

int main(int argc, char* argv[]) {

	#define LOGPATH "modloader_log.txt"

	int argflags = 0;

	#ifndef _DEBUG
	try {
	#endif

		if(!idImageEncodingContext::COMThreadInit())
			return 0;
		AtlanLogger_Init(LOGPATH);

		atlanstamp timer("Mod Loading Time");
		InjectorMain(argc, argv, argflags);
		timer.log();

	#ifndef _DEBUG
	}
	catch (std::exception e) {
		atlog(
			"\n\nFATAL ERROR: An unexpected crash has occurred\n"
			"This sudden crash may have left you with broken game files.\n"
			"Please re-run the mod loader with no mods loaded to restore them\n"
			"Or use \"Restore Backups\" in Dark Ages Mod Manager\n"
			"Error Message: %s", e.what()
		);
	}
	#endif
	
	std::cout << "\n\nOutput written to " << LOGPATH << "\n";
	AtlanLogger_Shutdown();
	idImageEncodingContext::COMThreadRelease();

	if (!(argflags & argflag_noExitTimer)) {
		std::cout << "This window will close in 10 seconds\n";
		std::this_thread::sleep_for(std::chrono::seconds(10));
	}
}