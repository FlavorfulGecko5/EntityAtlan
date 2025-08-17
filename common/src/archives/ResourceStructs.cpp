#include "ResourceStructs.h"
#include "hash/HashLib.h"
#include "entityslayer/Oodle.h"
#include <fstream>
#include <cassert>
#include <set>

#ifndef _DEBUG
#undef assert
#define assert(OP) (OP)
#endif

void Get_EntryStrings(const ResourceArchive& r, const ResourceEntry& e, const char*& typeString, const char*& nameString) {
	uint64_t* stringBuffer = r.stringIndex + e.strings;

	assert(e.numStrings == 2);
	assert(e.resourceTypeString == 0);
	assert(e.nameString == 1);
	assert(e.descString == -1);

	uint64_t typeOffset = r.stringChunk.offsets[*stringBuffer];
	uint64_t nameOffset = r.stringChunk.offsets[*(stringBuffer + 1)];
	typeString = r.stringChunk.dataBlock + typeOffset;
	nameString = r.stringChunk.dataBlock + nameOffset;

}

ResourceEntryData_t Get_EntryData_Internal(const ResourceEntry& e, char* raw, char*& buffer, size_t& buffersize) {
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
	if (!r.bufferData)
		return { EntryDataCode::DATA_NOT_READ, nullptr, 0 };

	char* raw = r.bufferData + (e.dataOffset - r.header.dataOffset);

	return Get_EntryData_Internal(e, raw, decompbuffer, decompsize);
}

ResourceEntryData_t Get_EntryData(const ResourceEntry& e, std::ifstream& archivestream, char*& raw, size_t& rawsize, char*& decomp, size_t& decompsize) {

	if (rawsize < e.dataSize) {
		delete[] raw;
		raw = new char[e.dataSize];
		rawsize = e.dataSize;
	}

	archivestream.seekg(e.dataOffset, std::ios_base::beg);
	archivestream.read(raw, e.dataSize);

	return Get_EntryData_Internal(e, raw, decomp, decompsize);

}


void Read_ResourceArchive(ResourceArchive& r, const fspath pathString, int flags) {

	// Read the Header
	std::ifstream opener(pathString, std::ios_base::binary);
	assert(opener.good());
	opener.read(reinterpret_cast<char*>(&r.header), sizeof(ResourceHeader));
	if (flags & RF_HeaderOnly)
		return;


	// Read the Resource Entries
	r.entries = new ResourceEntry[r.header.numResources];
	opener.seekg(r.header.resourceEntriesOffset);
	opener.read(reinterpret_cast<char*>(r.entries), r.header.numResources * sizeof(ResourceEntry));


	// Read the String Chunk
	opener.seekg(r.header.stringTableOffset);
	opener.read(reinterpret_cast<char*>(&r.stringChunk.numStrings), sizeof(uint64_t));
	r.stringChunk.offsets = new uint64_t[r.stringChunk.numStrings];
	r.stringChunk.dataBlock = new char[r.header.stringTableSize - r.stringChunk.numStrings * sizeof(uint64_t) - sizeof(uint64_t)];
	opener.read(reinterpret_cast<char*>(r.stringChunk.offsets), r.stringChunk.numStrings * sizeof(uint64_t));

	size_t stringBlockSize = r.header.stringTableSize - r.stringChunk.numStrings * sizeof(uint64_t) - sizeof(uint64_t);
	opener.read(r.stringChunk.dataBlock, stringBlockSize);

	// Count the number of padding bytes
	while (r.stringChunk.dataBlock[--stringBlockSize] == '\0') {
		r.stringChunk.paddingCount++;
	}
	r.stringChunk.paddingCount--; // We overcount by 1 due to the end string


	// Initialize Dependency Data
	r.dependencies = new ResourceDependency[r.header.numDependencies];
	r.dependencyIndex = new uint32_t[r.header.numDepIndices];
	r.stringIndex = new uint64_t[r.header.numStringIndices];

	// Read Dependency Data
	// There can be a varying number of null bytes after the final string (or none at all)
	// Hence we have to jump to the dependency offset and can't just read from where we stopped.
	opener.seekg(r.header.resourceDepsOffset);
	opener.read(reinterpret_cast<char*>(r.dependencies), r.header.numDependencies * sizeof(ResourceDependency));
	opener.read(reinterpret_cast<char*>(r.dependencyIndex), r.header.numDepIndices * sizeof(uint32_t));
	opener.read(reinterpret_cast<char*>(r.stringIndex), r.header.numStringIndices * sizeof(uint64_t));

	// TODO: must take note of IDCL size - develop assert for it
	// TODO: Account for location of data now being = file_offset - data_offset
	if (flags & RF_SkipData)
		return;

	// Determine size of data block
	opener.seekg(0, std::ios_base::beg);
	opener.seekg(0, std::ios_base::end);
	uint64_t fileLength = static_cast<uint64_t>(opener.tellg());
	opener.seekg(r.header.dataOffset);

	// Read the data block
	r.bufferData = new char[fileLength - r.header.dataOffset];
	opener.read(r.bufferData, fileLength - r.header.dataOffset);
}

