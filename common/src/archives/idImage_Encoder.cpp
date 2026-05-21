
#include <Windows.h>
#include <d3d11.h>
#include "DirectXTex/DirectXTex.h"

#include "idImage.h"
#include "atlan/AtlanLogger.h"
#include "io/BinaryWriter.h"

bool idImageEncodingContext::InitializeContext(const std::string& gamedir) {

	/*
	* Step 1: Global Initialization
	*/
	static bool Global_Init = false;
	if (Global_Init == false) {
		HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		if (FAILED(result)) {
			atlog << "FATAL ERROR: Failed to initialize COM for image encoder (Code: " << result << ")\n";
			return false;
		}
		
		Global_Init = true;
	}

	/*
	* Step 2: Intialize device
	*/

	UINT createDeviceFlags = 0;
	#ifdef _DEBUG
		createDeviceFlags = D3D11_CREATE_DEVICE_DEBUG;
	#endif

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	HRESULT result = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		createDeviceFlags,
		featureLevels,
		_countof(featureLevels),
		D3D11_SDK_VERSION,
		&m_device,
		&m_featurelevel,
		&m_context
	);

	if (FAILED(result)) {
		atlog << "FATAL ERROR: Failed to create Direct3D context for image encoder (Code: " << result << ")\n";
		return false;
	}

	/*
	* Step 3: Build the Image Header Map
	*/

	if (idImageHeaderMap_Build(m_headermap, gamedir) == false) {
		atlog << "FATAL ERROR: Failed to create ImageHeaderMap for image encoder\n";
		return false;
	}

	m_initialized = true;
	return true;
}

bool idImageEncodingContext::Release() {
	m_context->ClearState();
	m_context->Flush();
	m_context->Release();
	m_device->Release();

	m_context = nullptr;
	m_device = nullptr;

	m_headermap.clear();
	m_headermap.rehash(0);

	m_initialized = false;
	return true;
}

bool DXGI_UseGpuEncoding(const DXGI_FORMAT format) {
	switch (format) {
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
		return true;

		default:
		return false;
	}
}

