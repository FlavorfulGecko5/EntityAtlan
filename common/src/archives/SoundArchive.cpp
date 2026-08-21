#include "SoundArchive.h"
#include "io/BinaryReader.h"
#include <fstream>
#include <string>
#include <cassert>
#include <filesystem>
#include "hash/HashLib.h"

// Need the assert operations to still execute
#ifndef _DEBUG
#undef assert
#define assert(OP) if(!(OP)) {throw std::exception("Failure in SoundArchive.cpp");}
#endif

// format:
// 4 bytes: Always = 6 (version?)
// 4 bytes: Length of entire header chunk beginning immediately after this section
// 4 bytes: Length of first sub-section of header chunk
	// Previous value - this - 4 (size of this value) == Size of entry table

// Header Section #1:
// - Copy of each entry's data up until (and including) the length of the entry's data chunk
//    - The files are RIFF-formatted
//    - The real entries may be encoded and scrambled, but this section will give their unencrypted versions
// - End of Section: Up to 4 bytes of padding until a 4 byte alignment is reached
//   - First padding byte is 1, others are 0 

/*
* Header Section #2: Entry Table: 32 bytes per entry
*   8 bytes: unknown
	uint soundId = binaryReader.ReadUInt32();
	uint encodedSize = binaryReader.ReadUInt32();
	uint soundDataOffset = binaryReader.ReadUInt32(); - Relative to beginning of file
	uint decodedSize = binaryReader.ReadUInt32();

	
	ushort soundFormat = binaryReader.ReadUInt16();
		- 2 = .opus; 3 = .wem (music; conversion required)
	2 bytes: Length of this entry's Header Section #1 chunk
		- DARK AGES: This field is 4 bytes, soundFormat is gone


	4 bytes: Offset of this entry's Header Section #1 chunk, relative to global offset 0xC
*/


bool aksnd::ReadFrom(const char* filepath)
{
	// Read Header into memory
	std::ifstream reader(filepath, std::ios_base::binary);
	assert(reader.good());
	reader.seekg(0, std::ios_base::end);
	size_t filelength = reader.tellg();
	reader.seekg(0, std::ios_base::beg);
	reader.read(reinterpret_cast<char*>(&headerStart), sizeof(header_start));

	numentries = (headerStart.headersize - headerStart.entrymetasize - sizeof(headerStart.entrymetasize)) / sizeof(entry);

	entrymeta = new char[headerStart.entrymetasize];
	entries = new entry[numentries];

	reader.read(entrymeta, headerStart.entrymetasize);
	reader.read(reinterpret_cast<char*>(entries), numentries * sizeof(entry) );
	return true;
}

std::string aksnd::GetSampleName(const aksnd::entry& e, bool searchForLabel) const
{
	// For music specifically, there's an addtllabl field
	const char* metachunk = entrymeta + e.metaoffset;
	const char* metamax = metachunk + e.metasize;

	std::string entryname;

	// Not the safest thing to do...should really do a proper parse through
	// the chunks to see if this field exists instead of this.
	if (searchForLabel)
	{
		while (metachunk < metamax) {
			if (*metachunk == 'a' && memcmp(metachunk, "adtllabl", 8) == 0)
			{
				metachunk += 8;

				// Length includes 4 null bytes at beginning of string we must skip
				uint32_t stringlength = *reinterpret_cast<const uint32_t*>(metachunk) - 4;
				metachunk += 8;
				entryname = std::string(metachunk, stringlength) + "_";
				break;
			}
			metachunk++;
		}
	}

	// If name is not included in the sample, simply return the id for now
	entryname += std::to_string(e.id) + ".wav";
	return entryname;
}

void aksnd::GetSampleData(const aksnd::entry& e, std::ifstream& stream, char*& buffer, size_t& buffersize) const
{
	//assert(e.encodedSize == e.decodedSize);
	if (buffersize < e.encodedSize) {
		delete[] buffer;
		buffer = new char[e.encodedSize];
		buffersize = e.encodedSize;
	}

	stream.seekg(e.offset, std::ios_base::beg);
	stream.read(buffer, e.encodedSize);
}

