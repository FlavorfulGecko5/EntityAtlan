#include "AtlanDB.h"
#include "io/BinaryReader.h"
#include "io/BinaryWriter.h"
#include <vector>

bool AtlanDBFile::ReadFrom(const char* FILEDATA, size_t FILELENGTH)
{
    BinaryReader reader(FILEDATA, FILELENGTH);

    if(!reader.check32('BDTA'))
        return false;

    reader >> dbversion >> resourceVersion >> numEntries 
        >> resourceSize >> resourceSize_uncompressed;
    check(dbversion == 1 && reader.NoErrors());

    entries = (const entry*)reader.GetNext();

    check(reader.GoRight(numEntries * sizeof(entry)));
    resourceEntry = reader.GetNext();

    /* Verify everything is in-bounds */

    if(resourceSize > FILELENGTH - (resourceEntry - FILEDATA))
        return false;

    for (u64 i = 0; i < numEntries; i++) {
        if(entries[i].offset >= FILELENGTH)
            return false;

        if(entries[i].size > FILELENGTH - entries[i].offset)
            return false;
    }
    return true;
}

struct dbentrylist {
    AtlanDBFile::entry entries[16];
    int num = 0;
};

// Static Model Definitions: https://github.com/jandk/valen/blob/main/templates/model.bt
bool GetEntries_StaticModel(BinaryReader r, const size_t sfile_length, dbentrylist& output) {
    u32 num_lods, num_surfaces, streamable;
    u32 compSizeSum = 0;

    r.GoRight(12);
    r >> num_lods >> num_surfaces;
    r.GoRight(num_lods * sizeof(float));
    r >> streamable; // if 0, all entries inlined
    if(!streamable)
        return r.NoErrors();

    // Model Mesh Infos
    for (u32 i = 0; i < num_surfaces; i++) {
        u32 strlen;
        r >> strlen;
        r.GoRight(strlen + 12);

        for (u32 i = 0; i < 5; i++) {
            u32 absent;
            r >> absent;
            if (!absent) {
                r.GoRight(84); // Skip ModelLODInfo
            }
        }
    }

    // Model Settings
    u32 dummy;
    r.GoRight(12);
    r >> dummy;
    r.GoRight(56 * dummy); // textureAxes array

    // Model Geo Decals
    r >> dummy;
    r.GoRight(32 * dummy);
    r >> dummy;
    r.GoRight(dummy + 4);

    // streamed surface bytes
    r.GoRight(num_lods * num_surfaces);

    // Layouts
    if (streamable) {

        for (u32 i = 0; i < 5; i++) {
            u32 numstreams;
            r >> numstreams;

            // GeometryMemoryLayouts
            for (u32 i = 0; i < numstreams; i++) {
                u32 numVertexStreams;
                r.GoRight(8);
                r >> numVertexStreams;

                r.GoRight(sizeof(u32) * numVertexStreams * 2);
                r.GoRight(4);
            }

            // GeometryDiskLayout
            u32 db_compsize;
            u32 db_offset;
            r.GoRight(8);
            r >> db_compsize >> db_offset;

            check(r.NoErrors());
            check(db_offset == compSizeSum);
            compSizeSum += db_compsize;

            if (db_compsize > 0) {
                output.entries[output.num++] = {0, 0, i, 0, 0, 0, 0, db_offset, db_compsize};
            }
        }
    }

    check(compSizeSum == sfile_length);
    check(r.ReachedEOF());
    return true;
}

bool AtlanDBFile::FromStaticModel(const char* bmodel, const size_t bmodel_length, const char* sfile, const size_t sfile_length, charbuffer_t& output)
{
    dbentrylist entries;
    if (!GetEntries_StaticModel(BinaryReader(bmodel, bmodel_length), sfile_length, entries)) {
        return false;
    }

    for (u32 i = 0; i < entries.num; i++) {
        entries.entries[i].offset += 32 + 64 * entries.num + bmodel_length;
    }

    BinaryWriter writer;
    writer.EnsureMaxCapacity(bmodel_length + sfile_length + 2048);

    writer.WriteBytes("ATDB", 4);
    writer << (u32)1 << (u32)67 << entries.num << bmodel_length << bmodel_length;
    writer.WriteBytes((char*)entries.entries, entries.num * sizeof(AtlanDBFile::entry));
    writer.WriteBytes(bmodel, bmodel_length);
    writer.WriteBytes(sfile, sfile_length);

    AtlanDBFile validator;
    check(validator.ReadFrom(writer.GetBuffer(), writer.GetFilledSize()));

    output.capacity = writer.GetMaxCapacity();
    output.length = writer.GetFilledSize();
    output.data = writer.Finalize();

    return true;
}