void Audit_ResourceHeader(const ResourceHeader& h)
{
	assert(h.magic[0] == 'I' && h.magic[1] == 'D' && h.magic[2] == 'C' && h.magic[3] == 'L');
	assert(h.version == ARCHIVE_VERSION);
	
	#ifdef DOOMETERNAL
	assert(h.unknown == 0);
	assert(h.metaOffset == h.resourceDepsOffset + h.numDependencies * sizeof(ResourceDependency) + h.numDepIndices * sizeof(uint32_t) + h.numStringIndices * sizeof(uint64_t));
	#endif

	// Constants
	assert(h.flags == 0);
	assert(h.numSegments == 1);
	assert(h.segmentSize == 1099511627775UL);
	assert(h.metadataHash == 0);
	assert(h.numSpecialHashes == 0);
	assert(h.numMetaEntries == 0);
	assert(h.metaEntriesSize == 0);
	
	// Size Arithmetic
	assert(h.resourceEntriesOffset == sizeof(ResourceHeader));
	assert(h.resourceEntriesOffset + h.numResources * sizeof(ResourceEntry) == h.stringTableOffset);
	assert(h.stringTableSize % 8 == 0);
	assert(h.stringTableOffset + h.stringTableSize == h.resourceDepsOffset);
	assert(h.stringTableOffset + h.stringTableSize == h.metaEntriesOffset);
	assert(h.resourceDepsOffset + h.numDependencies * sizeof(ResourceDependency) == h.resourceSpecialHashOffset);
}

void Audit_ResourceArchive(const ResourceArchive& r) {
	Audit_ResourceHeader(r.header);

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

	// Audit String Chunk
	{
		const StringChunk& sc = r.stringChunk;
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
			if(e.numDependencies == 0)
				assert(e.defaultHash == e.dataCheckSum || e.defaultHash == 1);
			assert(e.flags == 0 || e.flags == 1);
			assert(e.variation == 0);
			assert(e.version <= 26);

			//if(e.flags == 1)
			//	 printf("%s\n", nameString);
		}
		else if (strcmp(typeString, "mapentities") == 0) {
			//if(e.compMode == 0)
			//	assert(e.dataCheckSum == e.defaultHash);
			assert(e.version == 81 || e.version == 80 || e.version == 77);
			assert(e.flags == 2);
			assert(e.variation == 70);
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

containerMaskEntry_t GetContainerMaskHash(const fspath archivepath) {
	std::ifstream input(archivepath, std::ios_base::binary);

	ResourceHeader h;
	input.read(reinterpret_cast<char*>(&h), sizeof(ResourceHeader));

	size_t start = sizeof(ResourceHeader);
	size_t end = h.resourceDepsOffset + h.numDependencies * sizeof(ResourceDependency) + h.numDepIndices * sizeof(uint32_t) + h.numStringIndices * sizeof(uint64_t) + 4;
	size_t len = end - start;
	char* buffer = new char[len];

	input.read(buffer, len);
	assert(buffer[len - 1] == 'L');
	assert(buffer[len - 2] == 'C');
	assert(buffer[len - 3] == 'D');
	assert(buffer[len - 4] == 'I');

	uint64_t hash = HashLib::FarmHash64(buffer, len);
	delete[] buffer;

	containerMaskEntry_t entrydata;
	entrydata.hash = hash;
	entrydata.numResources = h.numResources;
	return entrydata;
}