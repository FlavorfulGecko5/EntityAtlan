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

	enum game_t {
		game_eternal,
		game_darkages
	};

	struct eternalmeta {
		uint16_t encoding; // 2 == .opus; 3 == .wem
		uint16_t metasize;
	};

	struct entry {
		uint64_t farmhash;    // Farmhash64 of the the entry's decoded data (not necessarily the same as the encoded version stored in the archive)
		uint32_t id;          // ID used to reference this sample inside of soundbanks
		uint32_t encodedSize; // Size of the entry's data as it's stored inside the archive. In Dark Ages, always matches decodedSize
		uint32_t offset;      // Relative to beginning of file
		uint32_t decodedSize;
		union {
			uint32_t da_metasize;
			eternalmeta de;
		} metaunion;
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
	game_t game;

	~aksnd()
	{
		delete[] entrymeta;
		delete[] entries;
	}

	/*
	* FUNCTIONS
	*/
	public:



	bool ReadFrom(const char* filepath, aksnd::game_t p_game);

	std::string GetSampleName(const aksnd::entry& e, bool searchForLabel) const;

	void GetSampleData(const aksnd::entry& e, std::ifstream& stream, char*& buffer, size_t& buffersize) const;
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
	};

	std::vector<group> groups;
	std::vector<entry> masks;
	char* rawdata = nullptr;
	size_t rawsize = 0;

	~sndContainerMask() {
		delete[] rawdata;
	}
	
	void Build(const char* copyfrom, size_t length, const std::string& soundfolder);
};

struct sndMetaData2;

class AudioSampleMap
{
	std::unordered_map<uint32_t, std::string> bnk_eventstring_map;
	std::unordered_map<uint32_t, uint32_t>    sample_bnk_idmap;
	std::unordered_map<uint32_t, std::string> eternal_sample_map;
	int duplicate_sample_usages = 0;
	std::string duplicateLog;

	sndContainerMask containermask;
	aksnd::game_t game;

	public:
	bool Build_V2(std::string soundfolder);

	std::string ResolveEventName(const uint32_t sampleId) const;

	const std::string& GetDuplicateLog() const {return duplicateLog;}

	const sndContainerMask& GetMask() const {return containermask;}

	private:
	bool Build_DarkAges(sndMetaData2& metadata);
	bool Build_Eternal(sndMetaData2& metadata);
};

namespace akmetadata {
	
	typedef std::unordered_map<uint32_t, std::string> fnvmap_t;

	bool Build(fnvmap_t& fnvmap, const char* metastart, const size_t metalength);
}

namespace akpck {


	typedef std::unordered_map<uint32_t, std::string> LangMap_t;

	bool Build(LangMap_t& langmap, const char* pckstart, const size_t pcklength);

	struct entry {
		uint32_t id;
		uint32_t chunksize;
		uint32_t size;
		uint32_t offset;
		uint32_t langid;
	};

	typedef std::vector<entry> EntryList_t;

	// Hardcoded to build for the banks section and nothing else
	bool Build(EntryList_t& entries, const char* pckstart, const size_t pcklength);


}

class BinaryReader;

// Unified Interface for accessing data from idTech7 and 8 SoundMetaDatas
struct sndMetaData2 {

	aksnd::game_t version;

	const char* ptr_start = nullptr; // Ptr to start of file
	const char* ptr_end = nullptr;   // Ptr to end of file (first byte after it)

	const char* ptr_maskStart = nullptr; // Ptr to start of container mask chunk
	const char* ptr_maskEnd = nullptr;   // Ptr to first byte after container mask chunk

	const char* ptr_darkages_section6 = nullptr;     // Ptr to the "Sample Lists" section
	const char* ptr_darkages_section6_end = nullptr; // Ptr to first byte after section 6

	bool Parse_Eternal(char* data, size_t length, bool StopAfterContainerMask);
	bool Parse_DarkAges(char* data, size_t length);
	bool Parse(char* data, size_t length, bool StopAfterContainerMask);

private:
	bool Parse_ContainerMask(BinaryReader& r);
};