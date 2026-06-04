#include "idImage.h"
#include "io/BinaryReader.h"

#ifdef _DEBUG
#include <cassert>
#define check(OP) assert(OP)
#else
#define check(OP) if(!(OP)) {return false;}
#endif

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

bool idImage::Read(const char* data, size_t length, bool FullyValidate)
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

	if (FullyValidate) {
		const char* dummy = nullptr;
		for (uint32_t i = header.streamDBMipCount; i < header.mipCount; i++) {
			check(mipinfos[i].flagIsCompressed == 0);
			check(mipinfos[i].compressedSize == mipinfos[i].decompressedSize);
			check(reader.ReadBytes(dummy, mipinfos[i].decompressedSize));
		}
		check(reader.ReachedEOF());
		check(header.version >= 23 && header.version <= 26);

		// Largest texture sizes are 2^13 == 4096 hence 13 mips
		if (header.textureType == TT_CUBIC) {
			check(header.mipCount / 6 < 14);
		}
		else {
			check(header.mipCount < 14);
		}

		check(header.unkFloat1 == 0.0);
		check(header.padding1 == 0);
		check(header.always8 == 8);
		check(header.padding2 == 0);
		check(header.padding3 == 0);

		check(header.streamDBMipCount <= header.mipCount);
		if (header.streamDBMipCount < header.mipCount) {
			uint32_t left = mipinfos[header.streamDBMipCount].cumulativeSizeStreamDB;
			uint32_t right = 32 * header.mipCount + 64;

			// Only one image in the entire game passes the right-side check
			check(left == right || left == right - 1);
		}

		if (header.textureType == TT_2D && header.textureMaterialKind != TMK_NONE) {
			for (uint32_t i = 0; i < header.streamDBMipCount; i++) {
				check(mipinfos[i].mipPixelWidth > 32 || mipinfos[i].mipPixelHeight > 32);
			}
			for (uint32_t i = header.streamDBMipCount; i < header.mipCount; i++) {
				check(mipinfos[i].mipPixelWidth <= 32 && mipinfos[i].mipPixelHeight <= 32);
			}
		}

		// Not IFF as mipCount can be 1 when noMips is false
		if (header.noMips) {
			check(header.mipCount == 1);
		}
	}

	return true;
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

void ImageHeader::DefaultInitialize() {
	magic[0] = 'B'; magic[1] = 'I'; magic[2] = 'M';
	version = 26;
	textureType = TT_2D;
	textureMaterialKind = TMK_NONE;
	pixelWidth = 0; pixelHeight = 0;
	depth = 0;
	mipCount = 0;
	unkFloat1 = 0;
	albedoSpecularBias = 0;
	albedoSpecularScale = 1;
	padding1 = 0;
	textureFormat = FMT_NONE;
	always8 = 8;
	padding2 = 0; padding3 = 0;
	streamed = 1;
	singleStream = 0;
	noMips = 0;
	fftBloom = 0;
	prefiltermips = 0;
	streamDBMipCount = 0;
	CalcHeaderSize();
}

#if DEBUG_COLLECT_EXTENSIONPROPS
std::set<std::string> ALL_MATERIAL_PROPS;
#endif

