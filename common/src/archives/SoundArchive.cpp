#include "SoundArchive.h"
#include "io/BinaryReader.h"
#include <fstream>
#include <string>
#include <cassert>

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
	assert(e.encodedSize == e.decodedSize);
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

void AudioSampleMap::Build(std::string soundfolder)
{
	// Step 1: Build bnk_eventstring_map
	{
		BinaryOpener open(soundfolder + "/soundmetadata.bin");
		BinaryReader reader = open.ToReader();

		uint8_t byte = 0;
		uint32_t numevents = 0;
		uint32_t stringlength = 0;
		uint32_t bnkid = 0;
		const char* bytes = nullptr;
		assert(reader.ReadLE(numevents));

		for (uint32_t i = 0; i < numevents; i++) {
			assert(reader.ReadLE(stringlength));
			assert(reader.ReadBytes(bytes, stringlength));
			assert(reader.ReadLE(bnkid));

			assert(bnk_eventstring_map.find(bnkid) == bnk_eventstring_map.end());
			bnk_eventstring_map[bnkid] = std::string(bytes, stringlength);
			//std::cout << std::string(bytes, stringlength) << "\n";

			assert(reader.ReadLE(byte));
			assert(reader.ReadLE(stringlength));
			assert(reader.GoRight(stringlength));
		}
	}

	duplicateLog = "Some audio samples are used in multiple sound events.\n"
	"This file logs all duplicate usages of a single audio sample\n\n";

	std::unordered_map<uint32_t, std::vector<uint32_t>> duplicatelists; // Dynamic STL happy fun time!!!!
	duplicatelists.reserve(250);

	// Step 2: Build sample_bnk_idmap
	{
		BinaryOpener open(soundfolder + "/Titan.pck");
		BinaryReader pck(open.ToReader());

		uint32_t sec1_size, sec2_size;
		assert(pck.GoRight(0xC));
		assert(pck.ReadLE(sec1_size));
		assert(pck.ReadLE(sec2_size));
		assert(pck.GoRight(sec1_size + 8));

		uint32_t numbanks;
		assert(pck.ReadLE(numbanks));

		for(uint32_t bnkindex = 0; bnkindex < numbanks; bnkindex++)
		{
			uint32_t bnkid = 0, bnksize = 0, bnkoffset = 0;
			assert(pck.ReadLE(bnkid));
			assert(pck.GoRight(4));
			assert(pck.ReadLE(bnksize));
			assert(pck.ReadLE(bnkoffset));
			assert(pck.GoRight(4));
			assert(bnk_eventstring_map.find(bnkid) != bnk_eventstring_map.end());


			BinaryReader bnk(pck.GetBuffer() + bnkoffset, bnksize);
			assert(memcmp("BKHD", bnk.GetBuffer(), 4) == 0);
			
			uint32_t chunksize = 0;
			while(memcmp(bnk.GetNext(), "HIRC", 4) != 0) {
				assert(bnk.GoRight(4));
				assert(bnk.ReadLE(chunksize));
				assert(bnk.GoRight(chunksize));
			}

			assert(bnk.GoRight(4));
			assert(bnk.ReadLE(chunksize));
			
			uint32_t numhirc = 0;
			assert(bnk.ReadLE(numhirc));

			for(uint32_t hircindex = 0; hircindex < numhirc; hircindex++)
			{
				uint8_t hirctype;
				uint32_t hircsize;
				assert(bnk.ReadLE(hirctype));
				assert(bnk.ReadLE(hircsize));

				BinaryReader hirc(bnk.GetNext(), hircsize);
				assert(bnk.GoRight(hircsize));

				if(hirctype != 0x02) // CAkSound
					continue;

				uint32_t sampleid = 0;
				assert(hirc.GoRight(9));
				assert(hirc.ReadLE(sampleid));

				
				// NOT MUTUALLY EXCLUSIVE: The same sample can be found in multiple banks, and one bank can have multiple samples
				if(sample_bnk_idmap.find(sampleid) != sample_bnk_idmap.end()) {
					// Must account for a sound being used multiple times in the same bank
					bool found = false;
					for (uint32_t bid : duplicatelists[sampleid]) {
						if (bid == bnkid) {
							found = true;
							break;
						}
					}

					if (!found) {
						duplicate_sample_usages++;
						duplicatelists[sampleid].push_back(bnkid);
					}
					//printf("%d - %s AND %s \n", sampleid, bnk_eventstring_map[bnkid].c_str(), bnk_eventstring_map[sample_bnk_idmap[sampleid]].c_str());
				}
				else {
					sample_bnk_idmap[sampleid] = bnkid;
				}
			}
		}
	}

	// Step 3: Write out the duplicate log
	for (const auto& pair : duplicatelists) {
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
	containermask.Build(soundfolder);
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

void sndContainerMask::Build(const std::string soundfolder)
{
	BinaryOpener open(soundfolder + "/soundmetadata.bin");
	BinaryReader reader = open.ToReader();

	// We have to parse through the entirety of soundmetadata.bin to reach
	// the container mask section

	uint8_t byte;
	uint32_t numevents = 0;
	uint32_t stringlength = 0;

	// Sound Event list
	assert(reader.ReadLE(numevents));
	for (uint32_t i = 0; i < numevents; i++) {
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));
		assert(reader.GoRight(4)); // bnk id
		assert(reader.ReadLE(byte)); // lang id
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));
	}

	// Unknown
	assert(reader.ReadLE(numevents));
	for (uint32_t i = 0; i < numevents; i++) {
		assert(reader.GoRight(4)); // Some sort of id
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));
	}

	// Unknown
	assert(reader.ReadLE(numevents));
	for (uint32_t i = 0; i < numevents; i++) {
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));
		assert(reader.GoRight(4)); // Some sort of id
	}

	// Music Switches?
	assert(reader.ReadLE(numevents));
	for (uint32_t i = 0; i < numevents; i++) {
		assert(reader.GoRight(4));
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));

		uint32_t listlength = 0;
		assert(reader.ReadLE(listlength));

		for (uint32_t subindex = 0; subindex < listlength; subindex++)
		{
			assert(reader.GoRight(4));
			assert(reader.ReadLE(stringlength));
			assert(reader.GoRight(stringlength));
		}
	}

	// Music States?
	assert(reader.ReadLE(numevents));
	for (uint32_t i = 0; i < numevents; i++) {
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

	// Unknown
	assert(reader.ReadLE(numevents));
	for (uint32_t i = 0; i < numevents; i++) {
		assert(reader.ReadLE(stringlength));
		assert(reader.GoRight(stringlength));
		assert(reader.GoRight(4));

		assert(reader.GoRight(11)); // No idea what this is 
		uint32_t listlength = 0;
		assert(reader.ReadLE(listlength));

		// This is hopefully a stable way of determining this.
		// Really need to fully understand the soundmetadata
		bool isLanguageList;
		{
			size_t position = reader.GetPosition();

			const char* bytes = nullptr;
			uint32_t testlen = 0;
			assert(reader.ReadLE(testlen));

			if (testlen == 11) {
				assert(reader.ReadBytes(bytes, testlen));
				isLanguageList = memcmp(bytes, "English(US)", 11) == 0;
			}
			else {
				isLanguageList = false;
			}

			reader.Goto(position);
		}

		if (isLanguageList) { // Big language list
			for (uint32_t langind = 0; langind < listlength; langind++) {
				if (langind > 0)
					assert(reader.GoRight(4));
				assert(reader.ReadLE(stringlength));
				assert(reader.GoRight(stringlength));
			}

			assert(reader.GoRight(4));
			assert(reader.ReadLE(listlength));
			for (uint32_t langind = 0; langind < listlength; langind++) {
				assert(reader.GoRight(4));
				assert(reader.ReadLE(stringlength));
				assert(reader.GoRight(stringlength));
			}
		}
		else {
			for (uint32_t langind = 0; langind < listlength; langind++) { // List of SFX
				assert(reader.GoRight(4));
				assert(reader.ReadLE(stringlength));
				assert(reader.GoRight(stringlength));
			}
		}
	}

	// This is the container mask
	this->rawsize = reader.GetRemaining();
	rawdata = new char[rawsize];
	memcpy(rawdata, reader.GetNext(), rawsize);
	BinaryReader mask(rawdata, rawsize);

	const char* groupname = nullptr;
	uint32_t groupnamelength = 0;

	assert(mask.ReadLE(numevents));
	for (uint32_t i = 0; i < numevents; i++) {
		assert(mask.ReadLE(groupnamelength));
		assert(mask.ReadBytes(groupname, groupnamelength));

		uint32_t numarchives; // Number of archives of this type
		assert(mask.ReadLE(numarchives));

		const std::string archivestem(groupname, groupnamelength - 4); // Cut off the .snd

		for (uint32_t arcindex = 0; arcindex < numarchives; arcindex++) {
			
			entry e;
			e.archiveName = archivestem;
			if (arcindex > 0) {
				e.archiveName.append("_patch_");
				e.archiveName.append(std::to_string(arcindex));
			}
			e.archiveName.append(".snd");


			assert(mask.GoRight(4)); // Container id?
			
			assert(mask.ReadLE(e.size));
			assert(mask.ReadBytes(e.mask, e.size * sizeof(uint32_t)));
			e.size *= 32; // Convert from integers to bits

			masks.push_back(e);
		}

	}

	assert(mask.ReachedEOF());
}
