#pragma once
#include <filesystem>
#include "atlan/AtlanLib.h"

typedef std::filesystem::path fspath;

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
	// Always 0 in production archives
	// Non-zero implies an array of 16-byte structs, length header.numDependencies
	// placed in-between the ResourceDependencies and Dependency Indices arrays
	uint32_t unknown;
	
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
	heaparray<uint64_t> offsets;
	heaparray<char> dataBlock;
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


enum class EntryDataCode {
	OK,
	DATA_NOT_READ,
	UNKNOWN_COMPRESSION,
	OODLE_ERROR,
	UNUSED
};

struct ResourceDataBuffers {
	heaparray<char> raw;
	heaparray<char> decompressed;

	void init(size_t initialSizes) {
		raw.EnsureCapacity(initialSizes);
		decompressed.EnsureCapacity(initialSizes);
	}
};

// Reference to a resource entry's data.
// This structure does not own the data
struct ResourceData {
	EntryDataCode returncode = EntryDataCode::UNUSED;
	const char* buffer = nullptr;
	size_t length = 0;
};

struct ResourceArchive {
	FileReader filehandle;

	ResourceHeader     header;
	ResourceMetaHeader metaheader;    // Not present in archive version 13

	heaparray<ResourceEntry> entries;
	StringChunk stringChunk;          // Offset: header.stringTableOffset

	heaparray<ResourceDependency> dependencies; // header.numDependencies: Offset is header.resourceDepsOffset
	heaparray<uint32_t> dependencyIndex; // header.numDepIndices: Array of indices into ResourceDependency array
	heaparray<uint64_t> stringIndex; // header.numStringIndices: Array of indices into string chunk offsets array

	ResourceDataBuffers databuffers; // For storing the most recently fetched entry from a GetData call
	const char* bufferData_pk5 = nullptr; // Not owned by this struct


	/* Functions */

	public:

	ResourceArchive() = default;
	ResourceArchive(const ResourceArchive& other) = delete;
	ResourceArchive(const ResourceArchive&& other) = delete;
	ResourceArchive& operator=(const ResourceArchive& other) = delete;
	ResourceArchive& operator=(const ResourceArchive&& other) = delete;

	// Get decompressed (if applicable) data for an entry. File handle must be valid
	// Entry's data offset, size, compressed size, and compmode must be valid
	// Returned data is owned by the input buffers
	// Returned data should be copied elsewhere if it needs to be preserved before another GetData call
	ResourceData GetData(const ResourceEntry& e);

	// Gets raw data for an entry without decompressing it. File handle must be valid.
	// Entry's data offset, size, compressed size, and compmode must be valid
	// Returned data is owned by the archive's entry buffers.
	// Returned data should be copied elsewhere if it needs to be preserved before another GetData call
	ResourceData GetData_Raw(const ResourceEntry& e);

	// Get entry data. To use this function, this archive must have been read from a PK5 file entry
	// Entry's data offset, size, compressed size, and compmode must be valid
	// Returned data is owned by the archive's entry buffers.
	// Two files can be safely extracted at once without the need for copying.
	// Use the boolean arg to toggle which buffer is used.
	ResourceData GetData_PK5(const ResourceEntry& e, bool useSecondBuffer);

	// Read an archive from the filepath
	bool Read(const char* filepath);

	bool Read(const fspath& filepath, bool stopAfterEntries = false);

	bool ReadFromPK5(const char* const Data, const size_t Length);
};

struct containerMaskEntry_t {
	uint64_t hash;
	uint64_t numResources;
};

containerMaskEntry_t GetContainerMaskHash(const fspath archivepath);

struct idclMaskFile {

	struct entry {
		uint64_t hash = 0;
		uint32_t size = 0; // Size in bits
		const char* mask = nullptr;

		bool IsLoaded(uint32_t entryIndex) const {
			return *(mask + entryIndex / 8u) & (1u << (entryIndex % 8u));
		}
	};

	uint32_t timestamp = 0; // idTech7 only

	uint32_t maskcount = 0; // Number of container masks
	idclMaskFile::entry* masks = nullptr;

	char* maskblob = nullptr; // Raw container mask data
	size_t masksize = 0; // Container mask size
	
	~idclMaskFile(){
		delete[] maskblob;
		delete[] masks;
	}

	void Read(const fspath gamedir);

	// Finds the container mask for a given archive.
	// Returns an entry with hash == 0 if a container mask could not be found
	const idclMaskFile::entry FindArchiveMask(const fspath archivepath);
};

void Audit_ResourceHeader(const ResourceHeader& h, const ResourceMetaHeader& metaheader);
void Audit_ResourceArchive(const ResourceArchive& r);

void Get_EntryStrings(const ResourceArchive& r, const ResourceEntry& e, const char*& typeString, const char*& nameString);

void Get_DependencyStrings(const ResourceArchive& r, const ResourceDependency& d, const char*& typestring, const char*& namestring);

// Returns the offset where we *should* find the magic "IDCL" that denotes
// the end of the meta section. Returned value is the offset of the 'I' character
// IMPORTANT: This is only guaranteed to be accurate for retail .resources archives;
// .meta archives, archives extracted from pk5's, etc. have more stuff in them that
// can make this value inaccurate
inline uint64_t Get_ExpectedMetaOffset(const ResourceHeader& h)
{
	return h.resourceDepsOffset
		+ h.numDependencies  * sizeof(ResourceDependency)
		+ h.numDepIndices    * sizeof(uint32_t)
		+ h.numStringIndices * sizeof(uint64_t);
}

