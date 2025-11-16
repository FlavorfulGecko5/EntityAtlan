#pragma once
#include <filesystem>

namespace Oodle
{
	// Performs all necessary operations for initializing Oodle for an Atlan application
	// Will download Oodle if no valid dll can be found. Results are logged appropriately
	// Returns true if successful. A return value of false indicates the program should be terminated
	bool AtlanOodleInit(const std::filesystem::path& gamedirectory);

	// Create an Atlan Compression File from the given data
	// Output is written to the provided buffer. If the output buffer is too small, the buffer will be reallocated
	bool AtlanCompress(const char* input, size_t inputlength, char*& output, size_t& outputlength, size_t& outputBufferLength);

	// Returns true if this is a valid Atlan Compression File
	bool IsAtlanCompFile(const char* input, size_t inputlength);

	// Returns the size of the Atlan Compression File header that precedes the real data
	int AtlanCompHeaderSize();

	// Returns the uncompressed size of the given Atlan Compressed File's data
	size_t atcf_uncompressedSize(const char* input);

	// Returns a pointer to the data of the given Atlan Compressed File
	const char* atcf_dataptr(const char* input);
}
