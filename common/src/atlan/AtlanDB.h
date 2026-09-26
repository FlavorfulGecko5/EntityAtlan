#pragma once

#include "atlan/AtlanLib.h"

struct AtlanDBFile {

	struct entry {
		u64 da_hashparm1;
		u64 da_hashparm2;
		u64 en_hashparm;
		u64 reserved[4];
		u32 offset;
		u32 size;
	};
	
	u32 dbversion;
	u32 resourceVersion;
	u32 numEntries;
	u64 resourceSize;
	u64 resourceSize_uncompressed;

	const entry* entries;
	const char* resourceEntry;


	bool ReadFrom(const char* data, size_t length);

};