#include "ResourceStructs.h"
#include "hash/HashLib.h"
#include "entityslayer/Oodle.h"
#include "io/BinaryReader.h"
#include <fstream>
#include <cassert>
#include <set>

#ifndef _DEBUG
#undef assert
#define assert(OP) (OP)
#endif

void Get_EntryStrings(const ResourceArchive& r, const ResourceEntry& e, const char*& typeString, const char*& nameString) {
	uint64_t* stringBuffer = r.stringIndex.buffer + e.strings;

	assert(e.numStrings == 2);
	assert(e.resourceTypeString == 0);
	assert(e.nameString == 1);
	assert(e.descString == -1);

	uint64_t typeOffset = r.stringChunk.offsets[*stringBuffer];
	uint64_t nameOffset = r.stringChunk.offsets[*(stringBuffer + 1)];
	typeString = r.stringChunk.dataBlock.buffer + typeOffset;
	nameString = r.stringChunk.dataBlock.buffer + nameOffset;

}

void Get_DependencyStrings(const ResourceArchive& r, const ResourceDependency& d, const char*& typestring, const char*& namestring)
{
	typestring = r.stringChunk.dataBlock.buffer + r.stringChunk.offsets[d.type];
	namestring = r.stringChunk.dataBlock.buffer + r.stringChunk.offsets[d.name];
}


bool idcl::compfile_decompress(char* in_compdata, size_t in_complength, charbuffer_t& out_decomp)
{
	check(in_complength > 16);

	u64 size_decomp = *(u64*)in_compdata;
	u64 size_comp = *(u64*)(in_compdata + 8);

	// If this is true then the file's contents are not compressed
	if (size_comp == -1) {
		check(size_decomp == in_complength - 16);
		out_decomp.EnsureCapacity(size_decomp);
		out_decomp.length = size_decomp;
		memcpy(out_decomp.data, in_compdata + 16, size_decomp);
		return true;
	}

	// Verify Oodle compression signature is present
	check(in_complength - 16 == size_comp && (u8)in_compdata[16] == 0x8C)

	out_decomp.EnsureCapacity(size_decomp);
	out_decomp.length = size_decomp;

	return Oodle::DecompressBuffer(in_compdata + 16, size_comp, out_decomp.data, size_decomp);
}

bool idcl::compfile_isvalid(const char* data, size_t length) {
	if(length <= 16)
		return false;

	u64 size_decomp = *(u64*)data;
	u64 size_comp = *(u64*)(data + 8);
	if(size_comp == -1)
		return size_decomp == length - 16;

	return size_comp == length - 16 && (u8)data[16] == 0x8C;
}

bool idcl::compfile_create(const wchar_t* filepath, charbuffer_t& out)
{
	charbuffer_t input;
	if(!FileReader::ReadFile(filepath, input))
		return false;

	// Swap buffers
	// By checking if it's already a compfile, we filter out weird edge cases
	// where the user is loading an unzipped compfile
	if (compfile_isvalid(input.data, input.length)) {
		out.Swap(input);
		return true;
	}

	return idcl::compfile_create(input.data, input.length, out);
}

bool idcl::compfile_create(const char* data, size_t length, charbuffer_t& out)
{
	out.EnsureCapacity(length + 64);
	*(u64*)out.data = length;

	if(!Oodle::CompressBuffer((char*)data, length, out.data + 16, out.length))
		return false;
	*(u64*)(out.data + 8) = out.length;
	out.length += 16;

	return compfile_isvalid(out.data, out.length);
}

ResourceEntryData_t Get_EntryData_Internal(const ResourceEntry& e, const char* raw, char*& buffer, size_t& buffersize) {
	switch (e.compMode)
	{
		default:
		return {EntryDataCode::UNKNOWN_COMPRESSION, raw, e.dataSize};

		case 0:
		return {EntryDataCode::OK, raw, e.dataSize};

		case 4:
		raw += 12;
		case 2:
		{
			if (buffersize < e.uncompressedSize) {
				delete[] buffer;
				buffer = new char[e.uncompressedSize];
				buffersize = e.uncompressedSize;
			}
			bool success = Oodle::DecompressBuffer(raw, e.dataSize - (e.compMode == 4 ? 12 : 0), buffer, e.uncompressedSize);

			if(success)
				return {EntryDataCode::OK, buffer, e.uncompressedSize};
			return {EntryDataCode::OODLE_ERROR, nullptr, 0};
		}
	}
}

