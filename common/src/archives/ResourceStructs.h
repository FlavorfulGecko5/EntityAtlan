#pragma once
#include <filesystem>
#include <iosfwd>

typedef std::filesystem::path fspath;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

struct ResourceHeader {
    char      magic[4];                    
    uint32_t  version;					 
    uint32_t  flags;					 
    uint32_t  numSegments;				 
    uint64_t  segmentSize;				 
    uint64_t  metadataHash;				 
    uint32_t  numResources;				 
    uint32_t  numDependencies;			 
    uint32_t  numDepIndices;			 
    uint32_t  numStringIndices;			 
    uint32_t  numSpecialHashes;			 
    uint32_t  numMetaEntries;			 
    uint32_t  stringTableSize;			 
    uint32_t  metaEntriesSize;			 
    uint64_t  stringTableOffset;		 
    uint64_t  metaEntriesOffset;		 
    uint64_t  resourceEntriesOffset;	 
    uint64_t  resourceDepsOffset;
    uint64_t  resourceSpecialHashOffset; 
    uint64_t  dataOffset;
};

// This is a tightly packed structure in the files
// but the default alignment creates 4 bytes of padding
// May also wish to consider: splitting metaOffset into an integer pair.
#pragma pack(push, 1)
struct ResourceMetaHeader {
	uint32_t unknown;          // Always 0
	uint64_t metaOffset;       // Address of the 'I' char in the second "IDCL" magic
};
#pragma pack(pop)

struct ResourceEntry
{
	int64_t   resourceTypeString; // UNIVERSALLY 0 - String index of type string
	int64_t   nameString;         // UNIVERSALLY 1 - String index of file name string
	int64_t   descString;         // UNIVERSALLY -1 - String index of unused description string
	uint64_t  depIndices;
	uint64_t  strings;            // UNIVERSALLY <Entry Index> * 2
	uint64_t  specialHashes;      // UNIVERSALLY 0
	uint64_t  metaEntries;        // UNIVERSALLY 0
	uint64_t  dataOffset; // Relative to beginning of archive - possibly 8-byte aligned
	uint64_t  dataSize; // Not null-terminated

	uint64_t  uncompressedSize;
	uint64_t  dataCheckSum;
	uint64_t  generationTimeStamp;
	uint64_t  defaultHash;
	uint32_t  version;
	uint32_t  flags;
	uint8_t   compMode;
	uint8_t   reserved0;              // UNIVERSALLY 0
	uint16_t  variation;
	uint32_t  reserved2;              // UNIVERSALLY 0
	uint64_t  reservedForVariations;  // UNIVERSALLY 0

	uint16_t  numStrings;       // UNIVERSALLY 2
	uint16_t  numSources;       // UNIVERSALLY 0
	uint16_t  numDependencies;
	uint16_t  numSpecialHashes; // UNIVERSALLY 0
	uint16_t  numMetaEntries;   // UNIVERSALLY 0
    uint8_t   padding[6];
};

struct StringChunk {
	uint64_t numStrings;
	uint64_t* offsets = nullptr;   // numStrings - Relative to byte after the offset list
	char* dataBlock = nullptr;
	uint64_t paddingCount = 0; // Number of padding bytes at end of chunk. Enforces an 8-byte alignment
};

struct ResourceDependency {
	uint64_t type;
	uint64_t name;
	uint32_t depType;
	uint32_t depSubType;
	uint32_t firstInt;
	uint32_t secondInt;
	//uint64_t hashOrTimestamp;
};

struct ResourceArchive {
	char* bufferData = nullptr;

	ResourceHeader     header;
	ResourceMetaHeader metaheader;    // Not present in archive version 13
	ResourceEntry* entries = nullptr; // Offset: header.resourceEntriesOffset; Size: header.numResources
	StringChunk stringChunk;          // Offset: header.stringTableOffset

	//FSeek(header.resourceDepsOffset);
	ResourceDependency* dependencies = nullptr; // header.numDependencies
	uint32_t* dependencyIndex = nullptr; // header.numDepIndices
	uint64_t* stringIndex = nullptr; // header.numStringIndices

	~ResourceArchive() {
		delete[] bufferData;
		delete[] entries;
		delete[] dependencies;
		delete[] dependencyIndex;
		delete[] stringIndex;
		delete[] stringChunk.offsets;
		delete[] stringChunk.dataBlock;
	}
};

struct containerMaskEntry_t {
	uint64_t hash;
	uint64_t numResources;
};

containerMaskEntry_t GetContainerMaskHash(const fspath archivepath);

void Audit_ResourceHeader(const ResourceHeader& h, const ResourceMetaHeader& metaheader);
void Audit_ResourceArchive(const ResourceArchive& r);

enum ResourceFlags {
	RF_ReadEverything = 0,
	RF_SkipData = 1 << 0,
	RF_HeaderOnly = 1 << 1,
	RF_StopAfterEntries = 1 << 2
};

void Read_ResourceArchive(ResourceArchive& r, const fspath pathString, int flags);


/*
* Writes the given archive to a file
* @param entries A list of buffers, one per resource entry. If nullptr, will write out the archive's data buffer instead
*/
//void Write_ResourceArchive(const ResourceArchive& r, const fspath outpath, char** entries);

void Get_EntryStrings(const ResourceArchive& r, const ResourceEntry& e, const char*& typeString, const char*& nameString);

// Returns the offset where we *should* find the magic "IDCL" that denotes
// the end of the meta section. Returned value is the offset of the 'I' character
inline uint64_t Get_ExpectedMetaOffset(const ResourceHeader& h)
{
	return h.resourceDepsOffset
		+ h.numDependencies  * sizeof(ResourceDependency)
		+ h.numDepIndices    * sizeof(uint32_t)
		+ h.numStringIndices * sizeof(uint64_t);
}

// Returns the byte size of the gap between the archive's meta chunk and data chunk
// Starts at the 'I' in the "IDCL" magic and exclusively ends at the start of the data chunk
inline uint64_t Get_GapSize(const ResourceHeader& h)
{
	return h.dataOffset - Get_ExpectedMetaOffset(h);
}


enum class EntryDataCode {
	OK,
	DATA_NOT_READ,
	UNKNOWN_COMPRESSION,
	OODLE_ERROR,
	UNUSED
};

struct ResourceEntryData_t {
	EntryDataCode returncode = EntryDataCode::UNUSED;
	const char* buffer = nullptr;
	size_t length = 0;
};


/*
* Returns the correctly decompressed data for a given resource entry.
* Resource Archive must have it's data section read to memory
*
* If data is uncompressed or compression is unknown, returns a pointer within the archive's data buffer
*
* If data is compressed, it will be decompressed to the provided dynamically allocated buffer.
* If the provided buffer is too small, it will be deallocated and replaced with a new buffer to hold the data
*
*/
ResourceEntryData_t Get_EntryData(const ResourceArchive& r, const ResourceEntry& e, char*& decompbuffer, size_t& decompsize);


/*
* Returns the correctly decompressed data for a given resource entry
*
* (Use this overload when you haven't read the archive's entire data section to memory
*	and are reading the entry's data individually to improve performance)
*
* If the data is uncompressed or compression is unknown, returns the raw buffer
* If the data is compressed, it will be decompressed to the decomp buffer
*
* If either buffer is too small, it will be deallocated and replaced with a new buffer to hold the data
*
*/
ResourceEntryData_t Get_EntryData(const ResourceEntry& e, std::ifstream& archivestream, char*& raw, size_t& rawsize, char*& decomp, size_t& decompsize);