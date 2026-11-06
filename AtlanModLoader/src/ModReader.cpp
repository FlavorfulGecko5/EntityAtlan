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
	{"image",         {"image",         rt_image,         game_all, 26, 21}},
	{"slug_font",     {"slug_font",     rt_slug_font,     game_darkages, 14}},
	{"baseModel",     {"baseModel",     rt_generic_atdb,  game_all, 62, 31}},
	{"strandsHair",   {"strandsHair",   rt_generic_atdb,  game_darkages, 48}},
	{"compfile",      {"compfile",      rt_compfile,      game_eternal, 1, 1}},
	{"binaryFile",    {"binaryFile",    rt_binaryFile,    game_eternal, 1, 1}},
	{"cswf",          {"cswf",          rt_cswf,          game_eternal, 9, 9}},
	{"model",         {"model",         rt_generic_atdb,  game_eternal, 0, 67}},

	{"anim",          {"anim",          rt_generic_atdb,  game_eternal, 0, 17}},
	{"skeleton",      {"skeleton",      rt_generic_atdb,  game_eternal, 0, 12}},
	{"binarymd6def",  {"binarymd6def",  rt_generic_atdb,  game_eternal, 0, 0}},

	// Audio will be handled differently by the loader
	{"audio", {"audio", rt_audio, game_all}}
};