ResourceEntryData_t Get_EntryData(const ResourceArchive& r, const ResourceEntry& e, char*& decompbuffer, size_t& decompsize) {
	if (!r.bufferData.buffer)
		return { EntryDataCode::DATA_NOT_READ, nullptr, 0 };

	char* raw = r.bufferData.buffer + (e.dataOffset - r.header.dataOffset);

	return Get_EntryData_Internal(e, raw, decompbuffer, decompsize);
}

ResourceEntryData_t Get_EntryDataPK5(const ResourceArchive& r, const ResourceEntry& e, char*& decompbuffer, size_t& decompsize)
{
	if (!r.bufferData_pk5)
		return { EntryDataCode::DATA_NOT_READ, nullptr, 0 };

	const char* raw = r.bufferData_pk5 + (e.dataOffset - r.header.dataOffset);

	return Get_EntryData_Internal(e, raw, decompbuffer, decompsize);
}

ResourceEntryData_t Get_EntryData(const ResourceEntry& e, FileReader& reader, ResourceEntryBuffers_t& buffers) {

	if (buffers.raw_length < e.dataSize) {
		delete[] buffers.raw_buffer;
		buffers.raw_buffer = new char[e.dataSize];
		buffers.raw_length = e.dataSize;
	}

	reader.seek(e.dataOffset);
	reader.read(buffers.raw_buffer, e.dataSize);

	return Get_EntryData_Internal(e, buffers.raw_buffer, buffers.decomp_buffer, buffers.decomp_length);

}

bool Read_ResourceArchive_Internal(ResourceArchive& r, int flags) {

	FileReader& opener = r.filehandle;
	// Read the Header
	opener.read((char*)&r.header, sizeof(ResourceHeader));

	switch(r.header.version)
	{
		case 13:
		break;

		case 12:
		opener.read((char*)&r.metaheader, sizeof(ResourceMetaHeader));
		break;

		default:
		check(0, "Unknown resource version");
	}

	if (flags & RF_HeaderOnly)
		return true;

	// Read the Resource Entries
	r.entries.EnsureCapacity(r.header.numResources);
	opener.seek(r.header.resourceEntriesOffset);
	opener.read((char*)r.entries.buffer, r.header.numResources * sizeof(ResourceEntry));
	if(flags & RF_StopAfterEntries)
		return true;


	// Read the String Chunk
	opener.seek(r.header.stringTableOffset);
	opener.read((char*)&r.stringChunk.numStrings, sizeof(uint64_t));

	size_t stringBlockSize = r.header.stringTableSize - r.stringChunk.numStrings * sizeof(uint64_t) - sizeof(uint64_t);
	r.stringChunk.offsets.EnsureCapacity(r.stringChunk.numStrings);
	r.stringChunk.dataBlock.EnsureCapacity(stringBlockSize);

	opener.read((char*)r.stringChunk.offsets.buffer, r.stringChunk.numStrings * sizeof(uint64_t));
	opener.read(r.stringChunk.dataBlock.buffer, stringBlockSize);

	// Count the number of padding bytes
	while (r.stringChunk.dataBlock[--stringBlockSize] == '\0') {
		r.stringChunk.paddingCount++;
	}
	r.stringChunk.paddingCount--; // We overcount by 1 due to the end string


	// Initialize Dependency Data
	r.dependencies.EnsureCapacity(r.header.numDependencies);
	r.dependencyIndex.EnsureCapacity(r.header.numDepIndices);
	r.stringIndex.EnsureCapacity(r.header.numStringIndices);

	// Read Dependency Data
	// There can be a varying number of null bytes after the final string (or none at all)
	// Hence we have to jump to the dependency offset and can't just read from where we stopped.
	opener.seek(r.header.resourceDepsOffset);
	opener.read((char*)r.dependencies.buffer,    r.header.numDependencies * sizeof(ResourceDependency));
	opener.read((char*)r.dependencyIndex.buffer, r.header.numDepIndices * sizeof(uint32_t));
	opener.read((char*)r.stringIndex.buffer, r.header.numStringIndices * sizeof(uint64_t));

	// TODO: must take note of IDCL size - develop assert for it
	// TODO: Account for location of data now being = file_offset - data_offset
	if (flags & RF_SkipData)
		return true;

	// Determine size of data block
	uint64_t fileLength = static_cast<uint64_t>(opener.getlength());
	uint64_t dataBufferLength = fileLength - r.header.dataOffset;
	opener.seek(r.header.dataOffset);

	// Read the data block
	r.bufferData.EnsureCapacity(dataBufferLength);
	opener.read(r.bufferData.buffer, dataBufferLength);
	return true;
}

