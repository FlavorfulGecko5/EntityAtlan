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
		uint64_t unknown; // Probably a checksum?
		uint32_t id;
		uint32_t encodedSize;
		uint32_t offset; // Relative to beginning of file
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
		uint32_t entrymetasize; // Size of the header's entry meta section

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
};

class AudioSampleMap
{
	std::unordered_map<uint32_t, std::string> bnk_eventstring_map;
	std::unordered_map<uint32_t, uint32_t> sample_bnk_idmap;
	int duplicate_sample_usages = 0;
	std::string duplicateLog;

	public:
	void Build(std::string soundfolder);
	std::string ResolveEventName(const uint32_t sampleId) const;

	const std::string& GetDuplicateLog() const {return duplicateLog;}
};