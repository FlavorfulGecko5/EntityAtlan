#include "idImage.h"
#include "io/BinaryReader.h"
#include <cassert>

#define checkread(VAR) if(!reader.ReadLE(VAR)) {return false;}

bool ImageHeader::Read(const char* data, const size_t length)
{
	// We can memcpy the first 40 bytes before
	// running into memory alignment issues forcing us to read
	// each variable manually
	if (length < 40)
		return false;
	memcpy(this, data, 40);
	BinaryReader reader(data + 40, length - 40);

	checkread(padding1);
	checkread(*reinterpret_cast<int*>(&textureFormat));
	checkread(always8);
	checkread(padding2);
	checkread(padding3);
	checkread(streamed);
	checkread(singleStream);
	checkread(noMips);
	checkread(fftBloom);
	if (version >= 24) {
		checkread(prefiltermips);
		HEADER_LENGTH = 64;
	}
	else {
		prefiltermips = 0;
		HEADER_LENGTH = 63;
	}
	checkread(streamDBMipCount);

	return true;
}

bool idImage::Read(const char* data, size_t length)
{
	/*
	* Step 1: Read the Header
	*/

	if(header.Read(data, length) == false)
		return false;

	BinaryReader reader(data + header.HEADER_LENGTH, length - header.HEADER_LENGTH);

	/*
	* Step 2: Read the mip infos
	*/

	if(header.textureType == TT_CUBIC)
		header.mipCount *= 6;

	const size_t mipinfosize = header.mipCount * sizeof(ImageMipInfo);
	if (reader.GetRemaining() < mipinfosize) {
		return false;
	}
	mipinfos = new ImageMipInfo[header.mipCount];
	memcpy(mipinfos, reader.GetNext(), mipinfosize);
	reader.GoRight(mipinfosize);


	/*
	* Step 3: Some final debug checks
	*/

	#if 1
	const char* dummy = nullptr;
	for (uint32_t i = header.streamDBMipCount; i < header.mipCount; i++) {

		assert(mipinfos[i].flagIsCompressed == 0);
		assert(mipinfos[i].compressedSize == mipinfos[i].decompressedSize);
		assert(reader.ReadBytes(dummy, mipinfos[i].decompressedSize));
	}
	assert(reader.ReachedEOF());
	#endif

	return true;
}

bool idImage::audit() const
{
	assert(header.version >= 23 && header.version <= 26);

	// Largest texture sizes are 2^13 == 4096 hence 13 mips

	if (header.textureType == TT_CUBIC) {
		assert(header.mipCount / 6 < 14);
	}
	else {
		assert(header.mipCount < 14);
	}
	
	//assert(header.albedoSpecularBias == 1);
	//assert(header.albedoSpecularScale == 0);
	assert(header.unkFloat1 == 0.0);
	assert(header.padding1 == 0);
	assert(header.always8 == 8);
	assert(header.padding2 == 0);
	assert(header.padding3 == 0);

	if (header.streamDBMipCount < header.mipCount) {
		uint32_t left = mipinfos[header.streamDBMipCount].cumulativeSizeStreamDB;
		uint32_t right = 32 * header.mipCount + 64;

		// Only one image in the entire game passes the right-side check
		assert(left == right || left == right - 1);
	}
	assert(header.streamDBMipCount <= header.mipCount);

	return 1;
}

#define addprop(PROP) {addto.append("\t" #PROP " = "); addto.append(std::to_string(PROP)); addto.push_back('\n');}

void idImage::tostring(std::string& addto, const char* imageName) const
{
	addto.push_back('"');
	addto.append(imageName);
	addto.append("\"= {\n");

	header.tostring(addto);
	for (uint32_t i = 0; i < header.mipCount; i++) {
		addto.append("mipinfo[");
		addto.append(std::to_string(i));
		addto.append("] = {\n");

		mipinfos[i].tostring(addto);
		addto.append("}\n");
	}

	addto.append("}\n");
}


void ImageMipInfo::tostring(std::string& addto) const
{
	addprop(mipLevel);
	addprop(mipSlice);
	addprop(mipPixelWidth);
	addprop(mipPixelHeight);
	addprop(mipPixelDepth);
	addprop(decompressedSize);
	addprop(flagIsCompressed);
	addprop(compressedSize);
	addprop(cumulativeSizeStreamDB);
}

void ImageHeader::tostring(std::string& addto) const
{
	addto.append("\tmagic = ");
	addto.push_back(magic[0]); addto.push_back(magic[1]); addto.push_back(magic[2]); addto.append("\n");

	addprop(version)

	addto.append("\ttextureType = ");
	addto.append(textureType_tostring(textureType));

	addto.append("\n\ttextureMaterialKind = ");
	addto.append(textureMaterialKind_tostring(textureMaterialKind));
	addto.append("\n");

	addprop(pixelWidth) addprop(pixelHeight) addprop(depth)
	addprop(mipCount) addprop(unkFloat1) addprop(albedoSpecularBias)
	addprop(albedoSpecularScale)
	addprop(padding1)

	addto.append("\ttextureFormat = ");
	addto.append(textureFormat_tostring(textureFormat));
	addto.append("\n");

	addprop(always8);
	addprop(padding2);
	addprop(padding3);
	addprop(streamed);
	addprop(singleStream);
	addprop(noMips);
	addprop(fftBloom);
	
	if(version >= 24)
		addprop(prefiltermips);

	addprop(streamDBMipCount);
}