// TODO: Need to refine this function. Should refactor how buffers are stored in the archive
bool idcl::ReadResource_PK5(ResourceArchive& r, const char* const Data, const size_t Length)
{
	const char* next = Data;
	r.header = *(ResourceHeader*)next;
	next += sizeof(ResourceHeader);

	check(r.header.version == 12);
	r.metaheader = *(ResourceMetaHeader*)(next);
	next += sizeof(ResourceMetaHeader);

	// Read the Resource Entries
	r.entries.EnsureCapacity(r.header.numResources);
	next = Data + r.header.resourceEntriesOffset;
	memcpy(r.entries.buffer, next, r.header.numResources * sizeof(ResourceEntry));

	// Read the String Chunk
	next = Data + r.header.stringTableOffset;
	r.stringChunk.numStrings = *(uint64_t*)next;
	next += sizeof(uint64_t);

	size_t stringBlockSize = r.header.stringTableSize - r.stringChunk.numStrings * sizeof(uint64_t) - sizeof(uint64_t);

	r.stringChunk.offsets.EnsureCapacity(r.stringChunk.numStrings);
	r.stringChunk.dataBlock.EnsureCapacity(stringBlockSize);

	memcpy(r.stringChunk.offsets.buffer, next, r.stringChunk.numStrings * sizeof(uint64_t));
	next += r.stringChunk.numStrings * sizeof(uint64_t);
	memcpy(r.stringChunk.dataBlock.buffer, next, stringBlockSize);
	next += stringBlockSize;

	// Count the number of padding bytes
	while (r.stringChunk.dataBlock[--stringBlockSize] == '\0') {
		r.stringChunk.paddingCount++;
	}
	r.stringChunk.paddingCount--; // We overcount by 1 due to the end string


	// Initialize Dependency Data
	//INITBUFFER(ResourceDependency, r.dependencies, r.header.numDependencies, r.capacity_dependencies);
	//INITBUFFER(uint32_t, r.dependencyIndex, r.header.numDepIndices, r.capacity_depindex);
	r.stringIndex.EnsureCapacity(r.header.numStringIndices);

	// Read Dependency Data
	// There can be a varying number of null bytes after the final string (or none at all)
	// Hence we have to jump to the dependency offset and can't just read from where we stopped.
	next = Data + r.header.resourceDepsOffset;
	next += r.header.numDependencies * sizeof(ResourceDependency) + r.header.numDepIndices * sizeof(uint32_t);

	// in-between the dependencies and dependency indices
	if (r.metaheader.unknown) {
		next += 16 * r.header.numDependencies;
	}

	//opener.read((char*)r.dependencies, r.header.numDependencies * sizeof(ResourceDependency));
	//opener.read((char*)r.dependencyIndex, r.header.numDepIndices * sizeof(uint32_t));
	memcpy(r.stringIndex.buffer, next, r.header.numStringIndices * sizeof(uint64_t));

	r.bufferData_pk5 = Data + r.header.dataOffset;
	return true;
}

bool idcl::ReadResource(ResourceArchive& r, const wchar_t* pathString, int flags, bool KeepAlive)
{
	check(r.filehandle.open(pathString));

	bool result = Read_ResourceArchive_Internal(r, flags);
	if (!KeepAlive) {
		r.filehandle.close();
	}
	return result && r.filehandle.noerrors();
}

bool idcl::ReadResource(ResourceArchive& r, const char* pathString, int flags, bool KeepAlive)
{
	check(r.filehandle.open(pathString));

	bool result = Read_ResourceArchive_Internal(r, flags);
	if (!KeepAlive) {
		r.filehandle.close();
	}
	return result && r.filehandle.noerrors();
}

void Audit_ResourceHeader(const ResourceHeader& h, const ResourceMetaHeader& metaheader)
{
	assert(*(int*)h.magic == 'LCDI');

	// Version-Dependent Data
	if (h.version == 12) {
		assert(metaheader.unknown == 0);
		assert(metaheader.metaOffset == Get_ExpectedMetaOffset(h));
		assert(h.resourceEntriesOffset == sizeof(ResourceHeader) + sizeof(ResourceMetaHeader));
	}
	else if (h.version == 13) {
		assert(h.resourceEntriesOffset == sizeof(ResourceHeader));
	}
	else assert(0);


	// Constants
	assert(h.flags == 0);
	assert(h.numSegments == 1);
	assert(h.segmentSize == 1099511627775UL);
	assert(h.metadataHash == 0);
	assert(h.numSpecialHashes == 0);
	assert(h.numMetaEntries == 0);
	assert(h.metaEntriesSize == 0);
	
	// Size Arithmetic
	assert(h.resourceEntriesOffset + h.numResources * sizeof(ResourceEntry) == h.stringTableOffset);
	assert(h.stringTableSize % 8 == 0);
	assert(h.stringTableOffset + h.stringTableSize == h.resourceDepsOffset);
	assert(h.stringTableOffset + h.stringTableSize == h.metaEntriesOffset);
	assert(h.resourceDepsOffset + h.numDependencies * sizeof(ResourceDependency) == h.resourceSpecialHashOffset);
}

