#include "ModReader.h"
#include "miniz/miniz.h"
#include "entityslayer\EntityParser.h"
#include "GlobalConfig.h"
#include "atlan/AtlanLogger.h"
#include "atlan/AtlanModConfig.h"
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#define RESTYPE_NOLOAD "noload"

typedef std::filesystem::path fspath;

// Key is the start of the file path
const std::unordered_map<std::string, resourcetypeinfo_t> ValidResourceTypes = {
	{"rs_streamfile", {"rs_streamfile", rt_rs_streamfile, game_all, 0, 0}},
	{"decls",         {"rs_streamfile", rt_rs_streamfile, game_all, 0, 0, "generated/decls/"}},
	{"entityDef",     {"entityDef",     rt_entityDef,     game_darkages, 21}},
	{"logicClass",    {"logicClass",    rt_logicClass,    game_darkages, 4}},
	{"logicEntity",   {"logicEntity",   rt_logicEntity,   game_darkages, 4}},
	{"logicFX",       {"logicFX",       rt_logicFX,       game_darkages, 4}},
	{"logicLibrary",  {"logicLibrary",  rt_logicLibrary,  game_darkages, 4}},
	{"logicUIWidget", {"logicUIWidget", rt_logicUIWidget, game_darkages, 4}},
	{"mapentities",   {"mapentities",   rt_mapentities,   game_darkages, 86}},
	{"image",         {"image",         rt_image,         game_darkages, 26}},
	{"slug_font",     {"slug_font",     rt_slug_font,     game_darkages, 14}},
	{"baseModel",     {"baseModel",     rt_baseModel,     game_none, 62}},
	{"strandsHair",   {"strandsHair",   rt_strandsHair,   game_none, 48}},
	{"compfile",      {"compfile",      rt_compfile,      game_eternal, 1, 1}},
	{"binaryFile",    {"binaryFile",    rt_binaryFile,    game_eternal, 1, 1}},

	// Audio will be handled differently by the loader
	{"audio", {"audio", rt_audio, game_darkages}}
};

/*
* Check if version requirement is met. If it isn't, we should skip reading this mod
*/
bool ModReader_HasRequiredVersion(ModDef& moddef, const AtlanModConfig& cfg) {
	if (MOD_LOADER_VERSION < cfg.requiredVersion) {
		atlog("ERROR: Mod requires Mod Loader Version %d or greater. (Your version is %d)", cfg.requiredVersion, MOD_LOADER_VERSION);
		return false;
	}
	return true;
}

bool ModReader_ValidatePath(ModFile& modfile, const AtlanModConfig& cfg, std::string* EncodingInfo = nullptr) {
	std::string TypeString, NameString;
	cfg.GetNormalizedName(modfile.realPath, TypeString, NameString);

	if (TypeString.length() == 0) {
		atlog("ERROR: Missing resource type string for file %s", modfile.realPath.c_str());
		return false;
	}

	if (TypeString == RESTYPE_NOLOAD)
		return false;

	/*
	* Map the typeString to a resource type
	*/
	{
		const auto& iter = ValidResourceTypes.find(TypeString);
		if (iter == ValidResourceTypes.end()) {
			atlog("ERROR: Unsupported resource type for file \"%s\"", modfile.realPath.c_str());
			return false;
		}

		gamebit_t local_game = g_game;
		if(~iter->second.gamebits & local_game) {
			atlog("ERROR: Disabled resource type for file \"%s\"", modfile.realPath.c_str());
			return false;
		}

		modfile.resourceVersion = local_game == game_darkages ? iter->second.version_da : iter->second.version_et;
		modfile.typestring = iter->second.typestring;
		modfile.typeenum = iter->second.typeenum;
		modfile.assetPath = iter->second.namestart;
	}

	/*
	* Create the resource string - Beginning after the resource type
	*/
	bool hasBadChars = false;
	char* nameStart = NameString.data();
	char* nameEnd = nameStart;
	while (*nameEnd) {

		char c = *nameEnd;

		// Delimiter for explicit image encoding information
		// For unzipped mods, we want to relay the encoding information back to the calling function
		if ('~' == c) {
			if (EncodingInfo) {
				*EncodingInfo = NameString.substr(nameEnd - nameStart + 1);
			}

			break;
		}

		if ('.' == c && modfile.typeenum & rtc_no_extension) {
			break;
		}
		// Capital letters in file names can cause asset instability
		// in idStudio. Probably best to enforce the all-lowercase standard
		else if (c <= 'Z' && c >= 'A') {
			*nameEnd += 32;
			hasBadChars = true;
		}
		else if (' ' == c) {
			*nameEnd = '_';
			hasBadChars = true;
		}
		nameEnd++;
	}

	// For Audio: Trim the asset path down to just the sample ID
	if (modfile.typeenum & rtc_last_number) {

		char* iter = nameEnd - 1;
		while (iter >= nameStart) {
			if (*iter > '9' || *iter < '0') {
				nameStart = iter + 1;
				break;
			}
			iter--;
		}

		if (nameStart >= nameEnd || *nameStart > '9' || *nameStart < '0') {
			atlog("ERROR: File %s requires a number at the end of it's filename", modfile.realPath.c_str());
			return false;
		}
	}
	else {
		if (hasBadChars) {
			atlog("WARNING: Fixed capital letters or other bad characters in path %s", modfile.realPath.c_str());
		}
		if (nameStart >= nameEnd) {
			atlog("ERROR: File %s has an invalid name", modfile.realPath.c_str());
			return false;
		}
	}

	modfile.assetPath.append(nameStart, nameEnd);

	return true;
}