bool AudioSampleMap::Build_V2(std::string soundfolder)
{
	charbuffer_t rawmeta;
	FileReader::ReadFile((soundfolder + "/soundmetadata.bin").c_str(), rawmeta);

	sndMetaData2 parsedmeta;
	if (!parsedmeta.Parse_DarkAges(rawmeta.data, rawmeta.length)) {
		return false;
	}

	duplicateLog = "Some audio samples are used in multiple sound events.\n"
		"This file logs all duplicate usages of a single audio sample\n\n";

	// Step 2: Build out the sample map
	std::unordered_map<uint32_t, std::set<uint32_t>> duplicatesets; // Dynamic STL happy fun time!!!!
	duplicatesets.reserve(4000);
	{
		// Section 6 of the sound meta data maps sample IDs to strings
		BinaryReader reader(parsedmeta.ptr_darkages_section6, parsedmeta.ptr_darkages_section6_end - parsedmeta.ptr_darkages_section6);
		uint32_t total = 0, stringlength = 0, bnkfnv = 0;
		const char* string = nullptr;

		reader >> total;
		for (uint32_t i = 0; i < total; i++) {
			reader >> stringlength;
			reader.ReadBytes(string, stringlength);
			reader >> bnkfnv;

			check(bnkfnv == HashLib::akfnv_insensitive(string, stringlength));
			check(bnk_eventstring_map.find(bnkfnv) == bnk_eventstring_map.end());
			bnk_eventstring_map[bnkfnv] = std::string(string, stringlength);

			uint8_t extralistflag = 0;
			uint32_t listlength = 0;
			reader.GoRight(5);
			reader >> extralistflag;
			reader.GoRight(5);
			reader >> listlength;

			if (extralistflag == 0) {
				for (uint32_t listind = 0; listind < listlength; listind++) {
					reader >> stringlength;
					reader.GoRight(stringlength + 4);
				}
				reader.ReadLE(listlength);
			}

			for (uint32_t sampleindex = 0; sampleindex < listlength; sampleindex++) {
				uint32_t sampleid = 0;

				reader.ReadLE(sampleid);
				reader.ReadLE(stringlength);
				reader.GoRight(stringlength); // Language string, not the sample name

				if (sample_bnk_idmap.find(sampleid) != sample_bnk_idmap.end()) {
					duplicate_sample_usages++;
					duplicatesets[sampleid].insert(bnkfnv);
				}
				else {
					sample_bnk_idmap[sampleid] = bnkfnv;
				}
			}
		}

		check(reader.ReachedEOF());
	}

	// Step 3: Write out the duplicate log
	for (const auto& pair : duplicatesets) {
		duplicateLog.append(std::to_string(pair.first));
		duplicateLog.append(" - Extracted As: ");
		duplicateLog.append(bnk_eventstring_map[sample_bnk_idmap[pair.first]]);
		duplicateLog.append("\nAlso Used In:\n");
		for (uint32_t otherbnk : pair.second) {
			duplicateLog.append(bnk_eventstring_map[otherbnk]);
			duplicateLog.append("\n");
		}
		duplicateLog.append("\n\n");
	}

	// Step 4: Build the container mask
	containermask.Build(parsedmeta.ptr_maskStart, parsedmeta.ptr_maskEnd - parsedmeta.ptr_maskStart, soundfolder);
}

std::string AudioSampleMap::ResolveEventName(const uint32_t sampleId) const
{
	const auto& iter = sample_bnk_idmap.find(sampleId);
	if (iter == sample_bnk_idmap.end()) {
		return "~UNRESOLVED";
	}
	
	uint32_t bnkid = iter->second;
	const auto& stringiter = bnk_eventstring_map.find(bnkid);
	assert(stringiter != bnk_eventstring_map.end());

	return stringiter->second;
}

void sndContainerMask::Build(const char* copyfrom, size_t length, const std::string& soundfolder)
{
	this->rawsize = length;
	rawdata = new char[rawsize];
	memcpy(rawdata, copyfrom, rawsize);
	BinaryReader mask(rawdata, rawsize);

	groups.reserve(16);
	masks.reserve(48);

	uint32_t numgroups = 0;

	assert(mask.ReadLE(numgroups));
	for (uint32_t i = 0; i < numgroups; i++) {

		const char* groupname = nullptr;
		uint32_t groupnamelength = 0;
		uint32_t numarchives; // Number of archives of this type

		// Group Name
		assert(mask.ReadLE(groupnamelength));
		assert(mask.ReadBytes(groupname, groupnamelength));
		assert(mask.ReadLE(numarchives));

		groups.emplace_back();
		group& g = groups.back();
		g.groupname = std::string(groupname, groupnamelength);
		g.firstindex = masks.size();
		g.maskcount = numarchives;

		for (uint32_t arcindex = 0; arcindex < numarchives; arcindex++) {
			
			entry e;
			assert(mask.ReadLE(e.fnvhash));
			assert(mask.ReadLE(e.size));
			assert(mask.ReadBytes(e.mask, e.size * sizeof(uint32_t)));
			e.size *= 32; // Convert from integers to bits

			masks.push_back(e);
		}

	}

	// In a modded soundmetadata there will be stuff after the container mask
	//assert(mask.ReachedEOF());

	// Populate the fnv strings

	typedef std::filesystem::path fspath;

	for (const auto& entry : std::filesystem::directory_iterator(soundfolder))
	{
		if(entry.is_directory())
			continue;

		fspath path = entry.path();
		if(path.extension() != ".snd")
			continue;

		std::string filename = path.stem().string();

		uint32_t fnv = HashLib::akfnv_insensitive(filename.data(), filename.length());

		// LOOP #1
		bool found = false;
		size_t maskcount = masks.size();
		for (size_t i = 0; i < maskcount; i++)
		{
			if (masks[i].fnvhash == fnv) {
				found = true;
				masks[i].fnvstring = filename;
				break;
			}
		}

		// LOOP #2: Account for hardcoded hashes only present
		// in Doom The Dark Ages
		if (found)
			continue;
		if(filename == "MUSIC")
			fnv = 0;
		else if(filename == "SFX")
			fnv = 1;
		
		for (size_t i = 0; i < maskcount; i++)
		{
			if (masks[i].fnvhash == fnv) {
				found = true;
				masks[i].fnvstring = filename;
				break;
			}
		}

		if (!found) {
			printf("ERROR: snd archive with no container mask %s\n", filename.data());
		}
	}
}

