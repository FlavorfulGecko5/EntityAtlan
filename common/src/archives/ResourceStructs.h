#pragma once
#include <filesystem>
#include <iosfwd>

typedef std::filesystem::path fspath;

//#define DOOMETERNAL

#ifdef DOOMETERNAL
#define ARCHIVE_VERSION 12
#else
#define ARCHIVE_VERSION 13
#endif

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

#ifdef DOOMETERNAL
#pragma pack(push, 1)
#endif
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
	#ifdef DOOMETERNAL
	uint32_t unknown; // Creates 4 bytes of wasted space with default alignment
	uint64_t metaOffset;
	#endif
};
#ifdef DOOMETERNAL
#pragma pack(pop)
#endif

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
	char* compactmeta = nullptr;

	ResourceHeader header;

	//FSeek(header.resourceEntriesOffset);
	ResourceEntry* entries = nullptr; // header.numResources

	//FSeek(header.stringTableOffset);
	StringChunk stringChunk;

	//FSeek(header.resourceDepsOffset);
	ResourceDependency* dependencies = nullptr; // header.numDependencies
	uint32_t* dependencyIndex = nullptr; // header.numDepIndices
	uint64_t* stringIndex = nullptr; // header.numStringIndices

	uint64_t idclOffset; // Eternal archive already tells us this
	uint64_t idclSize; // Size of IDCL block before data. 

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

void Audit_ResourceHeader(const ResourceHeader& h);
void Audit_ResourceArchive(const ResourceArchive& r);

enum ResourceFlags {
	RF_ReadEverything = 0,
	RF_SkipData = 1 << 0,
	RF_HeaderOnly = 1 << 1
};

void Read_ResourceArchive(ResourceArchive& r, const fspath pathString, int flags);

void Get_EntryStrings(const ResourceArchive& r, const ResourceEntry& e, const char*& typeString, const char*& nameString);


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