#if 0
bool ModReader_ValidateData(ModFile& modfile, bool AllowUnserialized) {
	return true;
}
#endif

void ModReader_ConfirmModFile(ModDef& moddef, ModFile& modfile) {
	atlog_file("OK: %s --> %s", modfile.realPath.c_str(), modfile.assetPath.c_str());

	/*
	* Finish processing the file
	*/
	modfile.parentMod = &moddef;
	moddef.modFiles.push_back(modfile);
}

void ModReader_ExtractConfigData(const AtlanModConfig& modconfig, GlobalConfig_t& globalconfig) {
	globalconfig.mapspec.insert(globalconfig.mapspec.end(), modconfig.listmapspec, modconfig.listmapspec + modconfig.nummapspec);

	for (int i = 0; i < modconfig.numEdits; i++) {
		AtlanModConfig::namedlist_t& editlist = modconfig.listedits[i];
		std::string fullname = "generated/buildgame/";
		fullname.append(editlist.listname);
		fullname.append(".mapresources");
		GlobalConfig_t::mapres_t& g = globalconfig.mapresinfo[fullname];

		for (int sindex = 0; sindex < editlist.numentries; sindex++) {
			const std::string& key = editlist.entries[sindex];

			if (key == "LOAD_ALL") {
				g.LoadAll = true;
				continue;
			}

			bool foundList = false;
			for (int k = 0; k < modconfig.numAssetlists; k++) {
				AtlanModConfig::namedlist_t& alist = modconfig.listassets[k];
				if (alist.listname == key) {
					g.entries.insert(g.entries.end(), alist.entries, alist.entries + alist.numentries);
					foundList = true;
					break;
				}
			}

			if (!foundList)
				atlog("CONFIG ERROR: Unknown asset list named %s ", key.c_str());
		}
	}
}

struct idUnzippedImageJob {
	fspath filepath;
	std::string assetpath;
	std::string encodinginfo;
	std::string realPath;
	int ModFileIndex = 0;
};

struct idUnzippedImageJobList {
	std::vector<idUnzippedImageJob> jobs;
	idImageEncodingContext* context = nullptr;
	ModDef* mod = nullptr;
};

std::mutex g_getunzippedjob_mutex;

void UnzippedImageEncodingThread(idUnzippedImageJobList* joblist) {
	size_t CurrentIndex;
	idUnzippedImageJob CurrentJob;
	idImageEncodingResults ImageOutput;
	std::string OutputLog;

	if (!idImageEncodingContext::COMThreadInit())
		return;

	while (1) {

		// Fetch next image job
		{
			std::lock_guard<std::mutex> lock(g_getunzippedjob_mutex);
			if (joblist->jobs.size() == 0)
				break;

			CurrentJob = joblist->jobs.back();
			joblist->jobs.pop_back();
			CurrentIndex = joblist->jobs.size();
		}

		OutputLog = CurrentJob.realPath;
		OutputLog.append(" (");
		OutputLog.append(CurrentJob.assetpath);
		OutputLog.append(" )\n");
		bool success = joblist->context->EncodeImage(CurrentJob.assetpath, CurrentIndex,
			CurrentJob.encodinginfo, CurrentJob.filepath.c_str(), ImageOutput, OutputLog);
		atlog_raw(OutputLog.data(), OutputLog.length(), false);
		if (!success)
			continue;

		// Transfer ownership of output buffer to the mod file
		ModFile& modfile = joblist->mod->modFiles[CurrentJob.ModFileIndex];
		modfile.dataBuffer = (char*)ImageOutput.buffer;
		modfile.dataLength = ImageOutput.file_length;
		modfile.ownsData = true;
		ImageOutput.buffer = nullptr;
		ImageOutput.buffer_max = 0;
		ImageOutput.file_length = 0;

	}

	idImageEncodingContext::COMThreadRelease();
}

