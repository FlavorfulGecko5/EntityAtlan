// idMapFileEditor 0.1 by proteh
// -- edited by Scorp0rX0r 09/09/2020 - Remove file operations and work with streams only.
// -- Further edited by FlavorfulGecko5 to integrate into .entities parser

namespace Oodle 
{
    bool Download(const wchar_t* url, const wchar_t* saveto);
    bool init();
    bool init(const char* dllpath);
    bool IsInitialized();
    bool DecompressBuffer(char* inputBuffer, size_t inputSize, char* outputBuffer, size_t outputSize);
    bool CompressBuffer(char* inputBuffer, size_t inputSize, char* outputBuffer, size_t& outputSize);

    int CompressBuffer(char* inputBuffer, size_t inputSize, char* outputBuffer, int compLevel);
}