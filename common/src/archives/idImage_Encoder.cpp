#include "io/BinaryReader.h"
#include "ResourceStructs.h"
#include <mutex>

#include <Windows.h>
#include <d3d11.h>
#include "DirectXTex/DirectXTex.h"

#include "idImage.h"
#include "atlan/AtlanLogger.h"
#include "entityslayer/Oodle.h"
#include "io/BinaryWriter.h"

bool idImageEncodingContext::COMThreadInit() {
	HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	if (FAILED(result)) {
		atlog("FATAL ERROR: Failed to initialize COM for image encoder (Code: %d)", result);
		return false;
	}
	return true;
}

// Uninitializing during destruction of the image encoder appears to free up persistent resources
// used by DirectXTex, causing a crash when attempting to use 
// another image encoder after one is already released
// Hence, we really should ensure that COM is being initialized/uninitialized once per thread
void idImageEncodingContext::COMThreadRelease() {
	CoUninitialize();
}

bool idImageEncodingContext::InitializeContext(const std::string& gamedir, int in_CompressionLevel, const std::string* in_AssetPaths, size_t num_AssetPaths) {

	m_CompressionLevel = in_CompressionLevel;
		
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
		atlog("FATAL ERROR: Failed to create Direct3D context for image encoder (Code: %d)", result);
		return false;
	}

	/*
	* Step 3: Build the Image Header Map
	*/

	if (idImageHeaderMap_Build(gamedir, in_AssetPaths, num_AssetPaths, m_querylist) == false) {
		atlog("FATAL ERROR: Failed to create ImageHeaderMap for image encoder.\n"
			  "Please ensure AtlanModPackager and AtlanModLoader are placed in your game directory");
		return false;
	}

	m_initialized = true;
	return true;
}