void ModReader::ReadLooseModv2(ModDef& moddef, const fspath modsfolder, const fspath& gamedir, GlobalConfig_t& globalconfig)
{
	using namespace std::filesystem;

	const size_t modsfolder_length = modsfolder.string().length();
	std::vector<fspath> ModFilePaths;
	AtlanModConfig cfg;

	ModFilePaths.reserve(20);

	moddef.modName = "[Unzipped] ";
	moddef.modName.append(modsfolder.stem().string());
	moddef.IsUnzipped = true;
	moddef.ActiveZip = false;

	atlog("\n\nReading %s\n---", moddef.modName.c_str());

	// TODO: This loop is mostly copied from the mod packager. Could we create a shared function for it?
	for (const directory_entry& entry : recursive_directory_iterator(modsfolder)) {
		if(entry.is_directory())
			continue;

		const fspath entrypath = entry.path();
		const fspath extension = entrypath.extension();
		if(extension == L".zip" || extension == L".ZIP")
			continue;

		if (entrypath.filename() == CFG_NAME) {

			// Edge Case: Mod config is in a subfolder - ignore it
			if (entrypath.parent_path() != modsfolder) {
				atlog("Ignoring " CFG_NAME " inside a subfolder");
				continue;
			}

			atlog("Found " CFG_NAME);
			cfg.TryRead(entrypath.string());
			ModReader_ExtractConfigData(cfg, globalconfig);
			continue;
		}

		ModFilePaths.push_back(entrypath);
	}

	moddef.loadPriority = -999;
	if (!ModReader_HasRequiredVersion(moddef, cfg)) {
		return;
	}

	atlog("Found %llu Unzipped Mod Files", ModFilePaths.size());

	idUnzippedImageJobList ImageJobs;

	for (const fspath& FilePath : ModFilePaths) {
		ModFile modfile;
		modfile.realPath = FilePath.string().substr(modsfolder_length + 1);

		std::string EncodingInfo;
		if (!ModReader_ValidatePath(modfile, cfg, &EncodingInfo)) {
			continue;
		}

		// For simplicity, assume any unzipped image file is unencoded
		// (This beats reading the image file, checking if it's an Atlan Image,
		//    and then having the encoder re-read it when it's inevitably not an Atlan Image.
		//	  Don't want to bother developing an Encode-From-Memory pipeline
		//    just for this edge case that shouldn't reasonably happen)
		if (modfile.typeenum == rt_image) {
			
			ImageJobs.jobs.emplace_back();
			
			idUnzippedImageJob& job = ImageJobs.jobs.back();
			job.filepath = FilePath;
			job.realPath = modfile.realPath;
			job.assetpath = modfile.assetPath;
			job.encodinginfo = EncodingInfo;
			job.ModFileIndex = (int)moddef.modFiles.size();
		}
		else {
			std::ifstream filereader(FilePath, std::ios_base::binary);
			if (!filereader.good()) {
				atlog("ERROR: Failed to open file %s", modfile.realPath.c_str());
				continue;
			}

			filereader.seekg(0, std::ios_base::end);
			modfile.dataLength = filereader.tellg();
			modfile.dataBuffer = new char[modfile.dataLength];
			filereader.seekg(0, std::ios_base::beg);
			filereader.read((char*)modfile.dataBuffer, modfile.dataLength);
			filereader.close();
		}
		ModReader_ConfirmModFile(moddef, modfile);
	}

	if(ImageJobs.jobs.size()) {
		const int NUMTHREADS = 8;
		std::thread threadpool[NUMTHREADS];

		atlog("Running %llu Image Encoding Jobs on %d threads", ImageJobs.jobs.size(), NUMTHREADS);

		idImageEncodingContext ImageEncoder;
		atlog("Initializing Image Encoder with directory %ls", gamedir.c_str());

		std::vector<std::string> temp_assetpaths;
		temp_assetpaths.reserve(ImageJobs.jobs.size());
		for (const idUnzippedImageJob& j : ImageJobs.jobs) {
			temp_assetpaths.push_back(j.assetpath);
		}

		if(!ImageEncoder.InitializeContext(gamedir.string(), 3, temp_assetpaths.data(), temp_assetpaths.size()))
			return;

		ImageJobs.context = &ImageEncoder;
		ImageJobs.mod = &moddef;

		for (int i = 0; i < NUMTHREADS; i++)
			threadpool[i] = std::thread(UnzippedImageEncodingThread, &ImageJobs);
		for (int i = 0; i < NUMTHREADS; i++)
			threadpool[i].join();
	}
}

bool ReadZipMod_Internal(mz_zip_archive* zptr, ModDef& readto, GlobalConfig_t& globalconfig);

