#pragma once

#include "unordered_map"

typedef unsigned long long uint64_t;

struct checksumpair {
	uint64_t v0;

	// Only needed because of renderProgResource resources
	// One archive can contain multiple of these resources
	// with the same name, but different variation codes.
	// The empty map's gameresources archive has 2 copies of each
	// renderProgResource
	uint64_t v1;
};

// Map of HashLib::DeclHash(typestring, namestring) resource hashes
// to their data checksums
typedef std::unordered_map<uint64_t, checksumpair> AssetChecksumMap;

// When packaging an empty idStudio level, these are all the assets
// that get put into the warehouse archive
extern const AssetChecksumMap EMPTYMAP_WAREHOUSE;

// When packaging an empty idStudio level, these are all the assets
// that get put into the gameresources archive
extern const AssetChecksumMap EMPTYMAP_GAMERESOURCES;