#include "io/BinaryReader.h"

#include <Windows.h>
#include <d3d11.h>
#include "DirectXTex/DirectXTex.h"

#include "idImage.h"
#include "atlan/AtlanLogger.h"
#include "entityslayer/Oodle.h"
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

		case FMT_BC1:              return DXGI_FORMAT_BC1_UNORM;
		case FMT_BC1_SRGB: 		   return DXGI_FORMAT_BC1_UNORM_SRGB;

		// TODO: Monitor if this is correct
		case FMT_BC1_ZERO_ALPHA:   return DXGI_FORMAT_BC1_UNORM;

		case FMT_BC3: 			   return DXGI_FORMAT_BC3_UNORM;
		case FMT_BC3_SRGB:		   return DXGI_FORMAT_BC3_UNORM_SRGB;
		case FMT_BC4:			   return DXGI_FORMAT_BC4_UNORM;
		case FMT_BC5:			   return DXGI_FORMAT_BC5_UNORM;
		case FMT_BC6H_UF16:		   return DXGI_FORMAT_BC6H_UF16;
		case FMT_BC6H_SF16:		   return DXGI_FORMAT_BC6H_SF16;
		case FMT_BC7:			   return DXGI_FORMAT_BC7_UNORM;
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
			atlog << "ERROR: Could not find name in ImageHeaderMap (" << AssetPath << ")\n";
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

	// Ask Tjoener if you want to know why we do this
	// tl;dr is these variables are only necessary for id's method of encoding images
	// (or something like that)
	header.albedoSpecularBias = 0.0f;
	header.albedoSpecularScale = 1.0f;

	// TODO: Should we log if these change from their vanilla values?
	header.pixelWidth = (u32)image.GetImage(0, 0, 0)->width;
	header.pixelHeight = (u32)image.GetImage(0, 0, 0)->height;
	header.mipCount = (u32)image.GetImageCount();
	header.streamDBMipCount = 0;

	std::vector<ImageMipInfo> mipdata;
	mipdata.resize(header.mipCount);

	// Pass #1: Calculate everything except for the
	// compressed sizes and cumulativeStreamDbSizes (which uses the compressed sizes)
	for (uint32_t i = 0; i < header.mipCount; i++) {
		ImageMipInfo& m = mipdata[i];
		const DirectX::Image* mipImage = image.GetImage(i, 0, 0);
		m.mipLevel = i;
		m.mipSlice = 0;
		m.mipPixelWidth = mipImage->width;
		m.mipPixelHeight = mipImage->height;
		m.mipPixelDepth = 1;

		size_t rowpitch = 0, slicepitch = 0;
		DirectX::ComputePitch(dxgiFormat, mipImage->width, mipImage->height, rowpitch, slicepitch);
		m.decompressedSize = (u32)slicepitch;

		if (m.mipPixelWidth <= 32 && m.mipPixelHeight <= 32) {
			m.flagIsCompressed = 0;
			m.compressedSize = m.decompressedSize;
		}
		else {
			m.flagIsCompressed = 1;
			header.streamDBMipCount++;
		}
	}

	header.streamed = (header.streamDBMipCount > 0);

	/*
	* STEP 5: Write the file 
	*/

	BinaryWriter writer;

	// Transfer ownership of the buffer to the writer
	// So we can re-use the allocated memory
	writer.AcquireBuffer((char*)FINAL_IMAGE.buffer, FINAL_IMAGE.buffer_max);
	FINAL_IMAGE.buffer = nullptr; 
	FINAL_IMAGE.buffer_max = 0; 
	FINAL_IMAGE.file_length = 0;
	writer.EnsureMaxCapacity(image.GetPixelsSize() + 5000);

	// Write the Atlan Image header
	idAtlanImage atlanheader;
	memcpy(atlanheader.magic, "ATIM", 4);
	atlanheader.version = 1;
	atlanheader.prefetch_farmhash = 0;
	atlanheader.singlestream = 0;
	atlanheader.streamdbmips = header.streamDBMipCount;
	writer.WriteBytes((const char*)&atlanheader, 32); // IMPORTANT: Size of the above fields
	
	// Write the resources entry
	writer.pushSizeStack();
	writer.WriteBytes(header.magic, 3);
	writer << header.version 
		<< header.textureType 
		<< header.textureMaterialKind
		<< header.pixelWidth << header.pixelHeight << header.depth
		<< header.mipCount << header.unkFloat1
		<< header.albedoSpecularBias << header.albedoSpecularScale << header.padding1
		<< header.textureFormat << header.always8 << header.padding2 << header.padding3
		<< header.streamed << header.singleStream << header.noMips << header.fftBloom;
	if(header.version > 23)
		writer << header.prefiltermips;
	writer << header.streamDBMipCount;

	// Reserve space for the mipinfo array. We'll copy it in later
	// once we've fully populated all fields
	const size_t POSITION_MIPINFO = writer.GetPosition();
	writer.AddBytes(header.mipCount * sizeof(ImageMipInfo));

	// Write the resource entry mips, completing that part of the file
	u32 cumulativeSum = 32 * header.mipCount + 64;
	for(uint32_t i = header.streamDBMipCount; i < header.mipCount; i++) {
		writer.WriteBytes((char*)image.GetImage(i, 0, 0)->pixels, mipdata[i].compressedSize);
		mipdata[i].cumulativeSizeStreamDB = cumulativeSum;
		cumulativeSum += mipdata[i].compressedSize;
	}
	writer.popSizeStack();

	// Now we compress and write each streamdb mip data
	cumulativeSum = 0;
	for(uint32_t i = 0; i < header.streamDBMipCount; i++) {

		ImageMipInfo& m = mipdata[i];
		const DirectX::Image* mipimage = image.GetImage(i, 0, 0);

		writer << m.mipLevel << m.mipSlice;
		writer.pushSizeStack();

		// Some jank to avoid needing to copy the compressed data into the writer
		// from a separate buffer
		writer.EnsureAvailable(m.decompressedSize);
		char* writeTo = writer.GetEditableNext();
		size_t out_compressedSize = 0;

		bool OodleResult = Oodle::CompressBuffer((char*)mipimage->pixels, m.decompressedSize, writeTo, out_compressedSize);

		if (!OodleResult) {
			atlog << "ERROR: Failed to Oodle compress mip level " << i << "\n";
			return false;
		}
		m.compressedSize = (u32)out_compressedSize;
		m.cumulativeSizeStreamDB = cumulativeSum;
		cumulativeSum += m.compressedSize;
		writer.AddBytes(m.compressedSize);
		writer.popSizeStack();
	}

	FINAL_IMAGE.file_length = writer.GetFilledSize();
	FINAL_IMAGE.buffer_max = writer.GetMaxCapacity();
	FINAL_IMAGE.buffer = (uint8_t*)writer.Finalize();

	// Copy in the completed mipinfo
	uint8_t* mipinfo_location = FINAL_IMAGE.buffer + POSITION_MIPINFO;
	memcpy(mipinfo_location, mipdata.data(), header.mipCount * sizeof(ImageMipInfo));

	if(!idAtlanImage::Validate(FINAL_IMAGE.buffer, FINAL_IMAGE.file_length))
		atlog << "ERROR: Imaged failed final validation\n";

	return true;
}