DXGI_FORMAT idFormat_To_DXGI(const textureFormat_t idFormat) {


	switch (idFormat) {

		// TODO: Verify that these are the correct conversions
		case FMT_BC1:              return DXGI_FORMAT_BC1_TYPELESS;
		case FMT_BC1_SRGB: 		   return DXGI_FORMAT_BC1_UNORM_SRGB;
		case FMT_BC1_ZERO_ALPHA:   return DXGI_FORMAT_BC1_UNORM;
		case FMT_BC3: 			   return DXGI_FORMAT_BC3_TYPELESS;
		case FMT_BC3_SRGB:		   return DXGI_FORMAT_BC3_UNORM_SRGB;
		case FMT_BC4:			   return DXGI_FORMAT_BC4_TYPELESS;
		case FMT_BC5:			   return DXGI_FORMAT_BC5_TYPELESS;
		case FMT_BC6H_UF16:		   return DXGI_FORMAT_BC6H_UF16;
		case FMT_BC6H_SF16:		   return DXGI_FORMAT_BC6H_SF16;
		case FMT_BC7:			   return DXGI_FORMAT_BC7_TYPELESS;
		case FMT_BC7_SRGB:		   return DXGI_FORMAT_BC7_UNORM_SRGB;

		default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

bool idImageEncodingContext::EncodeImage(const std::string& AssetPath, const wchar_t* FilePath, idImageEncodingResults& FINAL_IMAGE)
{
	/*
	* Step 1: Locate the vanilla ImageHeader for this file
	*/

	ImageHeader header;
	DXGI_FORMAT dxgiFormat;
	{
		const auto& pair = m_headermap.find(AssetPath);
		if (pair == m_headermap.end()) {
			atlog << "ERROR: Could not find name in ImageHeaderMap\n";
			return false;
		}
		header = pair->second;
	}

	/*
	* Step 2: Ensure this image type is supported
	*/

	if (header.version < 23 || header.version > 26) {
		atlog << "ERROR: Unsupported Image File Version\n";
		return false;
	}
	if (header.textureType != TT_2D) {
		atlog << "ERROR: Only 2D textures are supported\n";
		return false;
	}

	dxgiFormat = idFormat_To_DXGI(header.textureFormat);
	if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
		atlog << "ERROR: Unsupported texture format\n";
		return false;
	}

	/*
	* Step 3: Perform the encoding
	*/

	DirectX::ScratchImage image;
	DirectX::ScratchImage TEMP_IMAGE;
	
	// We could read from memory, but at this point we should refactor our unzipped mod reader so we're not zipping everything internally
	// It's bearable for standard mods, but for image mods will be very wasteful
	//HRESULT result = DirectX::LoadFromWICMemory(parms.data, parms.datalength, DirectX::WIC_FLAGS_NONE, nullptr, image);

	HRESULT result = DirectX::LoadFromWICFile(FilePath, DirectX::WIC_FLAGS_NONE, nullptr, image);
	if (FAILED(result)) {
		atlog << "ERROR: Failed to read raw image file into ScratchImage (Error Code: " << result << ")\n";
		return false;
	}

	atlog << "Generating Mips...\n";
	result = DirectX::GenerateMipMaps(*image.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, TEMP_IMAGE);
	if (FAILED(result)) {
		atlog << "ERROR: Failed to generate Mips (Error Code: "  << result << ")\n";
		return false;
	}

	image.Release(); // Free the un-mipped image

	atlog << "Compressing Texture...\n";
	if (DXGI_UseGpuEncoding(dxgiFormat)) {
		result = Compress(m_device, TEMP_IMAGE.GetImages(), TEMP_IMAGE.GetImageCount(), TEMP_IMAGE.GetMetadata(),
			dxgiFormat, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_ALPHA_WEIGHT_DEFAULT, image);
	}
	else {
		result = Compress(TEMP_IMAGE.GetImages(), TEMP_IMAGE.GetImageCount(), TEMP_IMAGE.GetMetadata(),
			dxgiFormat, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, image);
	}
	if (FAILED(result)) {
		atlog << "ERROR: Failed to encode texture (Error Code: " << result << ")\n";
		return false;
	}

	atlog << "Formatting File\n";

	/*
	* STEP 4: Header adjustments
	*/

	struct {
		u32 width;
		u32 height;
		u32 mips;
	} realvalues;

	realvalues.width =  (u32)image.GetImage(0,0,0)->width;
	realvalues.height = (u32)image.GetImage(0,0,0)->height;
	realvalues.mips =   (u32)image.GetImageCount();

	if (realvalues.width != header.pixelWidth) {
		header.pixelWidth = realvalues.width;
	}
	if (realvalues.height != header.pixelHeight) {
		header.pixelHeight = realvalues.height;
	}
	if (realvalues.mips != header.mipCount) {
		header.mipCount = realvalues.mips;
	}

	// TODO: This is temporary until we start building
	// our own streamdb file
	header.streamDBMipCount = 0;
	header.streamed = 0;

	// Ask Tjoener if you want to know why we do this
	// tl;dr is these variables are only necessary for id's method of encoding images
	// (or something like that)
	header.albedoSpecularBias = 0.0f;
	header.albedoSpecularScale = 1.0f;

	/*
	* STEP 5: Write the file 
	*/

	BinaryWriter writer(image.GetPixelsSize() + 5000, 1.25f);

	writer.WriteBytes(header.magic, 3);
	writer << header.version 
		<< header.textureType 
		<< header.textureMaterialKind
		<< header.pixelWidth << header.pixelHeight << header.depth
		<< header.mipCount << header.unkFloat1
		<< header.albedoSpecularBias << header.albedoSpecularScale << header.padding1
		<< header.textureFormat << header.always8 << header.padding2 << header.padding3
		<< header.streamed << header.singleStream << header.noMips << header.fftBloom
		<< header.prefiltermips << header.streamDBMipCount;


	ImageMipInfo mipinfo;
	for (uint32_t i = 0; i < header.mipCount; i++) {

		const DirectX::Image* mipimage = image.GetImage(i, 0, 0);

		// TODO: Will need to revise a lot of this once we start putting mips
		// into the streamdb

		mipinfo.mipLevel = i;
		mipinfo.mipSlice = 0;
		mipinfo.mipPixelWidth = mipimage->width;
		mipinfo.mipPixelHeight = mipimage->height;
		mipinfo.mipPixelDepth = 1;

		if (i == 0) {
			mipinfo.cumulativeSizeStreamDB = 32 * header.mipCount + 64;
		}
		else {
			mipinfo.cumulativeSizeStreamDB += mipinfo.compressedSize;
		}

		size_t rowpitch = 0, slicepitch = 0;
		DirectX::ComputePitch(dxgiFormat, mipimage->width, mipimage->height, rowpitch, slicepitch);
		mipinfo.decompressedSize = slicepitch;
		mipinfo.flagIsCompressed = 0;
		mipinfo.compressedSize = mipinfo.decompressedSize;

		writer << mipinfo.mipLevel << mipinfo.mipSlice << mipinfo.mipPixelWidth
			<< mipinfo.mipPixelHeight << mipinfo.mipPixelDepth
			<< mipinfo.decompressedSize << mipinfo.flagIsCompressed << mipinfo.compressedSize
			<< mipinfo.cumulativeSizeStreamDB;
	}

	for (uint32_t i = 0; i < header.mipCount; i++) {
		const DirectX::Image* mipimage = image.GetImage(i, 0, 0);
		size_t rowpitch = 0, slicepitch = 0;
		DirectX::ComputePitch(DXGI_FORMAT_BC1_UNORM_SRGB, mipimage->width, mipimage->height, rowpitch, slicepitch);
		writer.WriteBytes(reinterpret_cast<const char*>(mipimage->pixels), slicepitch);
	}

	#ifdef _DEBUG
	idImage auditimage;
	auditimage.Read(writer.GetBuffer(), writer.GetFilledSize());
	auditimage.audit();
	#endif

	FINAL_IMAGE.length = writer.GetFilledSize();
	FINAL_IMAGE.data = (uint8_t*)writer.Finalize();

	return true;
}

#include "ResourceStructs.h"
#include "PackageMapSpec.h"
#include <fstream>

bool idImageHeaderMap_Build(idImageHeaderMap_t& HEADER_MAP, const std::string& gamedir)
{
	// TODO: The container mask is not accounted for in this
	// Will need to monitor for any edge cases of textures not loading properly
	HEADER_MAP.reserve(45000);
	std::vector<std::string> ARCHIVE_LIST = PackageMapSpec::GetPrioritizedArchiveList(gamedir, false);
	const fspath BASE_DIR = fspath(gamedir) / "base";

	ResourceEntryBuffers_t entrybuffers;

	const char* TypeString = nullptr, *NameString = nullptr;
	std::string NameStringSTD;

	for (const std::string& ARCHIVE_NAME : ARCHIVE_LIST) {
		const fspath ARCHIVE_PATH = BASE_DIR / ARCHIVE_NAME;
		ResourceArchive r;
		Read_ResourceArchive(r, ARCHIVE_PATH, RF_SkipData);

		std::ifstream DATA_READER(ARCHIVE_PATH, std::ios_base::binary);

		for (uint32_t i = 0; i < r.header.numResources; i++) {
			
			const ResourceEntry& e = r.entries[i];
			Get_EntryStrings(r, e, TypeString, NameString);

			if(strcmp(TypeString, "image") != 0)
				continue;

			if (e.uncompressedSize == 0) {
				//printf("%s\n", NameString);
				continue;
			}

			NameStringSTD = NameString;

			if (HEADER_MAP.find(NameStringSTD) != HEADER_MAP.end()) {
				continue;
			}

			ResourceEntryData_t entrydata = Get_EntryData(e, DATA_READER, entrybuffers);
			if (entrydata.returncode != EntryDataCode::OK) {
				return false;
			}

			ImageHeader imgheader;
			if(!imgheader.Read(entrydata.buffer, entrydata.length))
				return false;

			HEADER_MAP[NameStringSTD] = imgheader;
		}
		
	}

	return true;
}