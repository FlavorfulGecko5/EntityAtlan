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

// An empty map gets packaged with ~400 UI image assets. These images
// comprise the entirety of the empty map's ~15 megabyte streamdb file.
// 
// This implies these are are level-specific resources, packaged into every 
// vanilla level in the game. However, these don't normally get culled, because
// their checksums don't match their vanilla counterparts. For whatever reason,
// idStudio regenerates them differently. But this shouldn't ultimately matter.
// We can still cull them, using this list to identify them
// 
// In this map, v1 is the defaulthash, NOT a second checksum
extern const AssetChecksumMap EMPTYMAP_STREAMDB_FILES;