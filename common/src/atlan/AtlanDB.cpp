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

bool AtlanDB_Finalize(const char* archive, const size_t archive_length, const char* sfile, const size_t sfile_length, u32 resversion, dbentrylist& entries, charbuffer_t& output)
{
    // Add header size to the offsets
    for (u32 i = 0; i < entries.num; i++) {
        entries.entries[i].offset += 32 + 64 * entries.num + archive_length;
    }

    BinaryWriter writer;
    writer.EnsureMaxCapacity(archive_length + sfile_length + 2048);

    writer.WriteBytes("ATDB", 4);
    writer << (u32)1 << resversion << entries.num << archive_length << archive_length;
    writer.WriteBytes((char*)entries.entries, entries.num * sizeof(AtlanDBFile::entry));
    writer.WriteBytes(archive, archive_length);
    writer.WriteBytes(sfile, sfile_length);

    AtlanDBFile validator;
    check(validator.ReadFrom(writer.GetBuffer(), writer.GetFilledSize()));

    output.capacity = writer.GetMaxCapacity();
    output.length = writer.GetFilledSize();
    output.data = writer.Finalize();

    return true;
}

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

    return AtlanDB_Finalize(bmodel, bmodel_length, sfile, sfile_length, 67, entries, output);
}

// Doom Eternal BaseModel definitions: https://github.com/jandk/valen/blob/main/templates/md6mesh.bt
bool GetEntries_BaseModel(BinaryReader r, const size_t sfile_length, dbentrylist& output) 
{
    u32 length32;
    u16 length16;

    // Md6Header
    r >> length32;
    r.GoRight(length32 + 29); 

    // Md6boneInfo
    r >> length16;
    r.GoRight(length16 + 88);

    // Md6MeshInfos
    u32 numStructures;
    r >> numStructures;
    for (u32 i = 0; i < numStructures; i++) {

        r >> length32;
        r.GoRight(length32);
        r >> length32;
        r.GoRight(length32 + 21);

        // Md6LodInfos
        for (u32 i = 0; i < 5; i++) {
            u32 absent;
            r >> absent;
            if (!absent) {
                r.GoRight(72);
            }
        }

        r.GoRight(5);
    }

    // Md6MaterialInfo
    r >> numStructures;
    for (u32 i = 0; i < numStructures; i++) {
        r >> length32;
        r.GoRight(length32 + 12);
    }

    // Md6GeoDecal
    r >> length32;
    r.GoRight(length32);

    length32 = 0;
    r >> numStructures;
    for (u32 i = 0; i < numStructures; i++) {
        u32 n;
        r >> n;
        length32 += n;
    }
    r.GoRight(4 * length32);

    // GeometryMemoryLayouts
    r >> numStructures;
    for (u32 i = 0; i < numStructures; i++) {
        r.GoRight(8);
        r >> length32;
        r.GoRight(8 * length32 + 4);
    }

    u32 compsum = 0;

    // GeometryDiskLayouts
    output.num = 0;
    for (u32 i = 0; i < numStructures; i++) {

        u32 compsize, offset;
        
        r.GoRight(8);
        r >> compsize >> offset;

        check(offset == compsum);
        compsum += compsize;

        if(compsize == 0)
            continue;
        output.num++;
        output.entries[i] = {0, 0, i, 0, 0, 0, 0, offset, compsize};
    }

    check(compsum == sfile_length);
    check(r.ReachedEOF());
    return true;
}

bool AtlanDBFile::FromBaseModel(const char* mesh, const size_t mesh_length, const char* sfile, const size_t sfile_length, charbuffer_t& output)
{
    dbentrylist entries;

    if (!GetEntries_BaseModel(BinaryReader(mesh, mesh_length), sfile_length, entries)) {
        return false;
    }

    return AtlanDB_Finalize(mesh, mesh_length, sfile, sfile_length, 31, entries, output);
}

bool AtlanDBFile::FromGeneric_NoEntries(const char* res, const size_t res_length, const u32 resversion, charbuffer_t& output)
{
    dbentrylist entries;
    entries.num = 0;

    return AtlanDB_Finalize(res, res_length, nullptr, 0, resversion, entries, output);
}