bool idAtlanImage::Validate(const uint8_t* data, size_t length) {
	
	BinaryReader reader((const char*)data, length);
	int version = 0;
	
	const char* bytes = nullptr;
	
	if(!reader.ReadBytes(bytes, 4))
		return false;
	if(memcmp(bytes, "ATIM", 4) != 0)
		return false;

	reader.ReadLE(version);
	if(version != 1)
		return false;

	reader.GoRight(16);

	uint64_t streamdbmips = 0;
	uint32_t entry_length = 0;
	reader.ReadLE(streamdbmips);
	if(!reader.ReadLE(entry_length))
		return false;

	idImage auditImage;
	if(!auditImage.Read(reader.GetNext(), entry_length))
		return false;
	if(!auditImage.audit())
		return false;

	if(!reader.GoRight(entry_length))
		return false;

	for (uint64_t i = 0; i < streamdbmips; i++) {
		uint32_t mipLevel = 0, mipSlice = 0;
		uint32_t mipLength = 0;

		reader.ReadLE(mipLevel);
		reader.ReadLE(mipSlice);
		if(!reader.ReadLE(mipLength))
			return false;
		if(!reader.GoRight(mipLength))
			return false;
	}

	return reader.ReachedEOF();
};

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

	return HEADER_MAP.size() > 0;
}