#include "AtlanDB.h"
#include "io/BinaryReader.h"

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