bool idImageEncodingContext::Release() {
	if(!m_initialized)
		return false;

	m_context->ClearState();
	m_context->Flush();
	m_context->Release();
	m_device->Release();

	m_context = nullptr;
	m_device = nullptr;

	m_initialized = false;

	delete[] m_querylist;

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

bool BuildOriginalImageHeader(const std::string& AssetPath, const std::string& EncodingInfo, ImageHeader& header, std::string& OutputLog)
{
	header.DefaultInitialize();

	if (EncodingInfo.length()) {

		const char* c = EncodingInfo.data();

		OutputLog.append("   Approach: Explicit Encoding ");
		OutputLog.append(EncodingInfo);
		OutputLog.append("\n");

		// Format is "FORMAT_ENUM~MATERIAL_ENUM"
		std::string inputstring;
		while (*c) {
			if ('~' == *c) {
				size_t delimindex = c - EncodingInfo.data();
				inputstring = EncodingInfo.substr(0, delimindex);

				if (!textureFormat_fromstring(inputstring, header.textureFormat)) {
					return false;
				}

				inputstring = EncodingInfo.substr(delimindex + 1);
				return textureMaterialKind_fromstring(inputstring, header.textureMaterialKind);
			}
			c++;
		}
		return false;
	}
	else {
		OutputLog.append("   Approach: Extension Inference\n");
		idImageExtensionData ext;
		ext.FromAssetPath(AssetPath);

		if (ext.m_material == TMK_NONE)
			return false;

		if (ext.m_format == FMT_NONE) {
			ext.InferFormatFromMaterial();

			if (ext.m_format == FMT_NONE)
				return false;
		}

		header.textureMaterialKind = ext.m_material;
		header.textureFormat = ext.m_format;
		header.noMips = ext.m_nomips;
		header.prefiltermips = ext.m_prefiltermips;
		header.streamed = ext.m_streamed;

		return true;
	}
}

bool idImageEncodingContext::EncodeImage(const std::string& AssetPath, size_t JobIndex, const std::string& EncodingInfo, const wchar_t* FilePath, idImageEncodingResults& FINAL_IMAGE, std::string& OutputLog) const
{
	/*
	* Step 1: Locate the vanilla ImageHeader for this file
	*/

	ImageHeader header;
	DXGI_FORMAT dxgiFormat;
	{
		idImageEncodingQuery& query = m_querylist[JobIndex];
		if (!query.found) {
			
			if (BuildOriginalImageHeader(AssetPath, EncodingInfo, header, OutputLog)) {
				OutputLog.append("   Non-Vanilla Image Recognized ( ");
				OutputLog.append(textureFormat_tostring(header.textureFormat));
				OutputLog.append(", ");
				OutputLog.append(textureMaterialKind_tostring(header.textureMaterialKind));
				OutputLog.append(")\n   (If this should be a vanilla image, check your alias for a typo)\n");
			}
			else {
				OutputLog.append("   ERROR: This image file does not exist in the vanilla game, and there is not enough "
					"information to encode it correctly!\n   (If this should be a vanilla image, check your alias for a typo)\n");
				return false;
			}
		}
		else {
			header = query.header;
		}
	}

	/*
	* Step 2: Ensure this image type is supported
	*/

	if (header.version < 23 || header.version > 26) {
		OutputLog.append("   ERROR: Unsupported Image File Version\n");
		return false;
	}
	if (header.textureType != TT_2D) {
		OutputLog.append("   ERROR: Only 2D textures are supported\n");
		return false;
	}

	dxgiFormat = idFormat_To_DXGI(header.textureFormat);
	if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
		OutputLog.append("   ERROR: Unsupported texture format\n");
		return false;
	}
	bool IsSRGB = header.textureFormat == FMT_BC1_SRGB || header.textureFormat == FMT_BC3_SRGB || header.textureFormat == FMT_BC7_SRGB;

	/*
	* Step 3: Perform the encoding
	*/

	DirectX::ScratchImage image;
	DirectX::ScratchImage TEMP_IMAGE;
	
	HRESULT result = DirectX::LoadFromWICFile(FilePath, IsSRGB ? DirectX::WIC_FLAGS_DEFAULT_SRGB : DirectX::WIC_FLAGS_NONE, nullptr, image);
	if (FAILED(result)) {
		OutputLog.append("   ERROR: Failed to read raw image file into ScratchImage (Error Code: ");
		OutputLog.append(std::to_string(result));
		OutputLog.append(")\n");
		return false;
	}

	//printf("Generating Mips...");
	result = DirectX::GenerateMipMaps(*image.GetImage(0, 0, 0), DirectX::TEX_FILTER_DEFAULT, 0, TEMP_IMAGE);
	if (FAILED(result)) {
		OutputLog.append("   ERROR: Failed to generate Mips (Error Code: ");
		OutputLog.append(std::to_string(result));
		OutputLog.append(")\n");
		return false;
	}

	image.Release(); // Free the un-mipped image

	//printf("\rCompressing Texture...");
	if (DXGI_UseGpuEncoding(dxgiFormat)) {
		// Need this mutex to prevent nullptr dereferences or DXGI_ERROR_DEVICE_REMOVED when encoding multiple
		// images on the GPU simultaneously. Graphics Device context is not thread-safe
		// The multithreading flag DirectX::TEX_COMPRESS_PARALLEL has been removed because we're already
		// maxing out the CPU with 8 parallel jobs. So removing this flag actually makes the encoder ~2% faster
		static std::mutex g_gpu_encoding_mutex;
		std::lock_guard<std::mutex> lock(g_gpu_encoding_mutex);
		result = Compress(m_device, TEMP_IMAGE.GetImages(), TEMP_IMAGE.GetImageCount(), TEMP_IMAGE.GetMetadata(),
			dxgiFormat, 
			(IsSRGB ? DirectX::TEX_COMPRESS_SRGB : DirectX::TEX_COMPRESS_DEFAULT), 
			DirectX::TEX_ALPHA_WEIGHT_DEFAULT, image);
	}
	else {
		result = Compress(TEMP_IMAGE.GetImages(), TEMP_IMAGE.GetImageCount(), TEMP_IMAGE.GetMetadata(),
			dxgiFormat, 
			(IsSRGB ? DirectX::TEX_COMPRESS_SRGB : DirectX::TEX_COMPRESS_DEFAULT),
			DirectX::TEX_THRESHOLD_DEFAULT, image);
	}
	if (FAILED(result)) {
		OutputLog.append("   ERROR: Failed to encode texture (Error Code: ");
		OutputLog.append(std::to_string(result));
		OutputLog.append(")\n");

		if (result == DXGI_ERROR_DEVICE_REMOVED) {
			HRESULT RemoveReason = m_device->GetDeviceRemovedReason();
			OutputLog.append("   DXGI_ERROR_DEVICE_REMOVED: Reason Code ");
			OutputLog.append(std::to_string(RemoveReason));
			OutputLog.push_back('\n');
		}
		return false;
	}

	//printf("\rCreating output file...");

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
		m.mipPixelWidth = (u32)mipImage->width;
		m.mipPixelHeight = (u32)mipImage->height;
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
	header.singleStream = 0;

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
	writer.WriteBytes("ATIM", 4);
	writer << decltype(idAtlanImage::version)(2) 
		<< decltype(idAtlanImage::bimversion)(header.version)
		<< decltype(idAtlanImage::singlestream)(0)
		<< decltype(idAtlanImage::streamdbmips)(header.streamDBMipCount)
		<< decltype(idAtlanImage::prefetch_farmhash)(0)
		<< (uint64_t)(header.CalcHeaderSize());
	
	// Write the resources entry
	writer.pushSizeStack();
	writer.WriteBytes(header.magic, sizeof(header.magic));
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

		// Some jank to avoid needing to copy the compressed data into the writer
		// from a separate buffer
		writer.EnsureAvailable(m.decompressedSize);
		char* writeTo = writer.GetEditableNext();
		
		int OodleResult = Oodle::CompressBuffer((char*)mipimage->pixels, m.decompressedSize, writeTo, m_CompressionLevel);
		if (OodleResult == 0) {
			OutputLog.append("   ERROR: Failed to Oodle compress mip level ");
			OutputLog.append(std::to_string(i));
			OutputLog.append("\n");
			return false;
		}
		m.compressedSize = (u32)OodleResult;
		m.cumulativeSizeStreamDB = cumulativeSum;
		cumulativeSum += m.compressedSize;
		writer.AddBytes(m.compressedSize);
	}

	FINAL_IMAGE.file_length = writer.GetFilledSize();
	FINAL_IMAGE.buffer_max = writer.GetMaxCapacity();
	FINAL_IMAGE.buffer = (uint8_t*)writer.Finalize();

	// Copy in the completed mipinfo
	uint8_t* mipinfo_location = FINAL_IMAGE.buffer + POSITION_MIPINFO;
	memcpy(mipinfo_location, mipdata.data(), header.mipCount * sizeof(ImageMipInfo));

	if(!idAtlanImage::Validate(FINAL_IMAGE.buffer, FINAL_IMAGE.file_length))
		OutputLog.append("   ERROR: Image failed final validation\n");

	//printf("\r");
	return true;
}