void ModReader::ReadZipMod(ModDef& mod, const fspath& zipPath, GlobalConfig_t& globalconfig)
{
	atlog("\n\nReading %ls\n---", zipPath.filename().c_str());

	// Open the zip file
	mz_zip_archive* zptr = &mod.zipfile;
	mz_zip_zero_struct(zptr);
	if (!mz_zip_reader_init_file(zptr, zipPath.string().c_str(), 0))
	{
		atlog("ERROR: Failed to open zip file");
		return;
	}

	mod.modName = zipPath.stem().string();
	mod.ActiveZip = ReadZipMod_Internal(zptr, mod, globalconfig);
	if (!mod.ActiveZip) {
		mz_zip_reader_end(zptr);
	}
}

// If return value is true, we should keep the zip file alive after reading
// to enable just-in-time loading
bool ReadZipMod_Internal(mz_zip_archive* zptr, ModDef& mod, GlobalConfig_t& globalconfig)
{
	bool KEEP_ZIP_ALIVE = false;

	/*
	* Read config files if they exist
	*/
	AtlanModConfig cfg;
	{
		char* dataBuffer = nullptr;
		size_t dataLength = 0;

		/*
		* Extract file from zip
		*/
		dataBuffer = (char*)mz_zip_reader_extract_file_to_heap(zptr, CFG_NAME, &dataLength, 0);
		if (!dataBuffer) {
			atlog("WARNING: Could not find " CFG_NAME);
		}
		else {
			atlog("Found " CFG_NAME);
			cfg.TryRead(dataBuffer, dataLength);
			ModReader_ExtractConfigData(cfg, globalconfig);
			delete[] dataBuffer;
		}
	}

	mod.loadPriority = cfg.loadPriority;
	if (!ModReader_HasRequiredVersion(mod, cfg)) {
		return false;
	}

	/*
	* Read all mod files
	*/
	uint32_t fileCount = mz_zip_reader_get_num_files(zptr);
	size_t nameBufferMax = 512;
	char* ZipNameBuffer = new char[nameBufferMax];
	for (uint32_t i = 0; i < fileCount; i++) {
		if(mz_zip_reader_is_file_a_directory(zptr, i))
			continue;

		ModFile modfile;

		/*
		* Get the file name
		*/
		{
			uint32_t nameLength = mz_zip_reader_get_filename(zptr, i, nullptr, 0);
			if (nameLength > nameBufferMax) {
				delete[] ZipNameBuffer;
				ZipNameBuffer = new char[nameLength];
				nameBufferMax = nameLength;
			}
			mz_zip_reader_get_filename(zptr, i, ZipNameBuffer, nameLength);
			nameLength--; // The null char is included in the name length...(but data length is correct and without a nullchar)
			modfile.realPath = std::string(ZipNameBuffer, nameLength);
		}

		/*
		* If this is a pre-parsed config file, skip it
		*/
		if(modfile.realPath == CFG_NAME)
			continue;

		if (!ModReader_ValidatePath(modfile, cfg)) {
			continue;
		}

		/*
		* Read the mod data
		*/

		if (modfile.typeenum & rtc_deferload) {
			KEEP_ZIP_ALIVE = true;
		}
		else {
			void* dataBuffer = nullptr;
			size_t dataLength = 0;
			dataBuffer = mz_zip_reader_extract_to_heap(zptr, i, &dataLength, 0);
			if (!dataBuffer) {
				atlog("ERROR: Failed to extract file %s", modfile.realPath.c_str());
				continue;
			}
			modfile.dataBuffer = (char*)dataBuffer;
			modfile.dataLength = dataLength;
		}

		ModReader_ConfirmModFile(mod, modfile);
	}

	delete[] ZipNameBuffer;
	return KEEP_ZIP_ALIVE;
}

bool ModReader::LoadModData(ModFile& modfile, JustInTimeBuffer_t& buffer) {
	if(modfile.dataBuffer)
		return true;

	if(!modfile.parentMod->ActiveZip)
		return false;
	mz_zip_archive* zptr = &modfile.parentMod->zipfile;
	
	// todo should store index in modfile
	int zipindex = mz_zip_reader_locate_file(zptr, modfile.realPath.c_str(), nullptr, 0);
	if(zipindex == -1)
		return false;

	mz_zip_archive_file_stat fstats;
	if(!mz_zip_reader_file_stat(zptr, zipindex, &fstats))
		return false;


	if (buffer.maxcapacity < fstats.m_uncomp_size) {
		delete[] buffer.buffer;

		buffer.maxcapacity = fstats.m_uncomp_size + 2048;
		buffer.buffer = new char[buffer.maxcapacity];
	}
	buffer.filelength = fstats.m_uncomp_size;

	if(!mz_zip_reader_extract_to_mem_no_alloc(zptr, zipindex, buffer.buffer, buffer.maxcapacity, 0, NULL, 0))
		return false;

	modfile.ownsData = false;
	modfile.dataBuffer = buffer.buffer;
	modfile.dataLength = buffer.filelength;

	return true;
}