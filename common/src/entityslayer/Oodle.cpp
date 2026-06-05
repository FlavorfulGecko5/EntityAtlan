// idMapFileEditor 0.1 by proteh
// -- edited by Scorp0rX0r 09/09/2020 - Remove file operations and work with streams only.
// -- Further edited by FlavorfulGecko5 to integrate into .entities parser

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Urlmon.h>
#include "Oodle.h"
#pragma comment(lib, "urlmon.lib")

/* Typedefs from original program */
typedef unsigned char byte;
typedef unsigned char uint8;
typedef unsigned int uint32;
typedef unsigned __int64 uint64;
typedef signed __int64 int64;
typedef signed int int32;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint;

/* Oodle Function typedefs */
typedef int WINAPI OodLZ_CompressFunc(
    int codec, uint8* src_buf, size_t src_len, uint8* dst_buf, int level,
    void* opts, size_t offs, size_t unused, void* scratch, size_t scratch_size);

typedef int WINAPI OodLZ_DecompressFunc(uint8* src_buf, int src_len, uint8* dst, size_t dst_size,
    int fuzz, int crc, int verbose,
    uint8* dst_base, size_t e, void* cb, void* cb_ctx, void* scratch, size_t scratch_size, int threadPhase);

/* Variables used in Oodle Functions */
HMODULE oodle;
OodLZ_CompressFunc* OodLZ_Compress;
OodLZ_DecompressFunc* OodLZ_Decompress;
bool initializedSuccessfully = false;

bool Oodle::Download(const wchar_t* url, const wchar_t* writeto) {
    HRESULT result = URLDownloadToFile(NULL, url, writeto, 0, NULL);
    return result == S_OK;
}

bool Oodle::IsInitialized() {
    return initializedSuccessfully;
}

bool Oodle::init() {
    return Oodle::init("./oo2core_9_win64.dll");
}

bool Oodle::init(const char* dllpath)
{
    initializedSuccessfully = false;

    oodle = LoadLibraryA(dllpath);
    if (oodle == nullptr) // Could not find oodle binary
        return false;

    OodLZ_Decompress = (OodLZ_DecompressFunc*)GetProcAddress(oodle, "OodleLZ_Decompress");
    OodLZ_Compress = (OodLZ_CompressFunc*)GetProcAddress(oodle, "OodleLZ_Compress");

    if (OodLZ_Decompress == nullptr || OodLZ_Compress == nullptr)
    { // Couldn't find the function(s)
        FreeLibrary(oodle);
        oodle = nullptr;
        OodLZ_Decompress = nullptr;
        OodLZ_Compress = nullptr;
        return false;
    }

    initializedSuccessfully = true;
    return true;
}

bool Oodle::DecompressBuffer(char* inputBuffer, size_t inputSize, char* outputBuffer, size_t outputSize)
{
    if (!initializedSuccessfully && !init())
        return false;

    int result = OodLZ_Decompress((byte*)inputBuffer, (int)inputSize, (byte*)outputBuffer, outputSize,
        1, 1, 0, NULL, NULL, NULL, NULL, NULL, NULL, 0);

    if ((size_t)result != outputSize) // Decompression failed
        return false;
    return true;
}

bool Oodle::CompressBuffer(char* inputBuffer, size_t inputSize, char* outputBuffer, size_t& outputSize)
{
    if (!initializedSuccessfully && !init())
        return false;

    int compressedSize = OodLZ_Compress(13, (byte*)inputBuffer, inputSize, (byte*)outputBuffer,
        4, 0, 0, 0, 0, 0);
    if (compressedSize < 0) // Compression failed
        return false;

    outputSize = (size_t)compressedSize;
    return true;
}

int Oodle::CompressBuffer(char* inputBuffer, size_t inputSize, char* outputBuffer, int compLevel)
{
    if (!initializedSuccessfully && !init())
        return false;

    return OodLZ_Compress(13, (byte*)inputBuffer, inputSize, (byte*)outputBuffer, compLevel, 0, 0, 0, 0, 0);
}