// Attempts to parse
bool idImageExtensionData::FromAssetPath(const std::string& AssetPath)
{
	const std::unordered_map<std::string, textureMaterialKind_t> StringTMKmap = {
		{"albedo"          , TMK_ALBEDO },
		{"ao"			   , TMK_AO },
		{"blendmask"	   , TMK_BLENDMASK },
		{"bloommask"	   , TMK_BLOOMMASK },
		{"colormask"	   , TMK_COLORMASK },
		{"cover"		   , TMK_COVER },
		{"decalalbedo"	   , TMK_DECALALBEDO },
		{"decalheight"	   , TMK_DECALHEIGHTMAP }, // An enum value exists, but "None" is used in the entries instead
		{"decalnormal"	   , TMK_DECALNORMAL },
		{"decalspecular"   , TMK_DECALSPECULAR },
		{"font"			   , TMK_FONT },
		{"heightmap"	   , TMK_HEIGHTMAP },
		{"lightproject"	   , TMK_LIGHTPROJECT },
		{"normal"		   , TMK_NORMAL },
		{"painteddatagrid" , TMK_PAINTEDDATAGRID },
		{"particle"		   , TMK_PARTICLE },
		{"smoothness"	   , TMK_SMOOTHNESS },
		{"specular"		   , TMK_SPECULAR },
		{"sssmask"		   , TMK_SSSMASK },
		{"ui"			   , TMK_UI },
	};

	const std::unordered_map<std::string, textureFormat_t> StringFMTmap = {
		{"alpha", FMT_ALPHA },
		{"bc1", FMT_BC1},
		{"bc1srgb", FMT_BC1_SRGB},
		{"bc1za", FMT_BC1_ZERO_ALPHA},
		{"bc3", FMT_BC3},
		{"bc3srgb", FMT_BC3_SRGB},
		{"bc4",FMT_BC4},
		{"bc5", FMT_BC5},
		{"bc6huf16", FMT_BC6H_UF16},
		{"bc7", FMT_BC7},
		{"bc7srgb", FMT_BC7_SRGB},
		{"r8", FMT_R8},
		{"rg16f", FMT_RG16F},
		{"rg8", FMT_RG8},
		{"rgba8", FMT_RGBA8}
	};

	const char* iter = AssetPath.data();
	const char* itermax = AssetPath.data() + AssetPath.length();

	while (*iter) {
		char c = *iter++;

		if(c != '$')
			continue;

		bool hasValue = false;
		std::string propkey;
		std::string propvalue;
		while (*iter) {
			c = *iter;

			if(c == '$')
				break;

			iter++;
			if (c != '=') {
				propkey.push_back(c);
				continue;
			}

			hasValue = true;
			break;
		}

		if (hasValue) {
			while (*iter) {
				c = *iter;
				if(c == '$')
					break;

				iter++;
				propvalue.push_back(c);
			}
		}

		//printf("['%s' = '%s']", propkey.c_str(), propvalue.c_str());

		if (hasValue) {
			if(propkey != "mtlkind")
				continue;

			const auto& mtliter = StringTMKmap.find(propvalue);
			check(mtliter != StringTMKmap.end());

			m_material = mtliter->second;
		}
		else {
			if (propkey == "streamed") {
				m_streamed = 1;
			}
			else if (propkey == "nomips") {
				m_nomips = 1;
			}
			else if (propkey == "prefiltermips") {
				m_prefiltermips = 1;
			}
			else if (propkey == "uncompressed") {
				m_uncompressed = 1;
			}
			else {
				const auto& FMTIter = StringFMTmap.find(propkey);
				check(FMTIter != StringFMTmap.end());
				m_format = FMTIter->second;
			}
		}

		#if DEBUG_COLLECT_EXTENSIONPROPS
		std::string setstring = propkey;
		setstring.push_back('=');
		setstring.append(propvalue);
		ALL_MATERIAL_PROPS.insert(setstring);
		#endif
	}

	return true;
}

void idImageExtensionData::InferFormatFromMaterial() {
	//const std::unordered_map<textureMaterialKind_t, textureFormat_t> TmkFmtMap = {
	//	{TMK_SSSMASK     , FMT_BC7},
	//	{TMK_BLENDMASK   , FMT_BC7},
	//};

	if(m_format != FMT_NONE)
		return;

	// https://github.com/jandk/valen/blob/142d78b25be280c71032036aba2d5134873fdfdd/valen-game-idtech/src/main/java/be/twofold/valen/game/idtech/material/AbstractMaterialReader.java#L398
	switch (m_material) {
		case TMK_ALBEDO: 
		case TMK_SPECULAR:
		case TMK_BLOOMMASK:
		m_format = FMT_BC1_SRGB;
		return;

		case TMK_SMOOTHNESS: 
		case TMK_COVER: 
		case TMK_AO:
		m_format = FMT_BC4;
		return;

		case TMK_NORMAL:
		m_format = FMT_BC5;
		return;

		case TMK_COLORMASK:
		m_format = FMT_BC1;
		return;

		// Ambiguous: Can be multiple encodings depending on material template
		case TMK_BLENDMASK:
		case TMK_SSSMASK:
		case TMK_PAINTEDDATAGRID:
		return;

		// Every other image type *should* have it's encoding specified
		// in the extension (although it can still be wrong in some cases)
		default:
		return;
	}
}

#define CASE(PARM) case PARM: return #PARM;

#define ENTRY(PARM) { #PARM, PARM },

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

