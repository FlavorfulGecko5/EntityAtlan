#include "ModReader.h"
#include "archives/SoundArchive.h"
#include "atlan/AtlanLogger.h"
#include "io/BinaryWriter.h"
#include "io/BinaryReader.h"
#include "hash/HashLib.h"
#include <cassert>
#include <fstream>

template<typename T>
bool StringToID(const char* ptr, int len, T& writeTo) {
	if (len == 0)
		return false;

	const char* max = ptr + len;
	bool negative;
	if (*ptr == '-') {
		negative = true;
		ptr++;
	}
	else {
		negative = false;
	}

	T val = 0;
	while (ptr < max) {
		if (*ptr > '9' || *ptr < '0')
			return false;
		val = val * 10 + (*ptr - '0');
		ptr++;
	}

	writeTo = negative ? val * -1 : val;
	return true;
}

size_t GetSampleMetaSize(const char* data, size_t len)
{
	BinaryReader reader(data, len);

	const char* bytes = nullptr;
	uint32_t chunklength = 0;
	reader.GoRight(16); // Skip "RIFF<chunklength>WAVEFMT "

	// Skip WAVEFMT chunk
	reader.ReadLE(chunklength);
	reader.GoRight(chunklength); 

	// Meta terminates after the length of the data chunk
	while (reader.ReadBytes(bytes, 4)) {
		if (memcmp(bytes, "data", 4) == 0) {
			return reader.GetPosition() + 4;
		}
		else {
			reader.ReadLE(chunklength);
			reader.GoRight(chunklength);
		}
	}

	atlog("WARNING: Failed to measure metadata size for an audio sample. Is the sample correctly encoded?");
	return 0;
}

#define rc(VAR) reinterpret_cast<char*>(&VAR)

void BuildSoundMetadata(const fspath soundsfolder, const uint32_t numSamples)
{
	charbuffer_t rawfile;
	FileReader::ReadFile((soundsfolder / "soundmetadata.bin.backup").c_str(), rawfile);

	sndMetaData2 parsedmeta;
	if (!parsedmeta.Parse(rawfile.data, rawfile.length, true)) {
		atlog("ERROR: Failed to parse soundmetadata");
		return;
	}

	const size_t length_premask  = parsedmeta.ptr_maskStart - parsedmeta.ptr_start;
	const size_t length_mask     = parsedmeta.ptr_maskEnd - parsedmeta.ptr_maskStart;
	const size_t length_postmask = parsedmeta.ptr_end - parsedmeta.ptr_maskEnd;

	std::ofstream outwriter(soundsfolder / "soundmetadata.bin", std::ios_base::binary);

	// Write everything preceding the container mask
	outwriter.write(parsedmeta.ptr_start, length_premask);

	// Increment the number of mask groups
	uint32_t numgroups = 1 + *reinterpret_cast<const uint32_t*>(parsedmeta.ptr_maskStart);
	outwriter.write( rc(numgroups), sizeof(numgroups));


	// Write the AtlanMod Mask first
	// Priority is given to container masks that appear first
	// Format: String length of "ATLANMOD.SND", then this string
	// then the number of archives in this group (1), 
	// then the little-endian FNV-case-insensitive hash of "ATLANMOD"
	const char ATLANMASK[] = "\xC\x0\x0\x0" "ATLANMOD.snd" "\x1\x0\x0\x0" "\x91\xC8\xA9\x0C";
	outwriter.write(ATLANMASK, sizeof(ATLANMASK) - 1); // Don't include null-terminator

	// Write the bitmask
	uint32_t masksize_ints = numSamples / 32 + 1;
	outwriter.write(rc(masksize_ints), sizeof(masksize_ints));
	for (uint32_t i = 0; i < masksize_ints; i++) {
		outwriter.write("\xFF\xFF\xFF\xFF", 4);
	}

	// Write the rest of the vanilla container mask (resuming after the group count)
	outwriter.write(parsedmeta.ptr_maskStart + 4, length_mask - 4);

	// Write everything after the container mask
	outwriter.write(parsedmeta.ptr_maskEnd, length_postmask);

	// Write some magic at the end of the file to easily detect if this file is modded or not
	outwriter.write("ATLANMOD", 8);
	outwriter.close();
}

void ModBuilder::BuildAudioArchives(const fspath soundsfolder, const std::vector<ModFile*>& samplefiles, gamebit_t gameid)
{
	// To optimally support deferred loading of audio files, we assume that writing
	// the meta blob containing every wem's RIFF header chunks is unnecessary
	charbuffer_t DeferredBuffer;

	struct filestart_t {
		const u32 version = 6;
		u32 metasize;
		const u32 metablobsize = 4;
		u32 samplecount;
	} fileheader;

	fileheader.samplecount = static_cast<uint32_t>(samplefiles.size());
	fileheader.metasize = sizeof(fileheader.metablobsize) + sizeof(fileheader.samplecount) + sizeof(aksnd::entry) * fileheader.samplecount;

	u32 runningDataOffset =  sizeof(fileheader.version) + sizeof(fileheader.metasize) + fileheader.metasize;

	heaparray<aksnd::entry> entries;
	entries.EnsureCapacity(fileheader.samplecount);

	// We write the entries first, then the finalized meta chunk
	std::ofstream outwriter(soundsfolder / "ATLANMOD.snd", std::ios_base::binary);
	outwriter.seekp(runningDataOffset);

	for (u32 INDEX = 0; INDEX < fileheader.samplecount; INDEX++)
	{
		ModFile* sample = samplefiles[INDEX];
		aksnd::entry* eptr = entries.buffer + INDEX;

		if (!ModReader::LoadModData(*sample, DeferredBuffer)) {
			atlog("ERROR: Failed to load sample data for '%s'", sample->assetPath.c_str());
			continue;
		}

		// Find Sample ID
		{
			bool result = StringToID(sample->assetPath.data(), (int)sample->assetPath.length(), eptr->id);

			if (!result) {
				atlog("ERROR: Failed to parse audio sample ID from '%s'", sample->assetPath.c_str());
			}

			#ifdef _DEBUG
			printf("%u\n", eptr->id);
			#endif
		}

		const char* samplebuffer = sample->dataBuffer;
		const uint32_t samplelen = (uint32_t)sample->dataLength;

		if (samplelen < 4) {
			atlog("FATAL ERROR: Bad audio sample size");
			return;
		}
		if (*(u32*)samplebuffer != 'FFIR') {
			atlog("FATAL ERROR: Audio sample must be a .wem\n"
			"If these are .opus files from a legacy DOOM Eternal mod, please convert them to a PCM-encoded .wem");
			return;
		}

		// The meta chunk seems to only be used when samples are opus files
		eptr->farmhash = -1;
		eptr->encodedSize = samplelen;
		eptr->decodedSize = samplelen;
		eptr->metaoffset = 0;

		if (gameid == game_darkages) {
			eptr->metaunion.da_metasize = 0;
		}
		else {
			eptr->metaunion.de.encoding = 3;
			eptr->metaunion.de.metasize = 0;
		}
		
		eptr->offset = runningDataOffset;
		runningDataOffset += samplelen;
		outwriter.write(samplebuffer, samplelen);
	}

	// write to disk
	outwriter.seekp(0, std::ios_base::beg);
	outwriter.write((char*)&fileheader, sizeof(fileheader));
	outwriter.write((char*)entries.buffer, fileheader.samplecount * sizeof(aksnd::entry));
	outwriter.close();

	BuildSoundMetadata(soundsfolder, fileheader.samplecount);
}