void Audit_ResourceArchive(const ResourceArchive& r) {
	Audit_ResourceHeader(r.header, r.metaheader);

	const std::set<std::string> logictypes = {
		"logicClass", "logicEntity", "logicFX", "logicLibrary", "logicUIWidget"
	};

	// Insure string indices are in-bounds
	for (uint32_t i = 0; i < r.header.numStringIndices; i++) {
		uint64_t stringIndex = r.stringIndex[i];
		assert(stringIndex < r.stringChunk.numStrings);
	}

	// Insure dependency string indices are in-bounds
	for (uint32_t i = 0; i < r.header.numDependencies; i++) {
		const ResourceDependency& d = r.dependencies[i];
		assert(d.type < r.stringChunk.numStrings);
		assert(d.name < r.stringChunk.numStrings);
	}

	// Insure string chunk offsets are sorted in ascending order
	// Start with the second offset, and compare with the previous offset
	for (uint64_t* ptr = r.stringChunk.offsets.buffer + 1, *max = r.stringChunk.offsets.buffer + r.stringChunk.numStrings; ptr < max; ptr++) {
		assert(*ptr > *(ptr - 1));
	}

	// Audit Entries
	for (uint64_t i = 0; i < r.header.numResources; i++) {

		const ResourceEntry& e = r.entries[i];
		const char *typeString = nullptr, *nameString = nullptr;
		Get_EntryStrings(r, e, typeString, nameString);

		assert(e.resourceTypeString == 0);
		assert(e.nameString == 1);
		assert(e.descString == -1);
		assert(e.strings == i * 2);
		assert(e.specialHashes == 0);
		assert(e.metaEntries == 0);

		assert(e.compMode == 0 || e.compMode == 2 || e.compMode == 4);
		if(e.compMode == 0) {
			assert(e.dataSize == e.uncompressedSize);
		}
		else assert(e.dataSize != e.uncompressedSize);

		// e.dataCheckSum, e.generationTimeStamp, e.defaultHash, e.version, e.flags, e.compMode
		assert(e.reserved0 == 0);
		// e.variation
		assert(e.reserved2 == 0);
		assert(e.reservedForVariations == 0);
		assert(e.numStrings == 2);
		assert(e.numSources == 0);
		// e.numDependencies
		assert(e.numSpecialHashes == 0);
		assert(e.numMetaEntries == 0);

		assert(e.dataOffset % 8 == 0);

		/*
		* The following may be tested by individual file type:
		* - depIndices          - IGNORE - Can be non-zero even if numDependencies == 0
		* - dataOffset          - IGNORE
		* - dataSize
		* - uncompressedSize    - CHECK if == uncompressedSize by file type
		* - dataCheckSum
		* - generationTimeStamp - IGNORE
		* - defaultHash         - CHECK if == dataCheckSum by file type
		* - version             - CHECK BY FILE TYPE
		* - flags               - CHECK BY FILE TYPE
		* - compMode            - CHECK BY FILE TYPE
		* - variation           - CHECK BY FILE TYPE
		* - numDependencies     - CHECK BY FILE TYPE
		*/

		if(strcmp(typeString, "rs_streamfile") == 0) {
			assert(e.dataSize == e.uncompressedSize);
			assert(e.dataCheckSum == e.defaultHash);
			assert(e.version == 0);
			assert(e.flags == 0);
			assert(e.compMode == 0);
			assert(e.variation == 0);
			assert(e.numDependencies == 0);
		}
		else if(strcmp(typeString, "entityDef") == 0) {
			//assert(e.dataSize != e.uncompressedSize);
			assert(e.dataCheckSum == e.defaultHash);
			assert(e.version == 21);
			assert(e.flags == 2);
			assert(e.compMode == 2 || e.compMode == 0);
			assert(e.variation == 70);
			//assert(e.numDependencies == 0);
		}
		else if (strcmp(typeString, "image") == 0) {
			assert(e.numDependencies == 1 || e.numDependencies == 0);
			if(e.numDependencies == 1)
				assert(e.defaultHash != e.dataCheckSum);
			//if(e.numDependencies == 0)
			//	assert(e.defaultHash == e.dataCheckSum || e.defaultHash == 1);
			assert(e.flags == 0 || e.flags == 1);
			assert(e.variation == 0);
			assert(e.version <= 26);

			//if(e.flags == 1)
			//	 printf("%s\n", nameString);
		}
		else if (strcmp(typeString, "mapentities") == 0) {
			//if(e.compMode == 0)
			assert(e.dataCheckSum != e.defaultHash);
			assert(e.version == 86 || e.version == 81 || e.version == 80 || e.version == 77);
			assert(e.flags == 2);
			assert(e.variation == 70);
		}
		else if (strcmp(typeString, "slug_font") == 0) {
			assert((e.version == 14 || e.version == 13 ) && e.flags == 0 && e.variation == 0);
		}
		else if (strcmp(typeString, "baseModel") == 0) {
			assert(e.version == 62 && (e.flags == 0 || e.flags == 1) && e.variation == 0);
		}
		else if (strcmp(typeString, "strandsHair") == 0) {
			assert(e.version == 48 && (e.flags == 0 || e.flags == 1) && e.variation == 0);
		}
		else if (logictypes.count(typeString) > 0) {
			//printf("%s\n", typeString);
			assert(e.dataCheckSum == e.defaultHash);
			assert(e.version == 4);
			assert(e.flags == 2);
			assert(e.variation == 70);
			assert(e.compMode == 0 || e.compMode == 2);
			
			// Not true
			//assert(e.numDependencies == 0);
		}
	}

}