bool textureMaterialKind_fromstring(const std::string& string, textureMaterialKind_t& output)
{
	const std::unordered_map<std::string, textureMaterialKind_t> stringmap = {
		ENTRY(TMK_NONE)
		ENTRY(TMK_ALBEDO)
		ENTRY(TMK_SPECULAR)
		ENTRY(TMK_NORMAL)
		ENTRY(TMK_SMOOTHNESS)
		ENTRY(TMK_COVER)
		ENTRY(TMK_SSSMASK)
		ENTRY(TMK_COLORMASK)
		ENTRY(TMK_BLOOMMASK)
		ENTRY(TMK_HEIGHTMAP)
		ENTRY(TMK_DECALALBEDO)
		ENTRY(TMK_DECALNORMAL)
		ENTRY(TMK_DECALSPECULAR)
		ENTRY(TMK_LIGHTPROJECT)
		ENTRY(TMK_PARTICLE)
		ENTRY(TMK_DECALHEIGHTMAP)
		ENTRY(TMK_AO)
		ENTRY(TMK_UNUSED_3)
		ENTRY(TMK_UI)
		ENTRY(TMK_FONT)
		ENTRY(TMK_LEGACY_FLASH_UI)
		ENTRY(TMK_UNUSED_4)
		ENTRY(TMK_BLENDMASK)
		ENTRY(TMK_PAINTEDDATAGRID)
		ENTRY(TMK_COUNT)
	};

	const auto& iter = stringmap.find(string);
	if(iter == stringmap.end())
		return false;
	output = iter->second;
	return true;
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

bool textureFormat_fromstring(const std::string& string, textureFormat_t& output)
{
	const std::unordered_map<std::string, textureFormat_t> stringmap = {
		ENTRY(FMT_NONE)
		ENTRY(FMT_RGBA32F)
		ENTRY(FMT_RGBA16F)
		ENTRY(FMT_RGBA8)
		ENTRY(FMT_RGBA8_SRGB)
		ENTRY(FMT_ARGB8)
		ENTRY(FMT_ALPHA)
		ENTRY(FMT_L8A8_DEPRECATED)
		ENTRY(FMT_RG8)
		ENTRY(FMT_LUM8_DEPRECATED)
		ENTRY(FMT_INT8_DEPRECATED)
		ENTRY(FMT_BC1)
		ENTRY(FMT_BC1_SRGB)
		ENTRY(FMT_BC1_ZERO_ALPHA)
		ENTRY(FMT_BC3)
		ENTRY(FMT_BC3_SRGB)
		ENTRY(FMT_BC4)
		ENTRY(FMT_BC5)
		ENTRY(FMT_BC6H_UF16)
		ENTRY(FMT_BC6H_SF16)
		ENTRY(FMT_BC7)
		ENTRY(FMT_BC7_SRGB)
		ENTRY(FMT_DEPTH)
		ENTRY(FMT_DEPTH_STENCIL)
		ENTRY(FMT_DEPTH16)
		ENTRY(FMT_X32F)
		ENTRY(FMT_Y16F_X16F)
		ENTRY(FMT_X16)
		ENTRY(FMT_Y16_X16)
		ENTRY(FMT_RGB565)
		ENTRY(FMT_R8)
		ENTRY(FMT_R11FG11FB10F)
		ENTRY(FMT_R9G9B9E5)
		ENTRY(FMT_X16F)
		ENTRY(FMT_SMALLF)
		ENTRY(FMT_MAINVIEW_SMALLF)
		ENTRY(FMT_RG16F)
		ENTRY(FMT_R10G10B10A2)
		ENTRY(FMT_RG32F)
		ENTRY(FMT_R32_UINT)
		ENTRY(FMT_RG32_UINT)
		ENTRY(FMT_R16_UINT)
		ENTRY(FMT_R8_UINT)
		ENTRY(FMT_ASTC_4X4)
		ENTRY(FMT_ASTC_4X4_SRGB)
		ENTRY(FMT_ASTC_5X4)
		ENTRY(FMT_ASTC_5X4_SRGB)
		ENTRY(FMT_ASTC_5X5)
		ENTRY(FMT_ASTC_5X5_SRGB)
		ENTRY(FMT_ASTC_6X5)
		ENTRY(FMT_ASTC_6X5_SRGB)
		ENTRY(FMT_ASTC_6X6)
		ENTRY(FMT_ASTC_6X6_SRGB)
		ENTRY(FMT_ASTC_8X5)
		ENTRY(FMT_ASTC_8X5_SRGB)
		ENTRY(FMT_ASTC_8X6)
		ENTRY(FMT_ASTC_8X6_SRGB)
		ENTRY(FMT_ASTC_8X8)
		ENTRY(FMT_ASTC_8X8_SRGB)
		ENTRY(FMT_DEPTH32F)
		ENTRY(FMT_RGBA16_UINT)
		ENTRY(FMT_RG16_UINT)
		ENTRY(FMT_RGBA16)
		ENTRY(FMT_NEXTAVAILABLE)
	};

	const auto& iter = stringmap.find(string);
	if (iter == stringmap.end())
		return false;
	output = iter->second;
	return true;
}