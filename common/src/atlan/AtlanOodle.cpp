#include "AtlanOodle.h"
#include "AtlanLogger.h"
#include "entityslayer/Oodle.h"
#include "io/BinaryReader.h"
#include "hash/sha256.h"

typedef std::filesystem::path fspath;

bool Oodle::AtlanOodleInit(const std::filesystem::path& gamedirectory)
{
	using namespace std::filesystem;

	fspath oo2core_chosenpath;
	const fspath oo2corepath_debug = "oo2core_9_win64.dll";
	const fspath oo2corepath_alt   = gamedirectory / "oo2core_8_win64.dll";
	const fspath oo2corepath       = gamedirectory / "oo2core_9_win64.dll";

	/*
	* Search for a useable Oodle Core DLL to load
	* Download one if it doesn't exist
	* (Doom Eternal ships with core8, so we can avoid downloading core9 entirely)
	*/
	if (exists(oo2corepath_alt)) {
		oo2core_chosenpath = oo2corepath_alt;
	}
	else if (exists(oo2corepath_debug)) {
		oo2core_chosenpath = oo2corepath_debug;
	}
	else {
		if (!exists(oo2corepath)) {
			// Because nobody ever needed a simple STL function to convert a string to a wide string....
			#define OODLE_URL    "https://github.com/WorkingRobot/OodleUE/raw/206301de7e80dd41ee873fe25126774be0fa4608/Engine/Source/Programs/Shared/EpicGames.Oodle/Sdk/2.9.10/win/redist/oo2core_9_win64.dll"
			#define OODLE_URL_W L"https://github.com/WorkingRobot/OodleUE/raw/206301de7e80dd41ee873fe25126774be0fa4608/Engine/Source/Programs/Shared/EpicGames.Oodle/Sdk/2.9.10/win/redist/oo2core_9_win64.dll"
			atlog << "Downloading " << oo2corepath << " from " << OODLE_URL << "\n";

			bool success = Oodle::Download(OODLE_URL_W, oo2corepath.wstring().c_str());
			if (!success) {
				atlog << "FATAL ERROR: Failed to download " << oo2corepath << "\n";
				return false;
			}

			// Verify dll hash
			const BYTE EXPECTED[SHA256_BLOCK_SIZE] = {
				0x6f, 0x5d, 0x41, 0xa7, 0x89, 0x2e, 0xa6, 0xb2,
				0xdb, 0x42, 0x0f, 0x24, 0x58, 0xda, 0xd2, 0xf8,
				0x4a, 0x63, 0x90, 0x1c, 0x9a, 0x93, 0xce, 0x94,
				0x97, 0x33, 0x7b, 0x16, 0xc1, 0x95, 0xf4, 0x57
			};

			BYTE hash[SHA256_BLOCK_SIZE];

			BinaryOpener open(oo2corepath.string());

			SHA256_CTX ctx;
			sha256_init(&ctx);
			sha256_update(&ctx, (BYTE*)open.data(), open.len());
			sha256_final(&ctx, hash);

			if (memcmp(hash, EXPECTED, SHA256_BLOCK_SIZE) != 0) {
				atlog << "FATAL ERROR: " << oo2corepath << " checksum failed. Removing dll\n";
				std::filesystem::remove(oo2corepath);
				return false;
			}
			atlog << "Download Complete (Oodle is a file decompression library)\n";
		}
		oo2core_chosenpath = oo2corepath;
	}

	if (!Oodle::init(oo2core_chosenpath.string().c_str())) {
		atlog << "FATAL ERROR: Failed to initialize " << oo2core_chosenpath << "\n";
		return false;
	}

	return true;
}

/*
* ATLAN COMPRESSION FILE FORMAT:
* - Magic: "ATCF" (Atlan Compression File)
* - 8 Bytes: Decompressed Size
* - 8 Bytes: Compressed Size
*/

int Oodle::AtlanCompHeaderSize()
{
	return 4 * sizeof(char) + 2 * sizeof(size_t);
}

size_t Oodle::atcf_uncompressedSize(const char* input)
{
	return *(const size_t*)(input + 4);
}

const char* Oodle::atcf_dataptr(const char* input)
{
	return input + AtlanCompHeaderSize();
}

bool Oodle::AtlanCompress(const char* input, size_t inputlength, char*& output, size_t& outputlength, size_t& outputBufferLength)
{
	size_t targetSize = inputlength + 65000;

	if (outputBufferLength < targetSize) {
		delete[] output;
		output = new char[targetSize];
		outputBufferLength = targetSize;
	}

	size_t compressedSize; // Const cast is probably fine here...todo: inspect Oodle function signatures
	bool result = Oodle::CompressBuffer(const_cast<char*>(input), inputlength, output + AtlanCompHeaderSize(), compressedSize);
	if(!result)
		return false;

	outputlength = AtlanCompHeaderSize() + compressedSize;

	memcpy(output, "ATCF", 4);
	*(size_t*)(output + 4) = inputlength;
	*(size_t*)(output + 12) = compressedSize;

	return true;
}

bool Oodle::IsAtlanCompFile(const char* input, size_t inputlength)
{
	if(inputlength < AtlanCompHeaderSize())
		return false;

	if(memcmp(input, "ATCF", 4) != 0)
		return false;

	size_t compressedSize = *(size_t*)(input + 12);
	if(inputlength - AtlanCompHeaderSize() != compressedSize)
		return false;

	return true;
}