void idclMaskFile::Read(const fspath gamedir)
{
	// Read the container mask blob into memory
	{
		ResourceArchive meta;
		const fspath metapath = gamedir / "base/meta.resources";
		if(!std::filesystem::exists(metapath))
			return;
		idcl::ReadResource(meta, metapath.c_str(), RF_ReadEverything, false);
		assert(meta.header.numResources == 1);

		char* decompbuffer = nullptr;
		size_t decompsize = 0;

		ResourceEntryData_t maskdata = Get_EntryData(meta, meta.entries[0], decompbuffer, decompsize);
		assert(maskdata.returncode == EntryDataCode::OK);

		masksize = maskdata.length;
		maskblob = new char[maskdata.length];
		memcpy(maskblob, maskdata.buffer, maskdata.length);

		delete[] decompbuffer;
	}

	// Parse the container mask
	{
		BinaryReader reader(maskblob, masksize);

		reader.ReadLE(maskcount);
		if (maskcount & 0xFFFFF000) {
			timestamp = maskcount;
			reader.ReadLE(maskcount);
		}

		masks = new idclMaskFile::entry[maskcount];
		for (uint32_t i = 0; i < maskcount; i++) {
			idclMaskFile::entry& e = masks[i];

			reader.ReadLE(e.hash);
			reader.ReadLE(e.size); // Mask size in 64-byte numbers
			reader.ReadBytes(e.mask, e.size * sizeof(uint64_t));

			e.size *= 64; // Convert mask size to bits
		}
		assert(reader.GetRemaining() == 0);
	}
}

const idclMaskFile::entry idclMaskFile::FindArchiveMask(const fspath archivepath)
{
	containerMaskEntry_t entry = GetContainerMaskHash(archivepath);

	for (uint32_t i = 0; i < maskcount; i++) {
		if(masks[i].hash == entry.hash)
			return masks[i];
	}

	return idclMaskFile::entry();
}

containerMaskEntry_t GetContainerMaskHash(const fspath archivepath) {
	std::ifstream input(archivepath, std::ios_base::binary);
	assert(input.good());

	ResourceHeader h;
	input.read(reinterpret_cast<char*>(&h), sizeof(ResourceHeader));

	size_t start = h.resourceEntriesOffset; // Assumes entries follow the header
	size_t end = Get_ExpectedMetaOffset(h) + 4;
	size_t len = end - start;
	char* buffer = new char[len];

	input.seekg(start, std::ios_base::beg);
	input.read(buffer, len);
	assert(*(int*)(buffer + len - 4) == 'LCDI'); // Verify ending magic is present

	uint64_t hash = HashLib::FarmHash64(buffer, len);
	delete[] buffer;

	containerMaskEntry_t entrydata;
	entrydata.hash = hash;
	entrydata.numResources = h.numResources;
	return entrydata;
}

#include "PackageMapSpec.h"

const fspath& idcl::ArchiveIterator::CurrentArchive()
{
	return archivepaths[nextarchiveIndex - 1];
}

void idcl::ArchiveIterator::SkipArchive()
{
	archivemax = nullptr;
}

