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
#include "archives/ResourceStringTable.h"
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

void BuildArchive_StringChunk(const std::vector<ModFile*>& modfiles, ResourceArchive& r) {
	StringTable stable;

	/*
	* Build String Indices
	*/
	r.header.numStringIndices = static_cast<uint32_t>(modfiles.size() * 2);
	r.stringIndex.EnsureCapacity(r.header.numStringIndices);

	uint64_t* ptr = r.stringIndex.buffer;
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

#include "atlan/AtlanDB.h"

bool BuildArchive_DBFile(ResourceEntry& e, const ModFile& f, BuildArchiveStreams& out) {
	AtlanDBFile dbfile;
	if (!dbfile.ReadFrom(f.dataBuffer, f.dataLength)) {
		atlog("FATAL ERROR: Resource is not a valid Atlan StreamDB File"
			"\n Mod: %s - %s", f.parentMod->modName.c_str(), f.realPath.c_str());
		return false;
	}

	e.version = dbfile.resourceVersion;
	e.dataSize = dbfile.resourceSize;
	e.uncompressedSize = dbfile.resourceSize_uncompressed;
	e.compMode = e.dataSize == e.uncompressedSize ? 0 : 2;
	out.WriteResource(dbfile.resourceEntry, e);

	for (uint64_t i = 0; i < dbfile.numEntries; i++) {
		const AtlanDBFile::entry& e = dbfile.entries[i];

		idStreamDB::entry_t streamdb_entry;

		if(g_game == game_darkages)
			streamdb_entry.id = HashLib::darkage_miphash(f.defaulthash, e.da_hashparm1, e.da_hashparm2);
		else streamdb_entry.id = f.defaulthash << 4 | e.en_hashparm;

		streamdb_entry.length = e.size;
		streamdb_entry.offset16 = (u32)(out.streamDataOffset / 16);
		out.streamEntries.push_back(streamdb_entry);

		out.WriteStreamDB(f.dataBuffer + e.offset, streamdb_entry.length);
	}

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

	// Luckily....the mip count is in the early portion of the header that won't have alignment problems
	// The Atlan Image Header format was not designed with Eternal's miphash algorithm in mind
	const u32 HACK_header_mipcount = ((ImageHeader*)imgdef.binaryblob)->mipCount;

	// Write the StreamDB Data
	BufferToWrite += imgdef.entry_length;
	for (uint64_t mipindex = 0; mipindex < imgdef.streamdbmips; mipindex++) {
		idStreamDB::entry_t streamdb_entry;

		if(g_game == game_darkages)
			streamdb_entry.id = HashLib::darkage_miphash(f.defaulthash, imgdef.streamdbmips - mipindex - 1, 0);
		else streamdb_entry.id = e.defaultHash << 4 | (HACK_header_mipcount - mipindex);

		streamdb_entry.length = imgdef.mipinfos[mipindex].compressedSize;
		streamdb_entry.offset16 = (u32)(out.streamDataOffset / 16);
		out.streamEntries.push_back(streamdb_entry);

		out.WriteStreamDB(BufferToWrite, streamdb_entry.length);
		BufferToWrite += streamdb_entry.length;
	}
	assert(BufferToWrite == (char*)f.dataBuffer + f.dataLength);

	return true;
}

// Build the resources and streamdb archive. 
// If this returns false something went wrong and we should abort mod loading
bool BuildArchive(const std::vector<ModFile*>& modfiles, const size_t NUM_DBFILES, fspath outarchivepath, fspath outstreamdbpath) {
	// Buffer for doing just-in-time reading of large mod files
	// This way we're not loading every mod file into memory simultaneously
	// before loading it
	charbuffer_t JIT;

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

	// Leave room for "IDCL" and ensure data offset has an 8 byte aligment
	h.dataOffset = archive.metaheader.metaOffset + 4; 
	h.dataOffset += 8 - h.dataOffset % 8;
	assert(h.dataOffset % 8 == 0);

	outstreams.reswriter.open(outarchivepath, std::ios_base::binary);
	if(NUM_DBFILES)
		outstreams.streamwriter.open(outstreamdbpath, std::ios_base::binary);

	/*
	* Build the resource entries
	*/
	archive.entries.EnsureCapacity(modfiles.size());
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
		if (f.typeenum == rt_mapentities && !IsAtlanCompressed && f.parentMod->modtype == mt_folder) {
			buildResult = BuildArchive_HotReload(e, f, outstreams);
		}
		else if (f.typeenum == rt_image) {
			buildResult = BuildArchive_Image(e, f, outstreams);
		}
		else if(f.typeenum == rt_generic_atdb) {
			buildResult = BuildArchive_DBFile(e, f, outstreams);
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

	outstreams.reswriter.write((char*)archive.entries.buffer, sizeof(ResourceEntry) * h.numResources);

	// String Chunk
	uint64_t blobSize = h.stringTableSize - sizeof(uint64_t) - sizeof(uint64_t) * archive.stringChunk.numStrings - archive.stringChunk.paddingCount;
	outstreams.reswriter.write((char*)&archive.stringChunk.numStrings, sizeof(uint64_t));
	outstreams.reswriter.write((char*)archive.stringChunk.offsets.buffer, archive.stringChunk.numStrings * sizeof(uint64_t));
	outstreams.reswriter.write(archive.stringChunk.dataBlock.buffer, blobSize);
	for(uint64_t i = 0; i < archive.stringChunk.paddingCount; i++)
		outstreams.reswriter.put('\0');
	outstreams.reswriter.write((char*)archive.stringIndex.buffer, h.numStringIndices * sizeof(uint64_t));

	// IDCL
	outstreams.reswriter.write("IDCL", 4);
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

void RebuildContainerMask(const fspath metapath, const fspath& basedir, const std::vector<std::string>& NewArchives) {
	struct NewMaskEntry {
		containerMaskEntry_t hashinfo;
		uint32_t bitmaskLongs; // Size of the bitmask in 64 byte integers
	};

	// Read the entire archive into memory
	charbuffer_t open;
	FileReader::ReadFile(metapath.c_str(), open);
	
	// Get addresses of relevant data pieces
	char* archive = open.data;
	ResourceHeader* h = reinterpret_cast<ResourceHeader*>(archive);
	ResourceEntry* e = reinterpret_cast<ResourceEntry*>(archive + sizeof(ResourceHeader) + (h->version < 13 ? sizeof(ResourceMetaHeader) : 0));
	char* compressed = archive + e->dataOffset;

	// A few checks to ensure everything is normal
	assert(h->numResources == 1);
	assert(h->dataOffset == e->dataOffset);
	//assert(e->compMode == 2); COULD CHANGE TO UNCOMPRESSED BETWEEN UPDATES
	assert(e->defaultHash == e->dataCheckSum);

	// Number of bytes we're adding to the mask file size
	size_t TotalExtraBytes = 0;

	std::vector<NewMaskEntry> NewEntries;
	NewEntries.resize(NewArchives.size());

	for (size_t i = 0; i < NewArchives.size(); i++) {
		fspath archivedir = basedir / NewArchives[i];
		NewMaskEntry& n = NewEntries[i];
		n.hashinfo = GetContainerMaskHash(archivedir);
		//atlog("Container Mask Hash: 0x%llx", newentry.hash);

		// Number of uint64_t's in our bitmask.
		// Ensure at least 1 just incase having 0 is bad
		n.bitmaskLongs = static_cast<uint32_t>(n.hashinfo.numResources / 64 + (n.hashinfo.numResources % 64 ? 1 : 0) + 1);

		// Size Of: Hash Field + Size Field + Bitmask byte count
		TotalExtraBytes += sizeof(uint64_t) + sizeof(uint32_t) + n.bitmaskLongs * sizeof(uint64_t);
	}

	// Decompress the Oodle-compressed container mask
	char* decomp = new char[e->uncompressedSize + TotalExtraBytes];
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


	// Update number of bitmasks
	uint32_t* hashCount = reinterpret_cast<uint32_t*>(decomp);
	if(*hashCount & 0xFFFFF000)
		hashCount++; // Skip the compacted timestamp (idTech7 container masks only)
	*hashCount = *hashCount + static_cast<u32>(NewEntries.size());

	// Add new bitmasks to the file
	char* nextbyte = decomp + e->uncompressedSize;
	for(const NewMaskEntry& n : NewEntries) {
		memcpy(nextbyte, &n.hashinfo.hash, sizeof(u64));
		nextbyte += sizeof(u64);
		memcpy(nextbyte, &n.bitmaskLongs, sizeof(u32));
		nextbyte += sizeof(u32);
		memset(nextbyte, 0xFF, n.bitmaskLongs * sizeof(u64));
		nextbyte += n.bitmaskLongs * sizeof(u64);
	}

	check_debug(nextbyte == decomp + e->uncompressedSize + TotalExtraBytes);

	// Modify the ResourceEntry - disabling compression on the file should be fine
	e->dataSize = e->uncompressedSize + TotalExtraBytes;
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

bool Modify_PackageMapSpec(const fspath& pmspath, GlobalConfig_t& config) {

	MapSpec_t mapspec(pmspath);
	if(!mapspec.good)
		return false;

	bool result = mapspec.Modify(config.mapspec_addons.data(), config.mapspec_addons.size(), 
		config.mapspec.data(), config.mapspec.size());
	if(!result)
		return false;

	mapspec.SaveToFile(pmspath);
	return true;
}

#include "archives/BuildManifest.h"

bool Modify_BuildManifest(const fspath& gamedir, const std::vector<std::string>& NewArchives) {
	if(g_game != game_eternal)
		return true;

	idcl::buildmanifest::modargs_t args = {gamedir.c_str(), NewArchives.data(), NewArchives.size()};
	return idcl::buildmanifest::modfromcache(args);
}

#include "archives/MapResources.h"
#include "archives/Blang.h"

bool Query_Archives_Blang(GlobalConfig_t& config, ModDef& GlobalMod, ResourceData EntryData, const char* blangname) {
	const std::vector<ModFile*>& Langcsvs = config.blanginfo[blangname];

	idcl::blangmodargs langargs;
	langargs.blang = (char*)EntryData.buffer;
	langargs.blanglength = EntryData.length;
	langargs.blangname = blangname;
	langargs.numcsvs = (int)Langcsvs.size();

	char** p_csvs = new char*[langargs.numcsvs];
	size_t* l_csvs = new size_t[langargs.numcsvs];

	for(int i = 0; i < langargs.numcsvs; i++) {

		size_t len = Langcsvs[i]->dataLength;
		l_csvs[i] = len;

		// EDGE CASE: Escape sequences are parsed and replaced in the
		// original blang text so we don't need to allocate any std::strings when
		// rebuilding the blang file. This means the default.csv must be duplicated to prevent
		// the double-parsing of escape sequences in multiple blangs
		if (Langcsvs[i]->assetPath == "strings/default.blang") {
			char* duplicate = new char[len];
			memcpy(duplicate, Langcsvs[i]->dataBuffer, len);
			p_csvs[i] = duplicate;
		}
		else {
			p_csvs[i] = Langcsvs[i]->dataBuffer;
		}
	}

	langargs.csvs = p_csvs;
	langargs.csvlengths = l_csvs;

	charbuffer_t ModdedLang;
	bool Success = idcl::blang_modify(langargs, ModdedLang);

	// Delete duplicated default CSVs
	for(int i = 0; i < langargs.numcsvs; i++) {
		if (Langcsvs[i]->assetPath == "strings/default.blang") {
			delete[] p_csvs[i];
		}
	}

	delete[] p_csvs;
	delete[] l_csvs;

	if(!Success) {
		atlog("ERROR: Failed to modify blang file");
		return false;
	}

	GlobalMod.modFiles.emplace_back();
	ModFile* f = &GlobalMod.modFiles.back();
	f->typestring = "binaryFile";
	f->typeenum = rt_binaryFile;
	f->parentMod = &GlobalMod;
	f->assetPath = blangname;
	f->realPath = "GENERATED";
	f->ownsData = true;
	f->resourceVersion = 1;

	// Transfer ownership from buffer to modfile
	f->dataLength = ModdedLang.length;
	f->dataBuffer = ModdedLang.data;
	ModdedLang.data = nullptr;
	ModdedLang.length = 0;
	ModdedLang.capacity = 0;

	return true;
}

bool Query_Archives_MapResources(GlobalConfig_t& config, ModDef& ModDef_MapResources, ResourceData EntryData, const char* filename) {
	MapResource CurrentMap;

	if (!CurrentMap.Parse(EntryData.buffer, EntryData.length)) {
		atlog("ERROR: Failed to parse file");
		return false;
	}

	GlobalConfig_t::mapres_t& editinfo = config.mapresinfo.find(filename)->second;
	BinaryWriter finalbin;

	atlog("- New Entries: %zu, Load All: %hhu", editinfo.entries.size(), editinfo.LoadAll);
	if (!CurrentMap.AddFiles(editinfo.entries.data(), editinfo.entries.size(), editinfo.LoadAll, finalbin)) {
		atlog("ERROR: Failed to insert entries");
		return false;
	}

	ModDef_MapResources.modFiles.emplace_back();
	ModFile* f = &ModDef_MapResources.modFiles.back();
	f->typestring = "file";
	f->typeenum = rt_file;
	f->parentMod = &ModDef_MapResources;
	f->assetPath = filename;
	f->realPath = "GENERATED";
	f->dataLength = finalbin.GetFilledSize();
	f->dataBuffer = finalbin.Finalize();
	f->ownsData = true;
	if (g_game == game_darkages)
		f->resourceVersion = 2;
	else f->resourceVersion = 1;

	return true;
}

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
	for (const ResourceEntry& e : iter) {

		// TODO: We can probably simplify the lookup string format since
		// all files we need to query for have unique names (.mapentities, .blang, .mapresources)
		lookupstring = iter.typestring;
		lookupstring.append(iter.namestring);

		const auto& mapiter = FileMap.find(lookupstring);
		if(mapiter == FileMap.end())
			continue;
		ModFile* f = mapiter->second;

		// Nullptr --> a .mapresource or .blang file that we need
		if(f == nullptr) {
			ResourceData EntryData = iter.archive.GetData(e);

			atlog("Modifying %s", iter.namestring);

			bool result = false;
			if (strcmp(iter.typestring, "binaryFile") == 0) {
				result = Query_Archives_Blang(config, ModDef_MapResources, EntryData, iter.namestring);
			}
			else {
				result = Query_Archives_MapResources(config, ModDef_MapResources, EntryData, iter.namestring);
			}

			// It would be wise to abort mod loading here rather than let partially broken mods get through
			if(!result)
				return false;
		}
		else {
			f->defaulthash = e.defaultHash;
			f->resourceVersion = e.version;
		}

		FileMap.erase(mapiter);
		if (FileMap.empty()) {
			atlog("Found all required files");
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
	meta.Read(path, true);
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

	std::error_code lastCode;
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

	#define NUM_BACKUPS 4
	struct backup_t {
		fspath path;
		bool (*IsModded)(const fspath& path);
		gamebit_t games;
	};
	const backup_t backupsList[NUM_BACKUPS] = {
		{pmspath,       &IsModded_MapSpec,   game_all},
		{metapath,      &IsModded_Meta,      game_all},
		{soundmetapath, &IsModded_SoundMeta, game_all},
		{manifestpath,  &IsModded_BuildManifest, game_eternal}
	};

	for(int i = 0; i < NUM_BACKUPS; i++) {
		const backup_t& BUP = backupsList[i];
		if(!(g_game & BUP.games))
			continue;

		const fspath& original = BUP.path;
		const fspath backup = original.string() + ".backup";

		// Ensure the original file exists
		if (!exists(original)) {
			atlog("FATAL ERROR: Could not find %ls", original.filename().c_str());
			return false;
		}

		// Backups being modded is probably a bad thing and should be prevented
		if (BUP.IsModded(original)) {
			if(!exists(backup)) {
				atlog("FATAL ERROR: %ls is modded and no backup exists. Please verify your game files", original.filename().c_str());
				return false;
			}
			copy_file(backup, original, copy_options::overwrite_existing, lastCode);
		}
		else {

			// EDGE CASE:
			// If the build-manifest is vanilla, we want to dump the simplified version
			// However, we only want to do this once per game update, not every time we load mods.
			// Therefore, when we encounter a vanilla manifest, we write the simplified version,
			// then mark the backup file as modded so we don't keep re-generating the simplified json
			// The result: After first run, build-manifest.bin will always be seen as modded 
			// until an update is downloaded or game files are verified.
			if (original == manifestpath) {
				if (!idcl::buildmanifest::buildsimplemanifest(manifestpath.c_str(), true)) {
					atlog("FATAL ERROR: Failed to create simplified build manifest");
					return false;
				}
			}
			copy_file(original, backup, copy_options::overwrite_existing, lastCode);
		}
	}

	// Create input/output directories if they don't exist yet
	if (!exists(modsdir))
		create_directory(modsdir, lastCode);
	if(!exists(outdir))
		create_directory(outdir, lastCode);

	// Delete archives created from previous injections
	if (exists(outarchivepath)) {
		remove(outarchivepath, lastCode);
	}
	if (exists(outstreamdbpath)) {
		remove(outstreamdbpath, lastCode);
	}
	if (exists(modsndpath)) {
		remove(modsndpath, lastCode);
	}

	return true;
}

bool CleanupLevelMods(const fspath& gamedir) {
	
	atlog("Running Level Mod Uninstaller");

	fspath dir = gamedir / "base/modarchives";

	atlog("Deleting everything in base/modarchives/");

	if(std::filesystem::is_directory(dir)) {
		std::filesystem::remove_all(dir);
		std::filesystem::create_directory(dir);
	}

	atlog("Deleting modded files in virtualtextures/");

	MapSpec_t mapspec(gamedir / "base/packagemapspec.json");
	if(!mapspec.good)
		return false;

	dir = gamedir / "virtualtextures/maps";
	if(!std::filesystem::is_directory(gamedir))
		return true;

	// This folder also contains the game's vanilla lightmaps
	// We must carefully iterate through each file, checking if it's vanilla
	// by comparing with the maps list in the (assumed vanilla) packagemapspec. 
	for(const auto& dir_iter : std::filesystem::recursive_directory_iterator(dir)) {
		if(dir_iter.is_directory())
			continue;

		fspath relative = std::filesystem::relative(dir_iter.path(), gamedir);
		bool canDelete = mapspec.IsUnusedLightmap(relative);
		if(canDelete) {
			atlog("Deleting lightmap %ls", relative.c_str());
			std::filesystem::remove(dir_iter);
		}

	}

	atlog("Level mod uninstaller completed. The program will now exit.");
	return true;
}

struct modlist_t {
	ModDef* mods = nullptr;
	int totalmods = 0;

	~modlist_t() { delete[] mods; }
};

typedef std::unordered_map<std::string, ModFile*> modfilemap_t;

// Extreme Edge Case: since conflict detection never occurs for 
// Blang files, weird behavior will occur if users
// alias multiple files to the same blang.
// Not going to do anything about this, since this is
// highly incorrect usage of the aliasing system
void Injector_AddBlang(ModFile& file, GlobalConfig_t& config) {

	const char* BLANG_PATHS[] = {
		"strings/english.blang",
		"strings/french.blang",
		"strings/german.blang",
		"strings/italian.blang",
		"strings/japanese.blang",
		"strings/korean.blang",
		"strings/latin_spanish.blang",
		"strings/polish.blang",
		"strings/portuguese.blang",
		"strings/russian.blang",
		"strings/simplified_chinese.blang",
		"strings/spanish.blang",
		"strings/traditional_chinese.blang"
	};

	if(file.assetPath != "strings/default.blang") {

		std::vector<ModFile*>& vtor = config.blanginfo[file.assetPath];

		// Override this mod's default blang with the language-specific blang
		if(vtor.size() && vtor.back()->parentMod == file.parentMod) {
			check_debug(vtor.back()->assetPath == "strings/default.blang");
			vtor.back() = &file;
		}
		else {
			vtor.push_back(&file);
		}
		return;
	}


	int IMAX = sizeof(BLANG_PATHS) / sizeof(BLANG_PATHS[0]);

	for(int i = 0; i < IMAX; i++) {
		std::vector<ModFile*>& vtor = config.blanginfo[BLANG_PATHS[i]];

		// Only load the default csv if this mod has no language-specific
		// csv for this language
		if (!vtor.size() || vtor.back()->parentMod != file.parentMod) {
			vtor.push_back(&file);
		}
	}
}

/*
* Check for mod conflicts - eliminating any duplicate assets
* Also enables online safety when necessary
*/
void Injector_CheckConflicts(const modlist_t& ModList, GlobalConfig_t& globalconfig, modfilemap_t& priorityAssets, ModDef& OnlineSafetyMod) {

	extern bool IsOnlineSafe(const ModFile & modfile, const bool Strict);

	const bool EnforceOnlineSafety = g_game == game_eternal;
	bool HasUnsafeFiles = false;
	const ModDef* const ElevatedMod = globalconfig.elevatedmod;

	atlog("\n\nChecking for Conflicts:\n----------");
	for (int i = 0; i < ModList.totalmods; i++) {
		ModDef& current = ModList.mods[i];
		const bool IsElevated = &current == ElevatedMod;

		for (ModFile& file : current.modFiles) {

			// Special Case: Put string csvs into the global config data
			// These must not get loaded as regular mod files
			if (file.typeenum == rt_binaryFile) {

				file.assetPath += ".blang";
				Injector_AddBlang(file, globalconfig);
				continue;
			}

			if (EnforceOnlineSafety && !IsOnlineSafe(file, ElevatedMod != nullptr)) {
				if(ElevatedMod && !IsElevated) {
					atlog("ELEVATED GAMEPLAY CONFLICT: Cannot Load %s - %s" , current.modName.c_str(), file.realPath.c_str());
					continue;
				}
				HasUnsafeFiles = true;
			}

			// Must do this to prevent false conflicts between files
			// with the same path but different resource type
			std::string lookupstring(file.typestring);
			lookupstring.append(file.assetPath);

			auto iter = priorityAssets.find(lookupstring);
			if (iter == priorityAssets.end()) {
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

				if (replaceMapping) {
					iter->second = &file;
				}
			}
		}
	}

	if (EnforceOnlineSafety && HasUnsafeFiles) {
		atlog("Online Safety Activated");
		extern bool BuildOnlineSafetyMod(ModDef& mod);

		BuildOnlineSafetyMod(OnlineSafetyMod);
		check_debug(OnlineSafetyMod.modFiles.size());

		ModFile* cswf = &OnlineSafetyMod.modFiles[0];

		std::string lookupstring = "cswf";
		lookupstring += cswf->assetPath;

		priorityAssets[lookupstring] = cswf;

		for (size_t i = 1; i < OnlineSafetyMod.modFiles.size(); i++) {
			ModFile* f = &OnlineSafetyMod.modFiles[i];
			globalconfig.blanginfo[f->assetPath].push_back(f);
		}
	}
}

void Injector_ReadMods(const fspath& gamedir, modlist_t& ModList, GlobalConfig_t& globalconfig) {
	const fspath modsdir = gamedir / "mods";
	std::vector<fspath> UnzippedModFolders;
	std::vector<fspath> zipmodpaths;
	UnzippedModFolders.reserve(8);
	zipmodpaths.reserve(8);

	/*
	* GATHER MOD FILE PATHS
	*/
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

		if(dirEntry.path().extension() == L".zip")
			zipmodpaths.push_back(dirEntry.path());
	}

	atlog("\nMod Zips: %zu Unzipped Mod Folders: %zu", zipmodpaths.size(), UnzippedModFolders.size());

	ModList.totalmods = static_cast<int>(zipmodpaths.size() + UnzippedModFolders.size());
	ModList.mods = new ModDef[ModList.totalmods];

	ModDef* nextmod = ModList.mods;
	for (const fspath& UnzippedFolder : UnzippedModFolders) {
		ModReader::ReadLooseModv2(*nextmod++, UnzippedFolder, gamedir, globalconfig);
	}
	for (const fspath& ZipPath : zipmodpaths) {
		ModReader::ReadZipMod(*nextmod++, gamedir, ZipPath, globalconfig);
	}
	check_debug(nextmod == ModList.mods + ModList.totalmods);

	globalconfig.ReadLevelConfigs(gamedir);
}

bool InjectorLoadMods(const fspath gamedir, const int argflags) {
	fspath basedir = gamedir / "base";
	fspath outdir = basedir / "modarchives";
	fspath outarchivepath = outdir / "common_mod.resources";
	fspath outstreamdbpath = outdir / "common_mod.streamdb";
	fspath pmspath = basedir / "packagemapspec.json";
	fspath metapath = basedir / "meta.resources";
	

	/*
	* READ MOD DATA
	*/

	GlobalConfig_t globalconfig;
	ModDef GlobalMod;
	ModDef OnlineSafetyMod;
	modlist_t ModList;

	globalconfig.mapresinfo.reserve(4);
	globalconfig.blanginfo.reserve(16);
	Injector_ReadMods(gamedir, ModList, globalconfig);

	/*
	* Build the supermod - the list of mod files we will actually load into the archive
	*/

	std::vector<ModFile*> supermod;
	std::vector<ModFile*> audiosupermod;
	int streamdbFileCount = 0;
	std::unordered_map<std::string, ModFile*> find_defaulthashes;
	std::unordered_map<std::string, ModFile*> priorityAssets;

	Injector_CheckConflicts(ModList, globalconfig, priorityAssets, OnlineSafetyMod);


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
				if (file.parentMod->modtype != mt_folder) {
					atlog("ERROR: Zipped file is not serialized! Please use AtlanModPackager to serialize your mod files before distributing them.\n"
						"   Mod: %s - %s", file.parentMod->modName.c_str(), file.realPath.c_str());
					continue;
				}

				atlog("Serializing %s", file.realPath.c_str());

				BinaryWriter writer(static_cast<size_t>(file.dataLength * 0.75));

				Reserializer::Serialize((char*)file.dataBuffer, file.dataLength, writer, file.typeenum);

				size_t newsize = writer.GetFilledSize();
				char* newbuffer = writer.Finalize();

				delete[] file.dataBuffer;
				file.dataBuffer = newbuffer;
				file.dataLength = newsize;
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
				if (file.parentMod->modtype != mt_folder) {
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

		globalconfig.AddCommonFiles(streamdbFileCount > 0);

		okay = Modify_PackageMapSpec(pmspath, globalconfig);
		if (!okay) {
			atlog("FATAL ERROR: Mod Loading aborted due to above error with packagemapspec");
			return false;
		}

		const std::vector<std::string> NewArchives = globalconfig.GetNewArchives();

		RebuildContainerMask(metapath, basedir, NewArchives);

		// IMPORTANT: This MUST go after everything else, otherwise incorrect file sizes
		// will be used when updating packagemapspec.json and meta.resources build-manifest entries
		// Need to do a Cleanup if this fails because users will be left with a modded mapspec
		// and an unmodded build-manifest, which will cause a startup crash and Steam file verification
		okay = Modify_BuildManifest(gamedir, NewArchives);
		if (!okay) {
			atlog("FATAL ERROR: Failed to modify build-manifest");
			CleanupLastLoad(gamedir);
			return false;
		}
	}

	if (audiosupermod.size() > 0) {
		atlog("Constructing audio archives");

		fspath soundsdir = basedir / "sound" / "soundbanks" / "pc";
		ModBuilder::BuildAudioArchives(soundsdir, audiosupermod, g_game);
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
Atlan Mod Loader v%d.0 for DOOM: Eternal and DOOM: The Dark Ages
By FlavorfulGecko5
With Special Thanks to: Proteh, Zwip-Zwap-Zapony, Tjoener, and many other talented modders!
https://github.com/FlavorfulGecko5/EntityAtlan/
----------------------------------------------
)", MOD_LOADER_VERSION);
	
	// Do not end in a / or there may be problems when running system commands 
	// (it won't translate string literal slashes to the appropriate slash like it does when using the / operator)
	fspath gamedirectory = ".";
	bool uninstallLevelMods = false;

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

		else if (arg == "--uninstall") {
			uninstallLevelMods = true;
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
	}
	else {
		atlog("FATAL ERROR: Could not locate valid game executable");
		return;
	}

	/* Cleanup last mod load. Moved this here so it's execution is independent of the executable patcher's success */
	if (CleanupLastLoad(gamedirectory) == false)
		return;

	// Warning: This MUST run after CleanupLastLoad, because we need
	// a vanilla packagemapspec to know what lightmaps should be deleted
	if (uninstallLevelMods) {
		CleanupLevelMods(gamedirectory);
		return;
	}

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

		if (g_game == game_darkages) {
			std::system("start \"\" \"steam://run/3017860//\"");
		}
		else {
			std::system("start \"\" \"steam://run/782330//\"");
		}
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