#define CASE(PARM) case PARM: return #PARM;

std::string textureType_tostring(textureType_t type)
{
	switch (type) {
		CASE(TT_2D)
		CASE(TT_3D)
		CASE(TT_CUBIC)
		default: return "ERROR_UNKNOWN";
	}
}

std::string textureMaterialKind_tostring(textureMaterialKind_t type)
{
	switch (type) {
		CASE(TMK_NONE			  )
		CASE(TMK_ALBEDO			  )
		CASE(TMK_SPECULAR		  )
		CASE(TMK_NORMAL			  )
		CASE(TMK_SMOOTHNESS		  )
		CASE(TMK_COVER			  )
		CASE(TMK_SSSMASK		  )
		CASE(TMK_COLORMASK		  )
		CASE(TMK_BLOOMMASK		  )
		CASE(TMK_HEIGHTMAP		  )
		CASE(TMK_DECALALBEDO	  )
		CASE(TMK_DECALNORMAL	  )
		CASE(TMK_DECALSPECULAR	  )
		CASE(TMK_LIGHTPROJECT	  )
		CASE(TMK_PARTICLE		  )
		CASE(TMK_DECALHEIGHTMAP	  )
		CASE(TMK_AO				  )
		CASE(TMK_UNUSED_3		  )
		CASE(TMK_UI				  )
		CASE(TMK_FONT			  )
		CASE(TMK_LEGACY_FLASH_UI  )
		CASE(TMK_UNUSED_4		  )
		CASE(TMK_BLENDMASK		  )
		CASE(TMK_PAINTEDDATAGRID  )
		CASE(TMK_COUNT			  )
		default: return "ERROR_UNKNOWN";
	}
}

std::string textureFormat_tostring(textureFormat_t type)
{
	switch (type) {

		CASE(FMT_NONE			  )
		CASE(FMT_RGBA32F		  )
		CASE(FMT_RGBA16F		  )
		CASE(FMT_RGBA8			  )
		CASE(FMT_RGBA8_SRGB		  )
		CASE(FMT_ARGB8			  )
		CASE(FMT_ALPHA			  )
		CASE(FMT_L8A8_DEPRECATED  )
		CASE(FMT_RG8			  )
		CASE(FMT_LUM8_DEPRECATED  )
		CASE(FMT_INT8_DEPRECATED  )
		CASE(FMT_BC1			  )
		CASE(FMT_BC1_SRGB		  )
		CASE(FMT_BC1_ZERO_ALPHA	  )
		CASE(FMT_BC3			  )
		CASE(FMT_BC3_SRGB		  )
		CASE(FMT_BC4			  )
		CASE(FMT_BC5			  )
		CASE(FMT_BC6H_UF16		  )
		CASE(FMT_BC6H_SF16		  )
		CASE(FMT_BC7			  )
		CASE(FMT_BC7_SRGB		  )
		CASE(FMT_DEPTH			  )
		CASE(FMT_DEPTH_STENCIL	  )
		CASE(FMT_DEPTH16		  )
		CASE(FMT_X32F			  )
		CASE(FMT_Y16F_X16F		  )
		CASE(FMT_X16			  )
		CASE(FMT_Y16_X16		  )
		CASE(FMT_RGB565			  )
		CASE(FMT_R8				  )
		CASE(FMT_R11FG11FB10F	  )
		CASE(FMT_R9G9B9E5		  )
		CASE(FMT_X16F			  )
		CASE(FMT_SMALLF			  )
		CASE(FMT_MAINVIEW_SMALLF  )
		CASE(FMT_RG16F			  )
		CASE(FMT_R10G10B10A2	  )
		CASE(FMT_RG32F			  )
		CASE(FMT_R32_UINT		  )
		CASE(FMT_RG32_UINT		  )
		CASE(FMT_R16_UINT		  )
		CASE(FMT_R8_UINT		  )
		CASE(FMT_ASTC_4X4		  )
		CASE(FMT_ASTC_4X4_SRGB	  )
		CASE(FMT_ASTC_5X4		  )
		CASE(FMT_ASTC_5X4_SRGB	  )
		CASE(FMT_ASTC_5X5		  )
		CASE(FMT_ASTC_5X5_SRGB	  )
		CASE(FMT_ASTC_6X5		  )
		CASE(FMT_ASTC_6X5_SRGB	  )
		CASE(FMT_ASTC_6X6		  )
		CASE(FMT_ASTC_6X6_SRGB	  )
		CASE(FMT_ASTC_8X5		  )
		CASE(FMT_ASTC_8X5_SRGB	  )
		CASE(FMT_ASTC_8X6		  )
		CASE(FMT_ASTC_8X6_SRGB	  )
		CASE(FMT_ASTC_8X8		  )
		CASE(FMT_ASTC_8X8_SRGB	  )
		CASE(FMT_DEPTH32F		  )
		CASE(FMT_RGBA16_UINT	  )
		CASE(FMT_RG16_UINT		  )
		CASE(FMT_RGBA16			  )
		CASE(FMT_NEXTAVAILABLE	  )

		default: return "ERROR_UNKNOWN";
	}
	return "";
}