idcl::ArchiveIterator::ArchiveIterator(const fspath& path_mapspec, bool p_UseContainerMask) {

	archivepaths = PackageMapSpec::GetArchiveList(path_mapspec, false);

	if (p_UseContainerMask) {
		maskfile.Read(path_mapspec.parent_path().parent_path());
	}
}

ResourceEntry* idcl::ArchiveIterator::nextarchive() {

	while (1) {
		if (nextarchiveIndex >= archivepaths.size())
			return nullptr;

		const fspath& nextpath = archivepaths[nextarchiveIndex++];

		//printf("%ls\n", nextpath.c_str());

		if (maskfile.maskcount) {
			archivebitmask = maskfile.FindArchiveMask(nextpath);
			if (!archivebitmask.size && !allowUnmasked) {
				//printf("Skipping %ls \n", nextpath.c_str());
				continue;
			}
		}

		if (!idcl::ReadResource(archive, nextpath.c_str(), RF_SkipData, true)) {
			return nullptr;
		}

		if (archive.header.numResources == 0)
			continue;

		archivemax = archive.entries.buffer + archive.header.numResources;
		newarchiveloaded = true;
		return archive.entries.buffer;
	}
}

ResourceEntry* idcl::ArchiveIterator::next(ResourceEntry* current) {

	while (1) {
		current++;

		if (current >= archivemax) {
			current = nextarchive();
			if (!current) {
				return nullptr;
			}
		}

		Get_EntryStrings(archive, *current, typestring, namestring);

		if (archivebitmask.size) {
			isEnabled = archivebitmask.IsLoaded(static_cast<uint32_t>(current - archive.entries.buffer));
			if (!isEnabled && !allowDisabled) {
				continue;
			}
		} 
		else {
			isEnabled = true;
		}

		if(typefilter && strcmp(typefilter, typestring))
			continue;

		return current;
	}

}

idcl::ArchiveIterator::iter_t idcl::ArchiveIterator::begin() {
	// Ensure we read the first archive
	nextarchiveIndex = 0;
	archivemax = nullptr; 

	return {this, next(nullptr)};
}

idcl::ArchiveIterator::iter_t idcl::ArchiveIterator::end() {
	return {this, nullptr};
}

// --- Archive Dep Iterator

idcl::ArchiveDepIterator::ArchiveDepIterator(ResourceArchive& p_archive)
	: archive(p_archive) {}

ResourceDependency* idcl::ArchiveDepIterator::next()
{
	startDep++;
	if(startDep >= endDep)
		return nullptr;

	Get_DependencyStrings(archive, *startDep, type, name);

	return startDep;
}

idcl::ArchiveDepIterator::iter_t idcl::ArchiveDepIterator::begin()
{
	startDep = archive.dependencies.buffer - 1;
	endDep = archive.dependencies.buffer + archive.header.numDependencies;

	return {this, next()};
}

idcl::ArchiveDepIterator::iter_t idcl::ArchiveDepIterator::end()
{
	return {this, nullptr};
}

// ---- Entry Dep Iterator

idcl::EntryDepIterator::EntryDepIterator(const ResourceArchive& p_archive, const ResourceEntry& p_entry)
	: archive(p_archive) 
{
	startIndex = archive.dependencyIndex.buffer + p_entry.depIndices;
	endIndex = startIndex + p_entry.numDependencies;
}

ResourceDependency* idcl::EntryDepIterator::next()
{
	startIndex++;
	if(startIndex >= endIndex)
		return nullptr;

	ResourceDependency* dep = archive.dependencies.buffer + *startIndex;

	Get_DependencyStrings(archive, *dep, type, name);
	return dep;
}

idcl::EntryDepIterator::iter_t idcl::EntryDepIterator::begin()
{
	// TODO: Since we're not storing the ResourceEntry this iterator can only
	// run once. Fix this?
	startIndex--;
	return {this, next()};
}

idcl::EntryDepIterator::iter_t idcl::EntryDepIterator::end() {
	return {this, nullptr};
}

#define ts(NAME) {writeto.append(#NAME " = "); writeto.append(std::to_string(h.NAME)); writeto.append(";\n");}