/*
* Check if version requirement is met. If it isn't, we should skip reading this mod
*/
bool ModReader_CanLoadMod(ModDef& moddef, const AtlanModConfig& cfg, GlobalConfig_t& globalconfig) {
	if (MOD_LOADER_VERSION < cfg.requiredVersion) {
		atlog("ERROR: Mod requires Mod Loader Version %d or greater. (Your version is %d)", cfg.requiredVersion, MOD_LOADER_VERSION);
		return false;
	}
	if (!(g_game & cfg.gameid)) {
		atlog("ERROR: Mod is for a different game");
		return false;
	}
	if(cfg.elevated) {
		if(globalconfig.elevatedmod != nullptr) {
			atlog("ERROR: Cannot load two Elevated Gameplay mods at the same time! Please remove one of them!"
				"\nActive Elevated Mod: %s", globalconfig.elevatedmod->modName.c_str());
			return false;
		}
		atlog("Elevated Gameplay Enabled");
		globalconfig.elevatedmod = &moddef;
	}
	if(cfg.total_overhaul) {
		if(globalconfig.totalOverhaulMod) {
			atlog("ERROR: Cannot load two Total Overhaul mods at the same time! Please remove one of them!"
				"\nActive Total Overhaul Mod: %s", globalconfig.totalOverhaulMod->modName.c_str());
			return false;
		}
		globalconfig.totalOverhaulMod = &moddef;
		atlog("Total Overhaul Mod Found");
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

		// Apparently spaces are actually fine, at least in Eternal?
		// However, warnings should probably still be thrown about it
		// Some idStudio-converted mods will break if we replace spaces
		else if (' ' == c) {
			//*nameEnd = '_';
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

// AddLevel parameter is needed to prevent a recursive loop when parsing bad level config files
void ModReader_ExtractConfigData(const AtlanModConfig& modconfig, GlobalConfig_t& globalconfig, bool AddLevel) {
	globalconfig.mapspec.insert(globalconfig.mapspec.end(), modconfig.listmapspec, modconfig.listmapspec + modconfig.nummapspec);

	// If installing mods for the first time, the level folder may not have
	// been unzipped yet, depending on the order mods are read in.
	// Hence we must cache the folder names and read their configs later
	if (AddLevel) {
		globalconfig.levelfolders.insert(globalconfig.levelfolders.end(), modconfig.listLoadLevels, modconfig.listLoadLevels + modconfig.numLoadLevels);
	}

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

	for (int i = 0; i < modconfig.numnewmaps; i++) {
		AtlanModConfig::namedlist_t& maplist = modconfig.listnewmaps[i];
		for (int i = 0; i < maplist.numentries; i++) {
			globalconfig.mapspec_addons.push_back({ maplist.entries[i], maplist.listname});
		}
	}
}

void GlobalConfig_t::ReadLevelConfigs(const fspath& gamedir) {

	const fspath archivesdir = gamedir / "base/modarchives";

	if(levelfolders.size())
		atlog("\n\nReading Load Level configs for %zu levels\n-----", levelfolders.size());

	for (const std::string& s_levelfolder : levelfolders) {

		const fspath folder = archivesdir / s_levelfolder;

		AtlanModConfig cfg;

		// TODO: Should probably just read config into memory then call TryRead directory
		// it will avoid flooding console with standard atlanmod log messages
		if (!cfg.TryRead_FromDir(folder)) {
			atlog("Error while reading level config for %s", s_levelfolder.c_str());
		}

		ModReader_ExtractConfigData(cfg, *this, false);
	}
}

void GlobalConfig_t::AddCommonFiles(bool includeStreamDB)
{
	MapSpec_t::AddOn newfiles[] = {
		{"modarchives/common_mod.resources", "common"},
		{"modarchives/common_mod.streamdb", "common"}
	};
	mapspec_addons.insert(mapspec_addons.begin(), newfiles, newfiles + (includeStreamDB ? 2 : 1));
}

std::vector<std::string> GlobalConfig_t::GetNewArchives() const
{
	std::vector<std::string> relativepaths;
	relativepaths.reserve(mapspec_addons.size());

	// MONITOR: We're not checking for uniqueness....for now
	// Probably not necessary, since the only duplicate entries
	// are .streamdbs.
	for (const MapSpec_t::AddOn& addon : mapspec_addons) {
		if (addon.newfile.rfind(".resources") != -1) {
			relativepaths.push_back(addon.newfile);
		}
	}
	return relativepaths;
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
	AtlanModConfig cfg;

	moddef.modName = "[Unzipped] ";
	moddef.modName.append(modsfolder.stem().string());
	moddef.modtype = mt_folder;
	moddef.ActiveZip = false;

	atlog("\n\nReading %s\n---", moddef.modName.c_str());

	// Since we're now doing assetsinfo, it's probably a good idea to
	// return if there's a config parsing error
	if(!cfg.TryRead_FromDir(modsfolder))
		return;
	if(!ModReader_CanLoadMod(moddef, cfg, globalconfig))
		return;
	ModReader_ExtractConfigData(cfg, globalconfig, true);
	moddef.loadPriority = -999;

	std::vector<fspath> ModFilePaths = cfg.GetModFiles(modsfolder);
	atlog("Found %llu Unzipped Mod Files", ModFilePaths.size());

	idUnzippedImageJobList ImageJobs;

	for (const fspath& FilePath : ModFilePaths) {
		ModFile modfile;
		modfile.realPath = FilePath.string().substr(modsfolder_length + 1);

		std::string EncodingInfo;
		if (!ModReader_ValidatePath(modfile, cfg, &EncodingInfo)) {
			continue;
		}

		if (modfile.typeenum == rt_image && idImageEncodingContext::CanEncode(FilePath.c_str())) {
			
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

		for (size_t i = 0; i < temp_assetpaths.size(); i++) {
			idUnzippedImageJob& j = ImageJobs.jobs[i];
			j.assetpath = temp_assetpaths[i];
			moddef.modFiles[j.ModFileIndex].assetPath = std::move(temp_assetpaths[i]);
		}

		ImageJobs.context = &ImageEncoder;
		ImageJobs.mod = &moddef;

		for (int i = 0; i < NUMTHREADS; i++)
			threadpool[i] = std::thread(UnzippedImageEncodingThread, &ImageJobs);
		for (int i = 0; i < NUMTHREADS; i++)
			threadpool[i].join();
	}
}

bool ModReader_InstallLevelMod(mz_zip_archive* zptr, const fspath& gamedir) {

	const fspath basedir = gamedir / "base";
	fspath extractionPath;

	uint32_t fileCount = mz_zip_reader_get_num_files(zptr);

	MapSpec_t mapspec(gamedir / "base/packagemapspec.json");
	if (!mapspec.good) {
		atlog("   ERROR: Failed to read packagemapspec");
		return false;
	}
	
	std::string typestring;
	charbuffer_t namebuffer;
	namebuffer.EnsureCapacity(512);
	
	for (u32 i = 0; i < fileCount; i++) {
		if(mz_zip_reader_is_file_a_directory(zptr, i))
			continue;

		u32 namelength = mz_zip_reader_get_filename(zptr, i, namebuffer.data, (u32)namebuffer.capacity);
		while (namelength >= namebuffer.capacity) {
			namebuffer.EnsureCapacity(namelength);
			namelength = mz_zip_reader_get_filename(zptr, i, namebuffer.data, (u32)namebuffer.capacity);
		}

		if(strcmp(CFG_NAME_V2, namebuffer.data) == 0)
			continue;

		typestring.clear();
		char* cptr = namebuffer.data;
		while (*cptr) {
			if (*cptr == '/' || *cptr == '\\') {
				typestring = std::string(namebuffer.data, cptr - namebuffer.data);
				break;
			}
			cptr++;
		}
		if (typestring == "modarchives") {
			extractionPath = basedir / namebuffer.data;
		}
		else if (typestring == "virtualtextures") {
			extractionPath = gamedir / namebuffer.data;

			// Fail safe to prevent the deletion of vanilla lightmaps
			// TODO: we'll need to support lightmap backup creation/restoration if
			// users ever want to package a modified vanilla level as an Atlan mod
			bool replacesVanillaLightmap = !mapspec.IsUnusedLightmap(namebuffer.data);
			if (replacesVanillaLightmap) {
				atlog("ERROR: Unzipping file %s would overrwrite vanilla lightmap.\n   This is unsupported", namebuffer.data);
				return false;
			}
		}
		else {
			atlog("ERROR: Unrecognized file type for file %s", namebuffer.data);
			return false;
		}

		atlog("Extracting %s", namebuffer.data);
		std::filesystem::create_directories(extractionPath.parent_path());

		bool result = mz_zip_reader_extract_to_file(zptr, i, extractionPath.string().c_str(), 0);
		if (!result) {
			atlog("   ERROR: Failed to extract file");
			return false;
		}
	}

	return true;
}

// If return value is true, we should keep the zip file alive after reading
// to enable just-in-time loading
bool ReadZipMod_Internal(mz_zip_archive* zptr, const fspath& gamedir, ModDef& mod, GlobalConfig_t& globalconfig, bool& out_deleteZip)
{
	bool KEEP_ZIP_ALIVE = false;

	/*
	* Read config files if they exist
	*/
	AtlanModConfig cfg;
	if(!cfg.TryRead_FromZip(zptr))
		return false;
	mod.loadPriority = cfg.loadPriority;
	if (!ModReader_CanLoadMod(mod, cfg, globalconfig)) {
		return false;
	}

	if (cfg.installOnce) {
		atlog("Performing one-time installation of idStudio level mod"
			"\nThis process may take some time!");
		
		if (ModReader_InstallLevelMod(zptr, gamedir)) {
			out_deleteZip = true;
		}
		return false;
	}

	ModReader_ExtractConfigData(cfg, globalconfig, true);

	/*
	* Read all mod files
	*/
	uint32_t fileCount = mz_zip_reader_get_num_files(zptr);
	mod.modFiles.reserve(fileCount);
	
	charbuffer_t namebuffer;
	namebuffer.EnsureCapacity(512);

	for (uint32_t i = 0; i < fileCount; i++) {
		if(mz_zip_reader_is_file_a_directory(zptr, i))
			continue;

		ModFile modfile;

		/*
		* Get the file name
		*/
		uint32_t nameLength = mz_zip_reader_get_filename(zptr, i, nullptr, 0);
		namebuffer.EnsureCapacity(nameLength);
		mz_zip_reader_get_filename(zptr, i, namebuffer.data, nameLength);
		modfile.realPath = std::string(namebuffer.data, nameLength - 1); // Length includes null char

		/*
		* If this is a pre-parsed config file, skip it
		*/
		if(modfile.realPath == CFG_NAME || modfile.realPath == CFG_NAME_V2)
			continue;

		if (!ModReader_ValidatePath(modfile, cfg)) {
			continue;
		}

		/*
		* Read the mod data
		*/

		if (modfile.typeenum & rtc_deferload) {
			KEEP_ZIP_ALIVE = true;
			modfile.containerIndex = i;
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
	return KEEP_ZIP_ALIVE;
}

void ModReader::ReadZipMod(ModDef& mod, const fspath& gamedir, const fspath& zipPath, GlobalConfig_t& globalconfig)
{
	atlog("\n\nReading %ls\n---", zipPath.filename().c_str());

	// Make sure this starts off false
	bool delete_zipFile = false;

	// Open the zip file
	mz_zip_archive* zptr = &mod.zipfile;
	mz_zip_zero_struct(zptr);
	if (!mz_zip_reader_init_file(zptr, zipPath.string().c_str(), 0))
	{
		atlog("ERROR: Failed to open zip file");
		return;
	}

	mod.modtype = mt_zipfile;
	mod.modName = zipPath.stem().string();
	mod.ActiveZip = ReadZipMod_Internal(zptr, gamedir, mod, globalconfig, delete_zipFile);
	if (!mod.ActiveZip) {
		mz_zip_reader_end(zptr);
	}

	if (delete_zipFile) {
		atlog("Level Mod installed successfully. Deleting zip file");
		std::filesystem::remove(zipPath);
	}
}

bool ModReader::LoadModData(ModFile& modfile, charbuffer_t& buffer) {
	if(modfile.dataBuffer)
		return true;

	if(!modfile.parentMod->ActiveZip)
		return false;
	mz_zip_archive* zptr = &modfile.parentMod->zipfile;
	
	if(modfile.containerIndex < 0)
		return false;

	mz_zip_archive_file_stat fstats;
	if(!mz_zip_reader_file_stat(zptr, modfile.containerIndex, &fstats))
		return false;


	buffer.EnsureCapacity(fstats.m_uncomp_size + 2048);
	buffer.length = fstats.m_uncomp_size;

	if(!mz_zip_reader_extract_to_mem_no_alloc(zptr, modfile.containerIndex, buffer.data, buffer.capacity, 0, NULL, 0))
		return false;

	modfile.ownsData = false;
	modfile.dataBuffer = buffer.data;
	modfile.dataLength = buffer.length;

	return true;
}