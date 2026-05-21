#pragma once

typedef unsigned int u32;
typedef unsigned long long u64;

struct streamdb {

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
        u64 identity;
        u32 offset16;
        u32 length;
    };
};