void idcl::String_ResourceHeader(const ResourceHeader& h, const ResourceMetaHeader& meta, std::string& writeto, idclMaskFile::entry mask) {
	writeto.append("header {\n");
		 
	writeto.append("magic = \"");
	writeto.push_back(h.magic[0]);
	writeto.push_back(h.magic[1]);
	writeto.push_back(h.magic[2]);
	writeto.push_back(h.magic[3]);
	writeto.append("\";\n");

	ts(version);
	ts(flags);
	ts(numSegments);
	ts(segmentSize);
	ts(metadataHash);
	ts(numResources);
	ts(numDependencies);
	ts(numDepIndices);
	ts(numStringIndices);
	ts(numSpecialHashes);
	ts(numMetaEntries);
	ts(stringTableSize);
	ts(metaEntriesSize);

	ts(stringTableOffset);
	ts(metaEntriesOffset);
	ts(resourceEntriesOffset);
	ts(resourceDepsOffset);
	ts(resourceSpecialHashOffset);

	ts(dataOffset);

	if (h.version < 13) {
		writeto.append("unknown = "); writeto.append(std::to_string(meta.unknown)); writeto.append(";\n");
		writeto.append("metaOffset = "); writeto.append(std::to_string(meta.metaOffset)); writeto.append(";\n");
	}

	if (mask.size) {

		size_t extraslots = (h.numResources % 64) ? (64 - h.numResources % 64) : 0;

		// Lazy approach that assumes all extra slots have the same value
		bool useableExtras = extraslots ? mask.IsLoaded(h.numResources) : false;

		writeto.append("container_mask {\nhash = "); writeto.append(std::to_string(mask.hash)); 
		writeto.append(";\nextra_slots = "); writeto.append(std::to_string(extraslots));
		writeto.append(";\nextra_slots_are_enabled = "); writeto.append(useableExtras ? "1" : "0");
		writeto.append(";\n}\n");
	}
	else {
		writeto.append("NO_CONTAINER_MASK\n");
	}
	writeto.append("}\n");
}

void idcl::String_StringChunk(const StringChunk& s, std::string& writeto)
{
	writeto.append("strings {\n");

	for (uint64_t i = 0; i < s.numStrings; i++) {
		writeto.push_back('"');
		writeto.append(s.dataBlock.buffer + s.offsets[i]);
		writeto.append("\"\n");
	}
	writeto.append("}\n");
	writeto.append("stringChunkPadding = ");
	writeto.append(std::to_string(s.paddingCount));
	writeto.append("\n");
}

void idcl::String_ResourceArchive(const ResourceArchive& r, std::string& writeto, idclMaskFile::entry mask) {
	String_ResourceHeader(r.header, r.metaheader, writeto, mask);
	String_StringChunk(r.stringChunk, writeto);

	writeto.append("files {\n");

	for (uint64_t i = 0; i < r.header.numResources; i++) {
		const ResourceEntry& h = r.entries[i];

		const char* typeString = nullptr, * nameString = nullptr;
		Get_EntryStrings(r, h, typeString, nameString);

		writeto.push_back('"');
		writeto.append(typeString);
		writeto.push_back('/');
		writeto.append(nameString);
		writeto.append("\" ");

		if (mask.size) {
			writeto.append(mask.IsLoaded((u32)i) ? "1 {\n" : "0 {\n");
		}
		else {
			writeto.append("{\n");
		}

		//ts(resourceTypeString);
		//ts(nameString);
		//ts(descString);

		if(h.numDependencies) ts(depIndices);
		if(h.strings != i * 2) ts(strings);
		if(h.specialHashes) ts(specialHashes);
		if(h.metaEntries) ts(metaEntries);

		ts(dataOffset);
		ts(dataSize);
		if(h.dataSize != h.uncompressedSize) ts(uncompressedSize);
		//ts(dataCheckSum);
		//ts(generationTimeStamp);
		if(h.defaultHash != h.dataCheckSum) ts(defaultHash);

		ts(version);
		if(h.flags) ts(flags);
		if(h.compMode) ts(compMode);

		if(h.reserved0) ts(reserved0);
		if(h.variation) ts(variation);
		if(h.reserved2) ts(reserved2);
		if(h.reservedForVariations) ts(reservedForVariations);

		if(h.numStrings != 2) ts(numStrings);
		if(h.numSources) ts(numSources);
		if(h.numDependencies) ts(numDependencies);
		if(h.numSpecialHashes) ts(numSpecialHashes);
		if(h.numMetaEntries) ts(numMetaEntries);

		if(!h.numDependencies) {
			writeto.append("}\n");
			continue;
		}

		writeto.append("dependencies {\n");

		idcl::EntryDepIterator depiter(r, h);
		for (const ResourceDependency& d : depiter) {

			writeto.append("\"#"); // A # makes it easier to cntrl+f only for entry strings
			writeto.append(depiter.type);
			writeto.push_back('/');
			writeto.append(depiter.name);

			#if 1
			writeto.append("\"\n");
			#else
			writeTo.append("\" {\n");

			writeTo.append("\ndepType = "); writeTo.append(std::to_string(d.depType));
			writeTo.append("\ndepSubType = "); writeTo.append(std::to_string(d.depSubType));
			//writeTo.append("\ntimestampOrHash = "); writeTo.append(std::to_string(d.hashOrTimestamp));
			writeTo.append("\nfirstInt = "); writeTo.append(std::to_string(d.firstInt));
			writeTo.append("\nsecondInt = "); writeTo.append(std::to_string(d.secondInt));

			writeTo.append("\n}\n");
			#endif
		}

		writeto.append("}\n}\n");
	}


	writeto.append("}\n");
}