bool akmetadata::Build(fnvmap_t& fnvmap, const char* metastart, const size_t metalength)
{
	uint32_t total = 0, stringlength = 0, fnv = 0;
	const char* string = nullptr;
	BinaryReader reader(metastart, metalength);

	// The SoundEvents section (Section #1) of soundmetadata.bin provides a complete
	// mapping of soundbank fnv hashes to strings
	assert(reader.ReadLE(total));
	for (uint32_t i = 0; i < total; i++) {
		assert(reader.ReadLE(stringlength));
		assert(reader.ReadBytes(string, stringlength));
		assert(reader.ReadLE(fnv));

		assert(fnv == HashLib::akfnv_insensitive(string, stringlength));
		fnvmap[fnv] = std::string(string, stringlength);

		assert(reader.GoRight(1));
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));
	}

	return true;
}

bool akpck::Build(LangMap_t& langmap, const char* pckstart, const size_t pcklength)
{
	BinaryReader reader(pckstart, pcklength);

	assert(reader.GoRight(28));
	BinaryReader blobreader(reader.GetNext(), reader.GetRemaining());

	uint32_t numlangs = 0, langoffset = 0, langid = 0;
	assert(reader.ReadLE(numlangs));
	for (uint32_t i = 0; i < numlangs; i++) {
		assert(reader.ReadLE(langoffset));
		assert(reader.ReadLE(langid));

		// Offset is relative to the start of the language chunk
		assert(blobreader.Goto(langoffset));

		// Language chunk uses wide strings
		// This jank converts them to normal strings
		uint16_t widechar;
		std::string langstring;

		while (blobreader.ReadLE(widechar) && widechar) {
			langstring.push_back(static_cast<char>(widechar));
		}

		langmap[langid] = langstring;
	}
	return true;
}

bool akpck::Build(EntryList_t& entries, const char* pckstart, const size_t pcklength)
{
	BinaryReader reader(pckstart, pcklength);

	uint32_t langchunksize = 0;
	assert(reader.GoRight(12));
	assert(reader.ReadLE(langchunksize));
	assert(reader.GoRight(12 + langchunksize));

	uint32_t numentries = 0;
	assert(reader.ReadLE(numentries));
	entries.resize(numentries);

	const size_t bytesize = numentries * sizeof(entry);
	assert(reader.GetRemaining() >= bytesize);

	memcpy(entries.data(), reader.GetNext(), bytesize);

	return true;
}

// Read a string followed by it's fnv hash
void __forceinline sndNameHash(BinaryReader& r) {
	u32 length;
	r >> length;
	r.GoRight(length);
	r >> length;
}

void sndNameHashList(BinaryReader& r) {
	u32 listlength;
	r >> listlength;
	for(u32 i = 0; i < listlength; i++)
		sndNameHash(r);
}

// Read an fnv hash followed by it's string
void __forceinline sndHashName(BinaryReader& r) {
	u32 length;
	r >> length >> length;
	r.GoRight(length);
}

void sndHashNameList(BinaryReader& r) {
	u32 listlength;
	r >> listlength;
	for (u32 i = 0; i < listlength; i++)
		sndHashName(r);
}

void __forceinline sndString(BinaryReader& r) {
	u32 length;
	r >> length;
	r.GoRight(length);
}

