#pragma once

#include <string>
#include <unordered_map>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef float f32;

enum textureType_t {
    TT_2D = 0,
    TT_3D = 1,
    TT_CUBIC = 2,
};

enum textureMaterialKind_t {
    TMK_NONE            = 0,
    TMK_ALBEDO          = 1,
    TMK_SPECULAR        = 2,
    TMK_NORMAL          = 3,
    TMK_SMOOTHNESS      = 4,
    TMK_COVER           = 5,
    TMK_SSSMASK         = 6,
    TMK_COLORMASK       = 7,
    TMK_BLOOMMASK       = 8,
    TMK_HEIGHTMAP       = 9,
    TMK_DECALALBEDO     = 10,
    TMK_DECALNORMAL     = 11,
    TMK_DECALSPECULAR   = 12,
    TMK_LIGHTPROJECT    = 13,
    TMK_PARTICLE        = 14,
    TMK_DECALHEIGHTMAP  = 15,
    TMK_AO              = 16,
    TMK_UNUSED_3        = 17,
    TMK_UI              = 18,
    TMK_FONT            = 19,
    TMK_LEGACY_FLASH_UI = 20,
    TMK_UNUSED_4        = 21,
    TMK_BLENDMASK       = 22,
    TMK_PAINTEDDATAGRID = 23,
    TMK_COUNT           = 24,
};

enum textureFormat_t {
    FMT_NONE            = 0,
    FMT_RGBA32F         = 1,
    FMT_RGBA16F         = 2,
    FMT_RGBA8           = 3,
    FMT_RGBA8_SRGB      = 32,
    FMT_ARGB8           = 4,
    FMT_ALPHA           = 5,
    FMT_L8A8_DEPRECATED = 6,
    FMT_RG8             = 7,
    FMT_LUM8_DEPRECATED = 8,
    FMT_INT8_DEPRECATED = 9,
    FMT_BC1             = 10,
    FMT_BC1_SRGB        = 33,
    FMT_BC1_ZERO_ALPHA  = 54,
    FMT_BC3             = 11,
    FMT_BC3_SRGB        = 34,
    FMT_BC4             = 24,
    FMT_BC5             = 25,
    FMT_BC6H_UF16       = 22,
    FMT_BC6H_SF16       = 36,
    FMT_BC7             = 23,
    FMT_BC7_SRGB        = 35,
    FMT_DEPTH           = 12,
    FMT_DEPTH_STENCIL   = 13,
    FMT_DEPTH16         = 31,
    FMT_X32F            = 14,
    FMT_Y16F_X16F       = 15,
    FMT_X16             = 16,
    FMT_Y16_X16         = 17,
    FMT_RGB565          = 18,
    FMT_R8              = 19,
    FMT_R11FG11FB10F    = 20,
    FMT_R9G9B9E5        = 57,
    FMT_X16F            = 21,
    FMT_SMALLF          = 60,
    FMT_MAINVIEW_SMALLF = 61,
    FMT_RG16F           = 26,
    FMT_R10G10B10A2     = 27,
    FMT_RG32F           = 28,
    FMT_R32_UINT        = 29,
    FMT_RG32_UINT       = 58,
    FMT_R16_UINT        = 30,
    FMT_R8_UINT         = 55,
    FMT_ASTC_4X4        = 37,
    FMT_ASTC_4X4_SRGB   = 38,
    FMT_ASTC_5X4        = 39,
    FMT_ASTC_5X4_SRGB   = 40,
    FMT_ASTC_5X5        = 41,
    FMT_ASTC_5X5_SRGB   = 42,
    FMT_ASTC_6X5        = 43,
    FMT_ASTC_6X5_SRGB   = 44,
    FMT_ASTC_6X6        = 45,
    FMT_ASTC_6X6_SRGB   = 46,
    FMT_ASTC_8X5        = 47,
    FMT_ASTC_8X5_SRGB   = 48,
    FMT_ASTC_8X6        = 49,
    FMT_ASTC_8X6_SRGB   = 50,
    FMT_ASTC_8X8        = 51,
    FMT_ASTC_8X8_SRGB   = 52,
    FMT_DEPTH32F        = 53,
    FMT_RGBA16_UINT     = 56,
    FMT_RG16_UINT       = 62,
    FMT_RGBA16          = 59,
    FMT_NEXTAVAILABLE   = 63,
};

std::string textureType_tostring(textureType_t type);
std::string textureMaterialKind_tostring(textureMaterialKind_t type);
std::string textureFormat_tostring(textureFormat_t type);

struct ImageHeader {
    char magic[3];
    u8  version; // Same as the ResourceEntry version [23, 26]
    textureType_t textureType;
    textureMaterialKind_t textureMaterialKind;
    u32 pixelWidth;
    u32 pixelHeight;
    u32 depth; // For 3D images (i.e. 128x128x128)
    u32 mipCount; // For cubics you need to multiply the raw value by 6 after reading it
    f32 unkFloat1; // Always 0
    f32 albedoSpecularBias;
    f32 albedoSpecularScale;
    u8  padding1;
    textureFormat_t textureFormat;
    u32 always8;
    u32 padding2;
    u16 padding3;
    u8  streamed; // If true, there are mips stored in the streamdb files
    u8  singleStream; // If true, all streamdb mips are placed in one streamdb entry. Only true for lightprobes in the vanilla files
    u8  noMips;
    u8  fftBloom;
	u8  prefiltermips; // Only present when version >= 24

    // Mips [0, header.streamDBMipCount) are stored in the .streamdb entry
    // Mips [header.streamDBMipCount, header.mipCount) are stored in the .resources entry 
    u32 streamDBMipCount;

    // Length of the header read from the file
    // (This is not read/written to the file, it's here for convenience because of optional flags)
    u32 HEADER_LENGTH; 

    bool Read(const char* data, const size_t length);

    void tostring(std::string& addto) const;
};

struct ImageMipInfo {
    u32 mipLevel;
    u32 mipSlice;
    u32 mipPixelWidth;
    u32 mipPixelHeight;
    u32 mipPixelDepth;
    u32 decompressedSize;
    u32 flagIsCompressed; // Mips stored in .resources are never compressed
    u32 compressedSize;
    u32 cumulativeSizeStreamDB;

    void tostring(std::string& addto) const;
};

struct idImage {
    ImageHeader header;
    ImageMipInfo* mipinfos = nullptr; // Length == header.mipCount

    ~idImage() {
        delete[] mipinfos;
    }

    bool audit() const;

    bool Read(const char* data, size_t length);

    void tostring(std::string& addto, const char* imageName) const;
};

typedef std::unordered_map<std::string, ImageHeader> idImageHeaderMap_t;

bool idImageHeaderMap_Build(idImageHeaderMap_t& map, const std::string& gamedir);

struct ID3D11Device;
struct ID3D11DeviceContext;
enum D3D_FEATURE_LEVEL : int;

struct idImageEncodingResults {
    uint8_t* buffer = nullptr;
    size_t buffer_max = 0;
    size_t file_length = 0; // <= buffer_max

    ~idImageEncodingResults() {
        delete[] buffer;
    }
};

struct idImageEncodingContext {
    bool                 m_initialized = false;
    ID3D11Device*        m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    D3D_FEATURE_LEVEL    m_featurelevel;
    idImageHeaderMap_t   m_headermap;
    
    bool InitializeContext(const std::string& gamedir);
    bool EncodeImage(const std::string& AssetPath, const wchar_t* FilePath, idImageEncodingResults& results);
    bool Release();
};