bool idAtlanImage::Validate(const uint8_t* data, size_t length) {
	
	idAtlanImage testatlan;
	if(testatlan.Read(data, length) == false)
		return false;

	idImage auditImage;
	return auditImage.Read(testatlan.binaryblob, testatlan.entry_length, true);
};

bool idAtlanImage::Read(const uint8_t* data, size_t length) {
	BinaryReader reader((const char*)data, length);

	const char* magic_bytes = nullptr;
	check(reader.ReadBytes(magic_bytes, 4));
	check(memcmp(magic_bytes, "ATIM", 4) == 0);

	check(reader.ReadLE(version));
	check(version == 2);

	check(reader.ReadLE(bimversion));
	check(bimversion >= 23 && bimversion <= 26);

	check(reader.ReadLE(singlestream));
	check(reader.ReadLE(streamdbmips));
	check(reader.ReadLE(prefetch_farmhash));
	check(singlestream == 0 && prefetch_farmhash == 0);

	uint64_t header_size;
	check(reader.ReadLE(header_size));
	check(reader.ReadLE(entry_length));
	
	size_t blobsize = reader.GetRemaining();
	reader.ReadBytes(binaryblob, blobsize);

	check(entry_length >= header_size + sizeof(ImageMipInfo) * streamdbmips);
	mipinfos = (ImageMipInfo*)(binaryblob + header_size);

	uint32_t testsum = entry_length;
	for (int i = 0; i < streamdbmips; i++) {
		check(mipinfos[i].mipLevel == i);
		check(mipinfos[i].mipSlice == 0);
		
		testsum += mipinfos[i].compressedSize;
	}

	check(testsum == blobsize);

	return true;
}


#include "PackageMapSpec.h"

bool idImageHeaderMap_Build(const std::string& gamedir, const std::string* AssetPaths, const size_t NumAssetPaths, idImageEncodingQuery*& out_results)
{
	std::unordered_map<std::string, int> IndexMap;
	IndexMap.reserve(NumAssetPaths);
	for(size_t i = 0; i < NumAssetPaths; i++)
		IndexMap[AssetPaths[i]] = i;

	if(out_results)
		delete[] out_results;
	out_results = new idImageEncodingQuery[NumAssetPaths];

	idcl::ArchiveIterator iter(gamedir + "/base/packagemapspec.json", false);
	iter.typefilter = "image";


	ResourceEntryBuffers_t entrybuffers;

	for (const ResourceEntry& e : iter) {
		if (e.uncompressedSize == 0) {
			//printf("%s\n", NameString);
			continue;
		}

		const auto& IndexIter = IndexMap.find(iter.namestring);
		if(IndexIter == IndexMap.end())
			continue;
		size_t Index = IndexIter->second;
		if(out_results[Index].found)
			continue;

		ResourceEntryData_t entrydata = Get_EntryData(e, iter.archive.filehandle, entrybuffers);
		if (entrydata.returncode != EntryDataCode::OK) {
			return false;
		}
;
		if(!out_results[Index].header.Read(entrydata.buffer, entrydata.length))
			return false;
		out_results[Index].found = true;

		IndexMap.erase(IndexIter);
		if(IndexMap.size() == 0)
			return true;
	}

	// Failing to find all images is acceptable, in the case of new images
	return true;
}