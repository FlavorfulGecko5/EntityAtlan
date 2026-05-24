#include "StreamDB.h"
#include <fstream>
#include <cassert>

bool idStreamDB::Read(const wchar_t* filepath)
{

    std::ifstream reader(filepath, std::ios_base::binary);
    reader.seekg(0, std::ios_base::end);
    TOTAL_FILE_SIZE = reader.tellg();
    reader.seekg(0, std::ios_base::beg);
    
    // Header
    reader.read((char*)&header, sizeof(header_t));
    if ( header.magic != STREAMDB_MAGIC
        || header.pad0 != 0
        || header.pad1 != 0
        || header.pad2 != 0
        || header.flags != 3
    ) {
        return false;
    }

    // Entries
    entries = new entry_t[header.numEntries];
    reader.read((char*)entries, sizeof(entry_t) * header.numEntries);


    // Prefetch Header
    reader.read((char*)&prefetchheader, sizeof(prefetchheader_t));

    if (prefetchheader.numblocks > 0) {
        // Prefetch Blocks
        prefetchblocks = new prefetchblock_t[prefetchheader.numblocks];
        reader.read((char*)prefetchblocks, sizeof(prefetchblock_t) * prefetchheader.numblocks);

        // Prefetch Ids
        for (uint32_t i = 0; i < prefetchheader.numblocks; i++) {
            num_prefetch_ids += prefetchblocks[i].numItems;
        }
        prefetchIds = new u64[num_prefetch_ids];
        reader.read((char*)prefetchIds, num_prefetch_ids * sizeof(u64));

        // Eternal: Maximum of 2 prefetch blocks. Identifiers are "AI" or "FirstPerson"
        // DarkAges: Maximum of 1 prefetch block. Identifier is always "AI"
        assert(prefetchheader.numblocks < 3);
    }

    // Some final validations
    if (prefetchheader.totalLength !=
        sizeof(prefetchheader_t)
        + sizeof(prefetchblock_t) * prefetchheader.numblocks
        + sizeof(prefetchIds) * num_prefetch_ids
        ) {
        return false;
    }

    size_t s = reader.tellg();
    if(reader.tellg() != header.headerLength)
        return false;

    // Entry data is not necessary stored in the order the entries appear in
    // So we can't reliably audit for a running total like this
    #if 0
    size_t runningtotal = header.headerLength;
    for (uint32_t i = 0; i < header.numEntries; i++) {
        size_t realLocation = entries[i].offset16 * 16;
        assert(realLocation == runningtotal);
        runningtotal += entries[i].length + entries[i].length % 16;
    }
    #endif


    return true;
}
