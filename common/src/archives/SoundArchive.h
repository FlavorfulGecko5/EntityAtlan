#pragma once

#include <string>
#include <set>
#include <iosfwd>
#include <unordered_map>

typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;

class aksnd 
{
	public:

	/*
	* TYPES
	*/

	struct entry {
		uint64_t farmhash;    // Farmhash64 of the the entry's decoded data (not necessarily the same as the encoded version stored in the archive)
		uint32_t id;          // ID used to reference this sample inside of soundbanks
		uint32_t encodedSize; // Size of the entry's data as it's stored inside the archive. In Dark Ages, always matches decodedSize
		uint32_t offset;      // Relative to beginning of file
		uint32_t decodedSize;
		uint32_t metasize;
		uint32_t metaoffset; // Relative to global offset 0xC

		//	ushort soundFormat = binaryReader.ReadUInt16();
		//		2 = .opus; 3 = .wem (music; conversion required)
		//	2 bytes: Length of this entry's Header Section #1 chunk
		//		DARK AGES: This field is 4 bytes, soundFormat is gone
	};

	struct header_start {
		uint32_t version;       // Should always be 6?
		uint32_t headersize;    // Size of the entire header chunk
		uint32_t entrymetasize; // Size of the header's entry meta section. This field is the 4 bytes of the header chunk

		uint32_t datastart() const { // Start of data chunk
			return sizeof(version) + sizeof(headersize) + headersize;
		}
	};

	/*
	* VARIABLES
	*/

	public:

	aksnd::header_start headerStart;
	uint32_t numentries;
	char* entrymeta = nullptr;
	aksnd::entry* entries = nullptr;

	~aksnd()
	{
		delete[] entrymeta;
		delete[] entries;
	}

	/*
	* FUNCTIONS
	*/
	public:



	bool ReadFrom(const char* filepath);

	std::string GetSampleName(const aksnd::entry& e, bool searchForLabel) const;

	void GetSampleData(const aksnd::entry& e, std::ifstream& stream, char*& buffer, size_t& buffersize) const;

	struct samplehash {
		uint64_t lower;
		uint64_t upper;

		bool operator!=(const samplehash& other) const {
			return lower != other.lower || upper != other.upper;
		}
	};

	aksnd::samplehash GetSampleHash(const aksnd::entry& e);
};



struct sndContainerMask {
	struct entry
	{
		std::string fnvstring; // String that was hashed to make the fnvhash. Should be the filename without the extension (i.e. "SFX" or "SFX_patch_1")
		uint32_t fnvhash;     // Identifier for the container
		uint32_t size = 0; // In Bits
		const char* mask = nullptr;

		bool IsLoaded(uint32_t entryIndex) const {
			return *(mask + entryIndex / 8u) & (1u << (entryIndex % 8u));
		}
	};

	struct group
	{
		std::string groupname;   // Should be the base archive's filename (i.e. "SFX.snd" or "MUSIC.snd")
		uint32_t maskcount = 0;  // Number of masks in this group
		uint32_t firstindex = 0; // Index into sndContainerMask.masks

		std::string modfnvstring; // If not empty, will add an additional mask to this group with this string
	};

	std::vector<group> groups;
	std::vector<entry> masks;
	char* rawdata = nullptr;
	size_t rawsize = 0;

	~sndContainerMask() {
		delete[] rawdata;
	}

	void Build(const std::string soundfolder);
	
	void Build(const char* copyfrom, size_t length, const std::string& soundfolder);
};

// Representation of soundmetadata.bin
class sndMetadata 
{
	sndContainerMask ContainerMask;
	size_t containermaskIndex = 0;

	char* rawfile = nullptr;
	size_t filelength = 0;


public:
	~sndMetadata() {
		delete[] rawfile;
	}

	// Returns the start index of the Container Mask Chunk
	static size_t FindContainerMask(const char* metastart, const size_t metalength);

	void Read(const std::string& soundfolder);
};

class AudioSampleMap
{
	std::unordered_map<uint32_t, std::string> bnk_eventstring_map;
	std::unordered_map<uint32_t, uint32_t> sample_bnk_idmap;
	int duplicate_sample_usages = 0;
	std::string duplicateLog;

	sndContainerMask containermask;

	public:
	void Build(std::string soundfolder);
	std::string ResolveEventName(const uint32_t sampleId) const;

	const std::string& GetDuplicateLog() const {return duplicateLog;}

	const sndContainerMask& GetMask() const {return containermask;}
};