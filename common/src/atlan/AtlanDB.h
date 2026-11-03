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

	
	static bool FromStaticModel(const char* bmodel, const size_t bmodel_length, const char* sfile, const size_t sfile_length, charbuffer_t& output);

	static bool FromBaseModel(const char* mesh, const size_t mesh_length, const char* sfile, const size_t sfile_length, charbuffer_t& output);

	static bool FromGeneric_NoEntries(const char* res, const size_t res_length, const u32 resversion, charbuffer_t& output);

};