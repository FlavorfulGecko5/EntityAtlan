#pragma once

typedef unsigned int u32;
typedef unsigned long long u64;

#define STREAMDB_MAGIC 0x61C7F32E29C2A550UL

struct idStreamDB {

    enum headerflags_t {
        SDHF_NO_GUID = 1,
        SDHF_HAS_PREFETCH_BLOCKS = 2
    };

    struct header_t {
        u64 magic;
        u32 headerLength;
        u32 pad0;
        u32 pad1;
        u32 pad2;
        u32 numEntries;
        headerflags_t flags;
        //Assert(magic == 0x61C7F32E29C2A550UL);
        //Assert(pad0 == 0);
        //Assert(pad1 == 0);
        //Assert(pad2 == 0);
    };

    struct entry_t {
        u64 id;
        u32 offset16; // Multiply by 16 to get the entry's offset in the file
        u32 length;
    };

    struct prefetchheader_t {
        u32 numblocks;
        u32 totalLength; // Combined length of prefetch header, block, entries
    };

    struct prefetchblock_t {
        u64 farmhash; // Hash of "AI" or "FirstPerson"
        u32 firstItemIndex; // Offset relative to end of prefetch blocks
        u32 numItems;
    };

    header_t header;
    entry_t* entries = nullptr; // Length == header.numEntries
    prefetchheader_t prefetchheader; // Always included
    prefetchblock_t* prefetchblocks = nullptr; // Optional. Length == prefetchheader.numBlocks
    u64* prefetchIds = nullptr; // Entry IDs. Length == sum prefetchblocks[i].numItems

    size_t num_prefetch_ids = 0;

    size_t TOTAL_FILE_SIZE = 0;

    ~idStreamDB() {
        delete[] entries;
        delete[] prefetchblocks;
        delete[] prefetchIds;
    }


    bool Read(const wchar_t* filepath);
};