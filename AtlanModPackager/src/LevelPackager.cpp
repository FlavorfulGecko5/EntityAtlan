#include "archives/MapResources.h"
#include "archives/PackageMapSpec.h"
#include "archives/ResourceStructs.h"
#include "atlan/AtlanLogger.h"
#include "atlan/AtlanModConfig.h"
#include "LevelPackagerGlobals.h"
#include "miniz/miniz.h"
#include "hash/HashLib.h"
#include <set>

#ifdef _DEBUG // Compressing archives and lightmaps in debug builds is very slow
#define COMPMODE_LARGE_FILES MZ_NO_COMPRESSION
#else
#define COMPMODE_LARGE_FILES MZ_DEFAULT_COMPRESSION
#endif

#define DEBUG_ZIP_NO_FILES 1
#if DEBUG_ZIP_NO_FILES && defined ( _DEBUG )
#define mz_zip_writer_add_file(...)
#endif

extern std::set<std::string> G_PK5_MANIFEST;

// Maps all-lowercase mapresources entries to their capitalized type strings
// i.e. "ability_dash/some/file" = "ability_Dash"
typedef std::unordered_map<std::string, std::string> mapresmap_t;

// Our Goal: Automate asset list generation for .mapresources editing
void LevelPackager_LoadMapResources(ResourceArchive& r, mapresmap_t& EntryMap) {

	MapResource mapres;
	ResourceEntryBuffers_t buffers;

	// .mapresources are among the last entries in the archives
	for (int i = (int)r.header.numResources - 1; i > -1; i--) {

		const ResourceEntry& e = r.entries[i];
		const char* type, * name;
		Get_EntryStrings(r, e, type, name);

		if (strcmp(type, "file"))
			continue;

		// MONITOR: It shouldn't be necessary to parse init.mapresources
		// unless someone creates a map with custom renderparms/shader code/etc.
		// The big question is if custom renderparms/renderprograms/etc. can even be loaded through common
		// or if we need to add them to init's mapresources (and therefore also load Atlan's
		// resource archive in the init map.) More testing must be done. But for now,
		// it should be safe to ignore this edge case
		// FURTHER NOTES: idTech7 seems to mash the init map files into common's archives,
		// idTech8 gives the init map it's own archive.

		// MONITOR 2: Can the warehouse map even load assets from an archive that's only loaded by
		// the common map?
		if (strcmp(name, "generated/buildgame/common.mapresources")
			&& strcmp(name, "generated/buildgame/warehouse.mapresources")) {
			continue;
		}

		atlog("Loading %s", name);

		ResourceEntryData_t entrydata = Get_EntryData(e, r.filehandle, buffers);
		mapres.Parse(entrydata.buffer, entrydata.length);

		EntryMap.reserve(mapres.num_entries + 1048);
		std::string lookup;

		for (u32 i = 0; i < mapres.num_entries; i++) {
			std::string uppertype = mapres.list_types[mapres.list_entries[i].typeindex].string();
			lookup = uppertype;

			// Need to be able to map the all-lowercased decl typestrings to their
			// camelcased forms
			for (char& c : lookup) {
				if (c >= 'A' && c <= 'Z')
					c += 32;
			}

			lookup.push_back('/');
			lookup.append(mapres.list_entries[i].path.string());

			EntryMap[lookup] = uppertype;
		}
		return;
	}
}

void LevelPackager_NeedsMapRes(const std::string& archiveString, const mapresmap_t& MapResMap, std::string& atlanmod) {

	bool InPK5 = G_PK5_MANIFEST.count(archiveString);
	if (!InPK5) {
		atlog("%s\n   - WARNING: Global asset missing from PK5", archiveString.c_str());
	}

	std::string lookup = archiveString;

	if (lookup._Starts_with("rs_streamfile/generated/decls/")) {
		lookup.erase(lookup.begin(), lookup.begin() + 30); // Length of the above string literal
		lookup.erase(lookup.end() - 5, lookup.end()); // Erase ".decl"
	}

	auto iter = MapResMap.find(lookup);
	if (iter == MapResMap.end()) {
		atlog("%s\n   - WARNING: No mapresources entry found!", archiveString.c_str());
		return;
	}

	atlanmod.push_back('"');
	atlanmod.append(iter->second);
	atlanmod.push_back('/');
	atlanmod.append(lookup.substr(lookup.find('/') + 1));
	atlanmod.append("\"\n");
}

