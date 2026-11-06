#include "StreamDB.h"
#include "PackageMapSpec.h"
#include "entityslayer/Oodle.h"


bool idStreamDB::Read(const wchar_t* filepath, bool KeepAlive) {
    check(reader.open(filepath));

    bool result = Read_Internal();
    if (!KeepAlive) {
        reader.close();
    }
    return result && reader.noerrors();
}

bool idStreamDB::Read_Internal()
{
    TOTAL_FILE_SIZE = reader.getlength();
    
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
        check(prefetchheader.numblocks < 3);
    }

    // Some final validations
    if (prefetchheader.totalLength !=
        sizeof(prefetchheader_t)
        + sizeof(prefetchblock_t) * prefetchheader.numblocks
        + sizeof(prefetchIds) * num_prefetch_ids
        ) {
        return false;
    }

    if(reader.getposition() != header.headerLength)
        return false;

    // Entries are stored in order of ascending hash
    // (This is REQUIRED or the StreamDB will not work)
    for (u32 i = 1; i < header.numEntries; i++) {
        if(entries[i].id < entries[i - 1].id)
            return false;
    }

    return true;
}

idStreamDB::query idStreamDB::Find(const u64 targethash) {
    int low = 0, high = static_cast<int>(header.numEntries) - 1;

    while (low <= high) {
        int index = (high + low) / 2;

        if (entries[index].id < targethash) {
            low = index + 1;
        }
        else if (entries[index].id > targethash) {
            high = index - 1;
        }
        else {
            return {this, (u32)index};
        }
    }

    return {nullptr, 0};
}

void idStreamDB::GetData_Raw(u32 entryIndex, heaparray<char>& buffer) {
    const entry_t& e = entries[entryIndex];
    buffer.EnsureCapacity(e.length);

    u64 offset = e.offset16 * 16ULL;

    reader.seek(offset);
    reader.read(buffer.buffer, e.length);
}



bool idStreamDB_Database::Build(const wchar_t* gamedir)
{
    const fspath mapspec = fspath(gamedir) / "base/packagemapspec.json";

    std::vector<fspath> StreamDBNames = PackageMapSpec::GetStreamDBList(mapspec, false);

    check(StreamDBNames.size() > 0);

    this->dbptr = new idStreamDB[StreamDBNames.size()];

    for (const fspath& StreamName : StreamDBNames) {
        printf("%ls\n", StreamName.c_str());
        idStreamDB& ptr = dbptr[numdbs++];
        check(ptr.Read(StreamName.c_str(), true));
    }
}

bool idStreamDB_Database::GetData(const uint64_t targethash, const int DecompressedSize, charbuffer_t& outbuffer, charbuffer_t& decompbuffer) const
{
    for (int FILE_INDEX = 0; FILE_INDEX < numdbs; FILE_INDEX++) {
        idStreamDB& file = dbptr[FILE_INDEX];

        //const idStreamDB::entry_t *low = file.entries,
        //    *high = file.entries + file.header.numEntries - 1;

        int low = 0, high = file.header.numEntries - 1;

        bool found = false;
        idStreamDB::entry_t foundEntry;
        while (low <= high) {
            int index = (high + low) / 2;

            if (file.entries[index].id < targethash) {
                low = index + 1;
            }
            else if (file.entries[index].id > targethash) {
                high = index - 1;
            }
            else {
                found = true;
                foundEntry = file.entries[index];
                break;
            }
        }

        if(!found)
            continue;


        if (decompbuffer.capacity < foundEntry.length) {
            delete[] decompbuffer.data;
            decompbuffer.data = new char[foundEntry.length];
            decompbuffer.capacity = foundEntry.length;
        }
        if (outbuffer.capacity < DecompressedSize) {
            delete[] outbuffer.data;
            outbuffer.data = new char[DecompressedSize];
            outbuffer.capacity = DecompressedSize;
        }

        file.reader.seek(foundEntry.offset16 * 16LL);
        file.reader.read(decompbuffer.data, foundEntry.length);

        check(Oodle::DecompressBuffer(decompbuffer.data, foundEntry.length, outbuffer.data, DecompressedSize));

        return true;
    }

    return false;
}

bool idStreamDB_Database::FindHash(const uint64_t targethash) const
{
    for (int FILE_INDEX = 0; FILE_INDEX < numdbs; FILE_INDEX++) {
        idStreamDB& file = dbptr[FILE_INDEX];

        //const idStreamDB::entry_t *low = file.entries,
        //    *high = file.entries + file.header.numEntries - 1;

        int low = 0, high = file.header.numEntries - 1;

        bool found = false;
        idStreamDB::entry_t foundEntry;
        while (low <= high) {
            int index = (high + low) / 2;

            if (file.entries[index].id < targethash) {
                low = index + 1;
            }
            else if (file.entries[index].id > targethash) {
                high = index - 1;
            }
            else {
                return true;
            }
        }
    }

    return false;
}
