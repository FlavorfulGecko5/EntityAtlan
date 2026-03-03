#pragma once
#include <string_view>

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

namespace HashLib {
	uint64_t FarmHash64(const char* data, size_t length);
	uint64_t FingerPrint(uint64_t hi, uint64_t lo);
	uint64_t DeclHash(std::string_view type, std::string_view name);
	uint64_t ResourceMurmurHash(const char* data, size_t length);

	// idStr hashing algorithm
	// Used for typeinfo class name hashes, eventcall string hashes,
	// and probably various other things
	uint32_t idHashIndex(const char* string, size_t length);

	// AudioKinetic's Case-Insensitive FNV Hash algorithm
	// Used on the filenames of snd archives
	uint32_t akfnv_insensitive(const char* string, size_t length);
}