void AnalyzeGlobalMap(const fspath& basedir, const MapSpec_t& mapspec, const size_t mapindex, const AssetChecksumMap& assetmap, std::string& atlanmod) {

	atlog("\nAnalyzing %s", mapspec.listmaps[mapindex].c_str());

	// Gameresources has a .resources and a .streamdb
	// Warehouse only has a .resources
	if (mapspec.listrefs[mapindex].size() > 2) {
		atlog("ERROR: Expected <= 2 mapFileRefs, found %zu", mapspec.listrefs[mapindex].size());
		return;
	}

	fspath archivepath;
	for (const size_t i : mapspec.listrefs[mapindex]) {
		const std::string& filename = mapspec.listfiles[i];

		if (filename.rfind(".resources") != -1) {
			archivepath = basedir / filename;
		}
	}

	atlog("Checking archive %ls", archivepath.c_str());

	ResourceArchive r;
	if (!idcl::ReadResource(r, archivepath.c_str(), RF_SkipData, true)) {
		atlog("ERROR: Failed to open archive");
		return;
	}

	mapresmap_t MapResMap;
	LevelPackager_LoadMapResources(r, MapResMap);

	atlog("Map Resource Array Size: %zu", MapResMap.size());

	const char* type, * name;
	std::string lookup;
	for (u32 i = 0; i < r.header.numResources; i++) {
		const ResourceEntry& e = r.entries[i];
		Get_EntryStrings(r, e, type, name);

		uint64_t lookupHash = HashLib::DeclHash(type, name);

		lookup.clear();
		lookup.append(type);
		lookup.push_back('/');
		lookup.append(name);

		auto iter = assetmap.find(lookupHash);
		if (iter == assetmap.end()) {
			//atlog("ADD TO PK5: %s", lookup.c_str());
			LevelPackager_NeedsMapRes(lookup, MapResMap, atlanmod);
		}
		else if(iter->second.v0 != e.dataCheckSum && iter->second.v1 != e.dataCheckSum) {
			atlog("%s\n   - Note: Vanilla global asset is modified", lookup.c_str());
		}
	}
}

void AnalyzeInit(const fspath& basedir, const MapSpec_t& mapspec, const size_t index) {
	atlog("Analyzing Init");
	if (mapspec.listrefs[index].size() == 1) {
		size_t fileindex = mapspec.listrefs[index][0];
		if (mapspec.listfiles[fileindex].find(".streamdb") != -1) {
			return;
		}
	}
	atlog("WARNING: Init mapFileRefs are unexpected. Please report this.");
}

void AnalyzeShell(const fspath& basedir, const MapSpec_t& mapspec, const size_t index) {
	atlog("\nAnalyzing Shell");
	if (mapspec.listrefs[index].size() != 2) {
		atlog("ERROR: Shell map malformed?");
		return;
	}

	fspath shellarchive = basedir / "game/shell/shell.resources";
	ResourceArchive r;
	if (!idcl::ReadResource(r, shellarchive.c_str(), RF_SkipData, false)) {
		atlog("ERROR: Failed to open shell archive");
		return;
	}

	if (!(r.header.numResources == 1 && r.entries[0].uncompressedSize == 24)) {
		atlog("WARNING: game/shell/shell.resources has non-empty contents."
			"\n   Please report this warning.");
	}
}