ResourceEntryData_t idPackage::GetEntryData(const idPackageEntry& entry, ResourceEntryBuffers_t& buffers)
{
	handle.seek(entry.offset);

	if (buffers.decomp_length < entry.uncompressedSize) {
		buffers.decomp_length = entry.uncompressedSize;
		delete[] buffers.decomp_buffer;
		buffers.decomp_buffer = new char[entry.uncompressedSize];
	}

	if (entry.compressionMode) {
		if(entry.compressionMode != 11)
			return {EntryDataCode::UNKNOWN_COMPRESSION};

		if (buffers.raw_length < entry.compressedSize) {
			buffers.raw_length = entry.compressedSize;
			delete[] buffers.raw_buffer;
			buffers.raw_buffer = new char[entry.compressedSize];
		}

		handle.read(buffers.raw_buffer, entry.compressedSize);

		bool result = Oodle::DecompressBuffer(buffers.raw_buffer, entry.compressedSize, 
			buffers.decomp_buffer, entry.uncompressedSize);
		if(!result)
			return {EntryDataCode::OODLE_ERROR};
	}
	else {
		handle.read(buffers.decomp_buffer, entry.uncompressedSize);
	}

	return {EntryDataCode::OK, buffers.decomp_buffer, entry.uncompressedSize };

}

// TODO: More error checking
bool idPackage::Read(const wchar_t* filepath)
{
	if(!handle.open(filepath))
		return false;

	handle.read((char*)&header.magic, sizeof(header.magic));
	if(header.magic != 0x03D0C0CA)
		return false;

	handle.read((char*) &header.indexOffset, 16);
	header.indexOffset = _byteswap_uint64(header.indexOffset);
	header.indexSize = _byteswap_uint64(header.indexSize);

	handle.seek(header.indexOffset);

	charbuffer_t rawentries;
	rawentries.data = new char[header.indexSize];
	handle.read(rawentries.data, header.indexSize);

	BinaryReader reader(rawentries.data, header.indexSize);
	reader.ReadBig(numentries);

	check(numentries < 1000);

	delete[] entries;
	entries = new idPackageEntry[numentries];

	for (u32 i = 0; i < numentries; i++) {

		idPackageEntry& e = entries[i];

		const char* bytes;
		u32 stringlength;
		reader >> stringlength;
		reader.ReadBytes(bytes, stringlength);

		e.name = std::string(bytes, stringlength);
		reader.ReadBig(e.offset);
		reader.ReadBig(e.uncompressedSize);
		reader.ReadBig(e.compressedSize);
		reader.ReadBig(e.compressionMode);
		reader >> e.timestamp;
	}

	return reader.ReachedEOF();
}

std::string idPackage::ListEntries()
{
	std::string list;
	list.reserve(numentries * 128);

	ResourceEntryBuffers_t entrybuffers;
	ResourceArchive subarc;

	for (u32 i = 0; i < numentries; i++) {
		list.push_back('"');
		list.append(entries[i].name);
		list.append("\" ");
		list.append(entries[i].compressionMode ? "1\n" : "0\n");

		ResourceEntryData_t data = GetEntryData(entries[i], entrybuffers);
		check_debug(data.returncode == EntryDataCode::OK);

		if(data.length < 4)
			continue;
		if(*(u32*)data.buffer != 'LCDI')
			continue;
		
		bool result = idcl::ReadResource_PK5(subarc, data.buffer, data.length);
		check_debug(result);

		if (subarc.header.numMetaEntries) {
			int meta = 0;
		}
		
		for (u32 RESINDEX = 0; RESINDEX < subarc.header.numResources; RESINDEX++) {

			const ResourceEntry& e = subarc.entries[RESINDEX];

			if (e.compMode) {
				int debug_compmode = 0;
			}

			const char* typestring, *namestring;
			Get_EntryStrings(subarc, e, typestring, namestring);

			list.append("   - \"");
			list.append(typestring);
			list.push_back(':');
			list.append(namestring);
			list.append("\"\n");

			if (e.numSources) {
				int sources = 0;
			}

			if (strcmp(typestring, "compfile") == 0) {
				int derpy = 0;
			}
		}
	}
	return list;
}
