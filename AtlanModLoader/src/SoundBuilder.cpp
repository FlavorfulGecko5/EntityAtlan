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

	atlog << "WARNING: Failed to measure metadata size for an audio sample. Is the sample correctly encoded?\n";
	return 0;
}

#define rc(VAR) reinterpret_cast<char*>(&VAR)

void BuildSoundMetadata(const fspath soundsfolder, const uint32_t numSamples)
{
	BinaryOpener file((soundsfolder / "soundmetadata.bin.backup").string());
	assert(file.Okay());

	const char* data = file.data();
	size_t length = file.len();
	size_t maskindex = sndMetadata::FindContainerMask(data, length);

	std::ofstream outwriter(soundsfolder / "soundmetadata.bin", std::ios_base::binary);

	// Write everything preceding the container mask
	outwriter.write(data, maskindex);

	// Increment the number of mask groups
	uint32_t numgroups = *reinterpret_cast<const uint32_t*>(data + maskindex);
	numgroups++;
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
	outwriter.write(data + maskindex + 4, length - (maskindex + 4));

	// Write some magic at the end of the file to easily detect if this file is modded or not
	outwriter.write("ATLANMOD", 8);
	outwriter.close();
}

void ModBuilder::BuildAudioArchives(const fspath soundsfolder, const std::vector<ModFile*>& samplefiles)
{
	const uint32_t samplecount = static_cast<uint32_t>(samplefiles.size());

	BinaryWriter headerchunk(1000000);
	headerchunk << 6;
	headerchunk.pushSizeStack();
	headerchunk.pushSizeStack();

	aksnd::entry* const entries = new aksnd::entry[samplecount];
	aksnd::entry* eptr = entries;

	for (const ModFile* sample : samplefiles)
	{

		// Find Sample ID
		{
			bool result = StringToID(sample->assetPath.data(), sample->assetPath.length(), eptr->id);

			if (!result) {
				atlog << "ERROR: Failed to parse audio sample ID from '" << sample->assetPath << "'\n";
			}

			#ifdef _DEBUG
			printf("%u\n", eptr->id);
			#endif
		}

		const char* samplebuffer = (char*)sample->dataBuffer;
		const size_t samplelen = sample->dataLength;

		eptr->farmhash = HashLib::FarmHash64(samplebuffer, samplelen); // TODO: Test if necessary
		eptr->encodedSize = samplelen;
		eptr->decodedSize = samplelen;
		eptr->metaoffset = headerchunk.GetPosition() - 0x0C;
		eptr->metasize = GetSampleMetaSize(samplebuffer, samplelen);

		// TODO: Test if necessary
		headerchunk.WriteBytes(samplebuffer, eptr->metasize);

		*eptr++;
	}

	// After the metadata, we write the number of samples in the archive
	headerchunk << samplecount;
	headerchunk.popSizeStack();


	// Calculate the offset of each sample's entry in the archive
	uint32_t runningoffset = headerchunk.GetPosition() + samplecount * sizeof(aksnd::entry);
	eptr = entries;
	for (uint32_t i = 0; i < samplecount; i++) {
		eptr->offset = runningoffset;
		runningoffset += eptr->encodedSize;
		eptr++;
	}

	// Write the entries and header chunk length
	headerchunk.WriteBytes(reinterpret_cast<char*>(entries), samplecount * sizeof(aksnd::entry));
	headerchunk.popSizeStack();

	// Write to disk
	std::ofstream outwriter(soundsfolder / "ATLANMOD.snd", std::ios_base::binary);
	outwriter.write(headerchunk.GetBuffer(), headerchunk.GetFilledSize());
	for (const ModFile* f : samplefiles) {
		outwriter.write((char*) f->dataBuffer, f->dataLength);
	}
	outwriter.close();
	delete[] entries;

	BuildSoundMetadata(soundsfolder, samplecount);
}