bool LevelPackager_AddArchives(mz_zip_archive* zptr, const fspath& basedir, const std::string& LEVEL_FOLDER) {
	MapSpec_t mapspec(basedir / "packagemapspec.json");

	if (mapspec.listmaps.size() < 5
		|| mapspec.listmaps[0] != "common"
		|| mapspec.listmaps[1] != "warehouse"
		|| mapspec.listmaps[2] != "game/shell/shell"
		|| mapspec.listmaps.back() != "init")
	{
		atlog("ERROR: Malformed packagemapspec");
		return false;
	}

	// --- Analyze archives and build up global asset lists ---
	std::string atlanmod = "assets {\ngenerated_common {\n";
	AnalyzeGlobalMap(basedir, mapspec, 0, EMPTYMAP_GAMERESOURCES, atlanmod);
	atlanmod.append("}\ngenerated_warehouse{\n");
	AnalyzeGlobalMap(basedir, mapspec, 1, EMPTYMAP_WAREHOUSE, atlanmod);
	AnalyzeShell(basedir, mapspec, 2);
	AnalyzeInit(basedir, mapspec, mapspec.listmaps.size() - 1);

	atlanmod.append("}\n}\nmapresources {\ncommon {\ngenerated_common\n}\nwarehouse {\ngenerated_warehouse\n}\n}");

	atlanmod.append("\nnewmaps {\n");

	std::set<std::string> addlist;
	for (size_t i = 3; i < mapspec.listmaps.size() - 1; i++) {
		const std::string& s = mapspec.listmaps[i];
		atlog("New Map: %s", s.c_str());

		atlanmod.push_back('"');
		atlanmod.append(mapspec.listmaps[i]);
		atlanmod.append("\" {\n");
		for (const size_t fileindex : mapspec.listrefs[i]) {
			addlist.insert(mapspec.listfiles[fileindex]);

			atlanmod.append("\t\"");
			atlanmod.append("modarchives/");
			atlanmod.append(LEVEL_FOLDER);
			atlanmod.push_back('/');
			atlanmod.append(mapspec.listfiles[fileindex]);
			atlanmod.append("\"\n");
		}
		atlanmod.append("}\n");
	}
	atlanmod.append("}");

	std::string path_txt_mapspec = "modarchives/" + LEVEL_FOLDER;
	path_txt_mapspec.push_back('/');
	path_txt_mapspec.append(CFG_NAME_V2);
	mz_zip_writer_add_mem(zptr, path_txt_mapspec.c_str(), atlanmod.c_str(), atlanmod.size(), MZ_DEFAULT_COMPRESSION);
	for (const std::string& relative : addlist) {
		fspath fullpath = basedir / relative;
		atlog("Zipping %s", relative.c_str());

		std::string zipname = "modarchives/";
		zipname.append(LEVEL_FOLDER);
		zipname.push_back('/');
		zipname.append(relative);
		check_debug(std::filesystem::exists(fullpath));
		mz_zip_writer_add_file(zptr, zipname.c_str(), fullpath.string().c_str(), NULL, 0, COMPMODE_LARGE_FILES);
	}

	return true;
}

void LevelPackager_AddVirtualTextures(mz_zip_archive* zptr, const fspath& modfolder) {
	const size_t modfolder_length = modfolder.string().size();
	for (const auto& dir_iter : std::filesystem::recursive_directory_iterator(modfolder / "virtualtextures"))
	{
		if (dir_iter.is_directory())
			continue;

		fspath path = dir_iter.path();
		if (path.extension() != ".lightdb")
			continue;

		std::string zipname = path.string().substr(modfolder_length + 1);
		atlog("Zipping %s", zipname.c_str());
		mz_zip_writer_add_file(zptr, zipname.c_str(), path.string().c_str(), NULL, 0, COMPMODE_LARGE_FILES);
	}
}

std::string LevelPackager_GetArchiveDir(const fspath& modfolder) {
	std::string path = (modfolder / CFG_NAME_V2).string();
	AtlanModConfig config;
	if (config.TryRead(path)) {
		if (config.levelfolder.size()) {

			for (char c : config.levelfolder) {
				if ((c >= 'a' && c <= 'z') || c == '_')
					continue;
				atlog("FATAL ERROR: Config file's load_level property may only have lowercase letters and underscores");
				return "";
			}

			atlog("Found valid config file");
			return config.levelfolder;
		}
	}

	atlog("FATAL ERROR: Level Packager requires a valid " CFG_NAME_V2 " to exist, with a 'load_level' property"
		"\n   This property defines the directory your archives will go in");
	return "";
}

void LevelPackager_AddConfig(mz_zip_archive* zptr) {
	const char* atlanmod =
		R"(modinfo {
	game = "DOOM Eternal"
	requiredVersion = 10
	loadPriority = 1
}
install_once
)";

	mz_zip_writer_add_mem(zptr, CFG_NAME_V2, atlanmod, strlen(atlanmod), MZ_DEFAULT_COMPRESSION);
}

void ZipLevel(const fspath& modfolder, const fspath& leveldir) {

	std::string load_folder = LevelPackager_GetArchiveDir(modfolder);
	if (!load_folder.size())
		return;

	std::string outpath = (modfolder / "AtlanPackage_InstallOnce.zip").string();
	mz_zip_archive ziparchive;
	mz_zip_archive* zptr = &ziparchive;

	mz_zip_zero_struct(zptr);
	if (!mz_zip_writer_init_file(zptr, outpath.c_str(), 0)) {
		atlog("ERROR: Failed to open output zip file");
		return;
	}

	LevelPackager_AddArchives(zptr, leveldir, load_folder);

	LevelPackager_AddVirtualTextures(zptr, modfolder);

	LevelPackager_AddConfig(zptr);

	mz_zip_writer_finalize_archive(zptr);
	mz_zip_writer_end(zptr);
}