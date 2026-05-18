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

aksnd::samplehash aksnd::GetSampleHash(const aksnd::entry& e)
{
	BinaryReader reader(entrymeta + e.metaoffset, e.metasize);

	uint32_t chunksize;
	assert(reader.GoRight(16)); // RIFF + Total chunk size + "WAVEfmt "
	assert(reader.ReadLE(chunksize));
	assert(reader.GoRight(chunksize));

	assert(memcmp(reader.GetNext(), "hash", 4) == 0);
	assert(reader.GoRight(4));
	assert(reader.ReadLE(chunksize));
	assert(chunksize == 16);

	samplehash hash;
	assert(reader.ReadLE(hash.lower));
	assert(reader.ReadLE(hash.upper));

	return hash;
}

size_t SoundMetaData_FindSection6(const char* soundmetadata, size_t soundmetadata_length) {
	uint32_t total = 0, fnv = 0, stringlength = 0;
	const char* string = nullptr;

	BinaryReader reader(soundmetadata, soundmetadata_length);

	// Sound Events
	assert(reader.ReadLE(total));
	for (uint32_t i = 0; i < total; i++) {
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength + 4 + 1)); // fnv hash + language id
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));
	}

	// Section 2
	assert(reader.ReadLE(total));
	for (uint32_t i = 0; i < total; i++) {
		assert(reader.GoRight(4));
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));
	}

	// Section 3
	assert(reader.ReadLE(total));
	for (uint32_t i = 0; i < total; i++) {
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength + 4));
	}

	// Section 4 and 5
	for (int CHUNK = 0; CHUNK < 2; CHUNK++) {
		assert(reader.ReadLE(total));
		for (uint32_t i = 0; i < total; i++) {
			assert(reader.GoRight(4));
			assert(reader.ReadLE(stringlength));
			assert(reader.GoRight(stringlength));

			uint32_t listlength;
			assert(reader.ReadLE(listlength));

			for (uint32_t subindex = 0; subindex < listlength; subindex++) {
				assert(reader.GoRight(4));
				assert(reader.ReadLE(stringlength));
				assert(reader.GoRight(stringlength));
			}
		}
	}

	return reader.GetPosition();
}

void AudioSampleMap::Build_V2(std::string soundfolder)
{
	BinaryOpener open(soundfolder + "/soundmetadata.bin");
	BinaryReader reader = open.ToReader();

	// Section 6 of the sound meta data maps sample IDs to strings
	const size_t SEC6_POSITION = SoundMetaData_FindSection6(reader.GetBuffer(), reader.GetLength());
	reader.Goto(SEC6_POSITION);

	duplicateLog = "Some audio samples are used in multiple sound events.\n"
		"This file logs all duplicate usages of a single audio sample\n\n";

	// Step 2: Build out the sample map
	std::unordered_map<uint32_t, std::set<uint32_t>> duplicatesets; // Dynamic STL happy fun time!!!!
	duplicatesets.reserve(4000);
	{
		uint32_t total = 0, stringlength = 0, bnkfnv = 0;
		const char* string = nullptr;

		assert(reader.ReadLE(total));
		for (uint32_t i = 0; i < total; i++) {
			assert(reader.ReadLE(stringlength));
			assert(reader.ReadBytes(string, stringlength));
			assert(reader.ReadLE(bnkfnv));

			assert(bnkfnv == HashLib::akfnv_insensitive(string, stringlength));
			assert(bnk_eventstring_map.find(bnkfnv) == bnk_eventstring_map.end());
			bnk_eventstring_map[bnkfnv] = std::string(string, stringlength);

			uint8_t extralistflag = 0;
			uint32_t listlength = 0;
			assert(reader.GoRight(5));
			assert(reader.ReadLE(extralistflag));
			assert(reader.GoRight(5));
			assert(reader.ReadLE(listlength));

			if (extralistflag == 0) {
				for (uint32_t listind = 0; listind < listlength; listind++) {
					assert(reader.ReadLE(stringlength));
					assert(reader.GoRight(stringlength + 4));
				}
				assert(reader.ReadLE(listlength));
			}

			for (uint32_t sampleindex = 0; sampleindex < listlength; sampleindex++) {
				uint32_t sampleid = 0;

				assert(reader.ReadLE(sampleid));
				assert(reader.ReadLE(stringlength));
				assert(reader.GoRight(stringlength)); // Language string, not the sample name

				if (sample_bnk_idmap.find(sampleid) != sample_bnk_idmap.end()) {
					duplicate_sample_usages++;
					duplicatesets[sampleid].insert(bnkfnv);
				}
				else {
					sample_bnk_idmap[sampleid] = bnkfnv;
				}
			}
		}
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
	containermask.Build(reader.GetNext(), reader.GetRemaining(), soundfolder);
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

size_t sndMetadata::FindContainerMask(const char* metastart, const size_t metalength)
{
	// We have to parse through the entirety of soundmetadata.bin to reach
	// the container mask section

	BinaryReader reader(metastart, metalength);

	uint8_t byte;
	uint32_t numevents = 0;
	uint32_t stringlength = 0;

	reader.Goto(SoundMetaData_FindSection6(metastart, metalength));

	// Skip Section 6
	assert(reader.ReadLE(numevents));
	for (uint32_t i = 0; i < numevents; i++) {
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));
		assert(reader.GoRight(4));

		uint8_t extralistflag = 0;
		assert(reader.GoRight(5));
		assert(reader.ReadLE(extralistflag));
		assert(reader.GoRight(5));
		uint32_t listlength = 0;
		assert(reader.ReadLE(listlength));

		if (extralistflag == 0) {
			for (uint32_t langind = 0; langind < listlength; langind++) {
				assert(reader.ReadLE(stringlength));
				assert(reader.GoRight(stringlength + 4));
			}
			assert(reader.ReadLE(listlength));
		}

		for (uint32_t langind = 0; langind < listlength; langind++) {
			assert(reader.GoRight(4));
			assert(reader.ReadLE(stringlength));
			assert(reader.GoRight(stringlength));
		}
	}

	return reader.GetPosition();
}

void sndMetadata::Read(const std::string& soundfolder)
{
	std::ifstream file(soundfolder + "/soundmetadata.bin", std::ios_base::binary);
	assert(file.good());

	file.seekg(0, std::ios_base::end);
	filelength = file.tellg();
	rawfile = new char[filelength];
	file.seekg(0, std::ios_base::beg);
	file.read(rawfile, filelength);
	file.close();

	containermaskIndex = FindContainerMask(rawfile, filelength);
	ContainerMask.Build(rawfile + containermaskIndex, filelength - containermaskIndex, soundfolder);
}

void sndContainerMask::Build(const std::string soundfolder)
{
	BinaryOpener open(soundfolder + "/soundmetadata.bin");
	BinaryReader reader = open.ToReader();

	size_t maskstart = sndMetadata::FindContainerMask(reader.GetBuffer(), reader.GetLength());
	assert(reader.GoRight(maskstart));

	Build(reader.GetNext(), reader.GetRemaining(), soundfolder);
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