bool sndMetaData2::Parse_ContainerMask(BinaryReader& r) {
	u32 listlength;
	ptr_maskStart = r.GetNext();
	check(r.ReadLE(listlength));
	for (u32 i = 0; i < listlength; i++) {
		u32 sublist;
		u32 numInts;

		sndString(r);
		r >> sublist;
		for (u32 k = 0; k < sublist; k++) {
			r >> numInts >> numInts;
			r.GoRight(numInts * sizeof(u32));
		}
	}
	ptr_maskEnd = r.GetNext();
}

bool sndMetaData2::Parse_Eternal(char* data, size_t length, bool StopAfterContainerMask)
{
	ptr_start = data;
	ptr_end = data + length;
	version = VERSION_IDTECH7;

	BinaryReader r(data, length);

	// Version Number
	check(r.check32(0x17));

	u32 listlength;

	// Section 1: PCK list
	sndNameHashList(r);

	// Section 2: Container Mask
	if(!Parse_ContainerMask(r))
		return false;
	if(StopAfterContainerMask)
		return true;

	// Section 3: Bank List (very small)
	sndNameHashList(r);

	// Section 4: Effects (smallish)
	sndHashNameList(r);

	// Section 5: Parameters
	sndHashNameList(r);

	// Section 6: Switch Groups
	// Section 7: Switch States
	for (u32 chunk = 0; chunk < 2; chunk++) {
		check(r.ReadLE(listlength));
		for (u32 i = 0; i < listlength; i++) {
			sndHashName(r);
			sndHashNameList(r);
		}
	}

	// Section 8: Event Path Nodes Strings
	// This is a block of C Strings with no length fields
	check(r.ReadLE(listlength));
	r.GoRight(listlength);

	// Section 9: Sound Events
	check(r.ReadLE(listlength));
	for (u32 i = 0; i < listlength; i++) {
		
		u32 sublist;
		u32 word;
		u16 half;
		u8 byte;

		sndHashName(r);
		r >> word >> half >> word >> word;

		// Sublists: Sound IDs, Sound Bank IDs, PathNodeIds
		for (u32 k = 0; k < 3; k++) {
			r >> sublist;
			r.GoRight(sublist * sizeof(u32));
		}
	}

	// Section 10: Unknown Integers
	check(r.ReadLE(listlength));
	r.GoRight(listlength * sizeof(u32));

	// "ATLANMOD" signature that indicates a modded metadata file
	if (!r.ReachedEOF()) {
		r.check32('ALTA');
		r.check32('DOMN');
	}
	return r.ReachedEOF();
}

bool sndMetaData2::Parse_DarkAges(char* data, size_t length)
{
	ptr_start = data;
	ptr_end = data + length;

	version = VERSION_IDTECH8;

	BinaryReader r(data, length);
	
	u32 listlength;
	u8 byte;

	// Section 1: Sound Events
	check(r.ReadLE(listlength));
	for (u32 i = 0; i < listlength; i++) {
		sndNameHash(r); // Event name
		r >> byte;      // Language ID
		sndString(r);   // Language String
	}

	// Section 2: Unknown
	sndHashNameList(r);

	// Section 3: Unknown
	sndNameHashList(r);

	// Section 4: Sound Switches
	// Section 5: Sound States
	for (u32 chunk = 0; chunk < 2; chunk++) {
		check(r.ReadLE(listlength));
		u32 sublist;

		for (u32 i = 0; i < listlength; i++) {
			sndHashName(r);
			sndHashNameList(r);
		}
	}

	// Section 6: Bnk Sample Lists
	ptr_darkages_section6 = r.GetNext();
	check(r.ReadLE(listlength));
	for (u32 i = 0; i < listlength; i++) {

		u32 sublist;
		u32 word;
		u8 flag0;

		sndNameHash(r);
		r >> byte >> word >> flag0 >> byte >> word;

		if (!flag0) {
			r >> sublist;
			for (u32 k = 0; k < sublist; k++) {
				sndString(r);
				r >> word; // Float playbacktime
			}
		}

		r >> sublist;
		for (u32 k = 0; k < sublist; k++) {
			r >> word;
			sndString(r);
		}
	}
	ptr_darkages_section6_end = r.GetNext();

	// Section 7: Container Mask
	if(!Parse_ContainerMask(r))
		return false;

	// "ATLANMOD" signature that indicates a modded metadata file
	if (!r.ReachedEOF()) {
		r.check32('ALTA'); 
		r.check32('DOMN');
	}
	return r.ReachedEOF();
}

bool sndMetaData2::Parse(char* data, size_t length, bool StopAfterContainerMask)
{
	u32 first = *(u32*)data;

	if (first & 0xFFFFFF00) {
		return Parse_DarkAges(data, length);
	}
	return Parse_Eternal(data, length, StopAfterContainerMask);
}