namespace idcl {

	// Decompress a "compfile" resource
	bool compfile_decompress(char* in_compdata, size_t in_complength, charbuffer_t& out_decomp);
	bool compfile_isvalid(const char* data, size_t length);

	// Reads the given file into memory and creates a compfile
	// If the file is already a compfile, the original file data is returned
	bool compfile_create(const wchar_t* filepath, charbuffer_t& out);

	// Turns the given data into a compfile
	// It is assumed that this data is not already a compfile
	bool compfile_create(const char* data, size_t length, charbuffer_t& out);

}

#include <vector>
namespace idcl {
	
	// Iterator over all resource archives in a game
	// Can optionally specify whether to use a container mask
	// If no valid container mask can be found (as is the case when reading idStudio level mods)
	// then it operates as if container mask usage wasn't enabled
	struct ArchiveIterator {
		std::vector<fspath> archivepaths;
		size_t nextarchiveIndex = 0;

		idclMaskFile maskfile;
		idclMaskFile::entry archivebitmask; // Bitmask of the current archive

		ResourceArchive archive;
		const ResourceEntry* archivemax;

		const char* typestring, *namestring; // RUNTIME: Strings of the current resource entry
		bool isEnabled;						 // RUNTIME: Whether the current file is enabled by the container mask. Only applicable if allowDisabled = true. Always true if no container mask
		bool newarchiveloaded = false;       // RUNTIME: Gets set to true once another archive is loaded


		const char* typefilter = nullptr; // CONFIG: Optional filter string for resource types
		bool allowUnmasked = false;       // CONFIG: If false, skip archives with no container mask. Only applicable if using a valid container mask
		bool allowDisabled = false;       // CONFIG If false, skip entries disabled by the container mask. Only applicable if using a valid container mask

		// RUNTIME: Returns the name of the archive currently being iterated through
		const fspath& CurrentArchive();
		
		// RUNTIME: Skip the current archive and cause the next one to be loaded in the following loop cycle
		void SkipArchive(); 

		struct iter_t {
			ArchiveIterator* controller;
			ResourceEntry* entry;

			ResourceEntry& operator*() {
				return *entry;
			}

			bool operator!=(iter_t& other) {
				return entry != other.entry;
			}

			void operator++() {
				entry = controller->next(entry);
			}
		};

		ArchiveIterator(const fspath& path_mapspec, bool UseContainerMask);
		iter_t begin();
		iter_t end();
		ResourceEntry* next(ResourceEntry* current);
		ResourceEntry* nextarchive();
	};

	// Iterator over all ResourceDependencies in a resource archive
	struct ArchiveDepIterator {
		ResourceArchive& archive;
		ResourceDependency* startDep, *endDep;
		const char* type, *name;

		ArchiveDepIterator(ResourceArchive& p_archive);

		struct iter_t {
			ArchiveDepIterator* controller;
			ResourceDependency* dependency;

			ResourceDependency& operator*() {return *dependency;}
			bool operator!=(iter_t& other) { return dependency != other.dependency;}
			void operator++() {dependency = controller->next();}
		};

		ResourceDependency* next();
		iter_t begin();
		iter_t end();
	};

	// Iterates over all ResourceDependencies belonging
	// to a particular ResourceEntry
	struct EntryDepIterator {
		const ResourceArchive& archive;
		uint32_t* startIndex, *endIndex;
		const char* type, *name;

		EntryDepIterator(const ResourceArchive& p_archive, const ResourceEntry& p_entry);

		struct iter_t {
			EntryDepIterator* controller;
			ResourceDependency* dependency;

			ResourceDependency& operator*() { return *dependency; }
			bool operator!=(iter_t& other) { return dependency != other.dependency; }
			void operator++() { dependency = controller->next(); }
		};

		ResourceDependency* next();
		iter_t begin();
		iter_t end();
	};
}

namespace idcl {
	void String_ResourceHeader(const ResourceHeader& h, const ResourceMetaHeader& meta, std::string& writeto, idclMaskFile::entry mask);
	void String_StringChunk(const StringChunk& s, std::string& writeto);
	void String_ResourceArchive(const ResourceArchive& r, std::string& writeto, idclMaskFile::entry mask);
}

struct idPackageHeader {
	uint32_t magic;
	uint64_t indexOffset; // Big Endian
	uint64_t indexSize; // Big Endian
};

struct idPackageEntry {
	std::string name;
	uint64_t offset;
	uint32_t uncompressedSize;
	uint32_t compressedSize;
	uint32_t compressionMode; // 11 is Kraken
	uint32_t timestamp;
};

// pk5 files
struct idPackage {
	
	idPackageHeader header;
	idPackageEntry* entries = nullptr;
	uint32_t numentries = 0;
	FileReader handle;

	ResourceDataBuffers entrybuffers;

	~idPackage() {
		delete[] entries;
	}

	ResourceData GetEntryData(const idPackageEntry& entry);
	bool Read(const wchar_t* filepath);

	std::string ListEntries();

	idPackage() = default;
	idPackage(const idPackage& other) = delete;
	idPackage(const idPackage&& other) = delete;
	idPackage& operator=(const idPackage& other) = delete;
	idPackage& operator=(const idPackage&& other) = delete;
};