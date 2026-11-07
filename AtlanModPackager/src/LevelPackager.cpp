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

		ResourceData entrydata = r.GetData(e);
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
	if (!r.Read(archivepath)) {
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
	if (!r.Read(shellarchive)) {
		atlog("ERROR: Failed to open shell archive");
		return;
	}

	if (!(r.header.numResources == 1 && r.entries[0].uncompressedSize == 24)) {
		atlog("WARNING: game/shell/shell.resources has non-empty contents."
			"\n   Please report this warning.");
	}
}

const AssetChecksumMap LevelOptimizer_BuildMap(const fspath& gamedir) {

	AssetChecksumMap output;

	idcl::ArchiveIterator iter(gamedir / "base/packagemapspec.json", true);

	for (const ResourceEntry& e : iter) {
		if (iter.newarchiveloaded) {
			iter.newarchiveloaded = false;

			std::string archivename = iter.CurrentArchive().filename().string();
			if (archivename.find("e5m1_spear") == -1 && archivename.find("gameresources") == -1 && archivename.find("warehouse") == -1) {
				iter.SkipArchive();
				continue;
			}

			//printf("%s\n", archivename.c_str());
		}

		uint64_t hash = HashLib::DeclHash(iter.typestring, iter.namestring);
		output[hash] = {e.dataCheckSum, e.defaultHash};
	}

	return output;
}

#include "archives/ResourceStringTable.h"

void LevelOptimizer_BuildStringChunk(const ResourceArchive& original, ResourceArchive& newarc) 
{
	StringTable stable;

	newarc.header.numStringIndices = newarc.header.numResources * 2;
	newarc.stringIndex.EnsureCapacity(newarc.header.numStringIndices);

	uint64_t* ptr = newarc.stringIndex.buffer;

	for (u32 i = 0; i < newarc.header.numResources; i++) {
		const ResourceEntry& e = newarc.entries[i];

		const char* type, *name;

		// The New archive's entries are still identical to
		// the old archive's at this point
		Get_EntryStrings(original, e, type, name);

		*ptr = stable.indexof(type);
		*(ptr + 1) = stable.indexof(name);
		ptr += 2;
	}

	stable.finalize(newarc.stringChunk, newarc.header.stringTableSize);
}

void LevelOptimizer_BuildEntryArray(const ResourceArchive& original, ResourceArchive& newarc, const AssetChecksumMap& lookupmap) 
{
	int numDefaultHashMismatches = 0;

	newarc.entries.EnsureCapacity(original.header.numResources);
	newarc.header.numResources = 0;
	for (u32 i = 0; i < original.header.numResources; i++) {
		const ResourceEntry& e = original.entries[i];
		const char* type, * name;

		Get_EntryStrings(original, e, type, name);

		uint64_t hash = HashLib::DeclHash(type, name);

		auto iter = lookupmap.find(hash);
		if (iter != lookupmap.end()) {

			if (iter->second.v0 == e.dataCheckSum && iter->second.v1 == e.defaultHash) {
				continue;
			}
			if(iter->second.v0 == e.dataCheckSum)
				numDefaultHashMismatches++;

			// Keep check inside this conditional incase some files we're culling don't exist in the vanilla levels we're importing
			iter = EMPTYMAP_STREAMDB_FILES.find(hash);
			if (iter != EMPTYMAP_STREAMDB_FILES.end() && iter->second.v0 == e.dataCheckSum && iter->second.v1 == e.defaultHash) {
				continue;
			}
		}

		newarc.entries[newarc.header.numResources++] = e;
	}

	// There are instances where the checksum matches up, but not
	// the default hash. This log is here to monitor how rare it is.
	atlog("   Encountered %d defaultHash mismatches", numDefaultHashMismatches);
}

#include <fstream>
#include <archives/StreamDB.h>

// We can hyper-optimize both the resource archive and the streamdb file sizes
// by excluding entries found in a DLC2 level, and importing the DLC 2 level
// files into the modded level via packagemapspec editing.
// If a level mod uses all DLC2 AI, this approach reduces the streamdb size by ~300 megabytes.
// It appears these AI assets aren't in a globally loaded streamdb file.
//
// Because there's only one streamdb file shared by all levels in a mod, we have to analyze
// every rebuilt archive to finalize a list of cullable streamdb entries.
// As level mods become more expansive, it may become less optimal to use a DLC2 level versus
// a level with better overlap in level geometry kit pieces.
// (i.e. if it's a hell-themed level, maybe Nekravol would be a better level choice)
//
// For multi-level mods, with each level having a different theme, this approach will
// give diminishing returns. In these cases, it may be worth constructing one streamdb per level.
// and allowing users to specify different maps they want to import for each level via the config.
// Exploring this approach would be pretty overkill at the moment
// 
// Currently, this approach will suffice, and swapping the DLC2 level we use should be enough
// to support Sentinel/Earth/Immora-themed levels
struct OptimizedDB {
	idStreamDB dbfile;
	fspath realpath;
	std::string zipname;
	std::vector<u8> include;
};

void LevelOptimizer_RebuildStreamDB(mz_zip_archive* zptr, OptimizedDB& db) {
	atlog("Optimizing StreamDB file %s", db.zipname.c_str());

	const fspath TEMP_PATH = db.realpath.string() += ".temp";

	/* Update the header chunk */
	u32 oldEntryCount = db.dbfile.header.numEntries;
	db.dbfile.header.numEntries = 0;
	for (const u8 i : db.include) {
		if(i) db.dbfile.header.numEntries++;
	}

	db.dbfile.header.headerLength = sizeof(idStreamDB::header_t) + sizeof(idStreamDB::prefetchheader_t)
		+ sizeof(idStreamDB::entry_t) * db.dbfile.header.numEntries;

	// Ensure we've culled all prefetch blocks (unclear how important these are. The empty archive doesn't produce these.)
	// But we would need to cull the hashes of any removed entries if these exist
	if (db.dbfile.prefetchheader.numblocks > 0) {
		atlog("WARNING: StreamDB has prefetch blocks");
	}
	db.dbfile.prefetchheader.numblocks = 0;
	db.dbfile.prefetchheader.totalLength = sizeof(idStreamDB::prefetchheader_t);

	heaparray<char> entrybuffer;

	idStreamDB::entry_t* newEntries = new idStreamDB::entry_t[db.dbfile.header.numEntries];
	u64 runningDataOffset = db.dbfile.header.headerLength;
	runningDataOffset += 16 - runningDataOffset % 16;

	std::ofstream outwriter(TEMP_PATH, std::ios_base::binary);

	/* Copy over the entries*/

	idStreamDB::entry_t* nextEntry = newEntries;
	for (u32 i = 0; i < oldEntryCount; i++) {
		if(!db.include[i])
			continue;

		outwriter.seekp(runningDataOffset, std::ios_base::beg);

		*nextEntry = db.dbfile.entries[i];

		//atlog("WRITING: %zu %u", runningDataOffset, nextEntry->length);

		nextEntry->offset16 = (u32)(runningDataOffset / 16);
		runningDataOffset += nextEntry->length;
		runningDataOffset += 16 - runningDataOffset % 16;

		db.dbfile.GetData_Raw(i, entrybuffer);

		outwriter.write(entrybuffer.buffer, nextEntry->length);

		nextEntry++;
	}

	outwriter.seekp(0, std::ios_base::beg);
	outwriter.write((char*)&db.dbfile.header, sizeof(idStreamDB::header_t));
	outwriter.write((char*)newEntries, db.dbfile.header.numEntries * sizeof(idStreamDB::entry_t));
	outwriter.write((char*)&db.dbfile.prefetchheader, sizeof(idStreamDB::prefetchheader_t));
	outwriter.close();

	delete[] newEntries;

	#ifdef _DEBUG
	idStreamDB validator;
	check_debug(validator.Read(TEMP_PATH.c_str(), false));

	#endif

	atlog("Zipping StreamDb");
	mz_zip_writer_add_file(zptr, db.zipname.c_str(), TEMP_PATH.string().c_str(), NULL, 0, COMPMODE_LARGE_FILES);
	std::filesystem::remove(TEMP_PATH);
}

void LevelOptimizer_CheckStreamDB(const ResourceArchive& newarc, OptimizedDB& StreamDB) {
	
	for (u32 i = 0; i < newarc.header.numResources; i++) {
		const ResourceEntry& e = newarc.entries[i];

		if(e.defaultHash == e.dataCheckSum)
			continue;

		for (u32 i = 0; i < 16; i++) {
			u64 lookup = e.defaultHash << 4 | i;

			idStreamDB::query result = StreamDB.dbfile.Find(lookup);
			if (result.found()) {
				StreamDB.include[result.index] = 1;
			}
		}
	}
}

/*
* Optimizes the level-specific archives by culling duplicates of globally-loaded resources
* By doing this, we can reduce the final archive size by a minimum of ~127 megabytes
* 
* There's seemingly no need to optimize the .streamdb file in a similar way. 
* Analysis suggests that idStudio already excludes duplicate streamdb entries.
* This would explain why packaging a level mod gets messed up when custom streamdbs exist in your game folder
*/
void LevelPackager_OptimizeArchive(const fspath& archivepath, const AssetChecksumMap& lookupmap, OptimizedDB& StreamDB) 
{
	ResourceArchive original;
	original.Read(archivepath);

	ResourceArchive newarc;
	newarc.header = original.header;

	/* Determine what Resource Entries we're keeping */
	LevelOptimizer_BuildEntryArray(original, newarc, lookupmap);

	/* Build the string table */
	LevelOptimizer_BuildStringChunk(original, newarc);

	/* Change header values from the original */
	{
		ResourceHeader& h = newarc.header;
		h.stringTableOffset = h.resourceEntriesOffset + h.numResources * sizeof(ResourceEntry);
		h.resourceDepsOffset = h.stringTableOffset + h.stringTableSize;
		h.numDependencies = 0;
		h.numDepIndices = 0;
		h.metaEntriesOffset = h.resourceDepsOffset;
		h.resourceSpecialHashOffset = h.resourceDepsOffset + h.numDependencies * sizeof(ResourceDependency);

		newarc.metaheader.unknown = 0;
		newarc.metaheader.metaOffset = Get_ExpectedMetaOffset(h);
		h.dataOffset = newarc.metaheader.metaOffset + 4;
		h.dataOffset += 8 - h.dataOffset % 8;
		assert(h.dataOffset % 8 == 0);
	}

	std::ofstream outwriter(archivepath.string() + ".temp", std::ios_base::binary);

	uint64_t runningOffset = newarc.header.dataOffset;

	/* Update Resource Entries and write entry data */
	for (u32 i = 0; i < newarc.header.numResources; i++) {
		ResourceEntry& e = newarc.entries[i];

		// Must fetch this before we update the data offset
		ResourceData entrydata = original.GetData_Raw(e);

		e.numDependencies = 0;
		e.depIndices = 0;
		e.strings = i * 2;
		e.dataOffset = runningOffset;

		runningOffset += e.dataSize;
		runningOffset += 8 - runningOffset % 8;
		assert(runningOffset % 8 == 0);

		outwriter.seekp(e.dataOffset, std::ios_base::beg);
		outwriter.write(entrydata.buffer, entrydata.length);
	}

	// TODO: This is copied verbatim from the mod loader
	// create a shared function somehow?
	/* Write meta chunk */
	outwriter.seekp(0, std::ios_base::beg);
	outwriter.write((char*)&newarc.header, sizeof(ResourceHeader));
	outwriter.write((char*)&newarc.metaheader, sizeof(ResourceMetaHeader));
	outwriter.write((char*)newarc.entries.buffer, sizeof(ResourceEntry) * newarc.header.numResources);
	
	// String Chunk
	uint64_t blobSize = newarc.header.stringTableSize - sizeof(uint64_t) - sizeof(uint64_t) * newarc.stringChunk.numStrings - newarc.stringChunk.paddingCount;
	outwriter.write((char*)&newarc.stringChunk.numStrings, sizeof(uint64_t));
	outwriter.write((char*)newarc.stringChunk.offsets.buffer, newarc.stringChunk.numStrings * sizeof(uint64_t));
	outwriter.write(newarc.stringChunk.dataBlock.buffer, blobSize);
	for (uint64_t i = 0; i < newarc.stringChunk.paddingCount; i++)
		outwriter.put('\0');
	outwriter.write((char*)newarc.stringIndex.buffer, newarc.header.numStringIndices * sizeof(uint64_t));

	// IDCL
	outwriter.write("IDCL", 4);
	outwriter.close();

	#ifdef _DEBUG
	Audit_ResourceArchive(newarc);

	std::string manifest;
	idcl::String_ResourceArchive(newarc, manifest, {0, 0, nullptr});
	outwriter.open(archivepath.string() + ".manifest.txt", std::ios_base::binary);
	outwriter.write(manifest.data(), manifest.length());
	outwriter.close();
	#endif

	LevelOptimizer_CheckStreamDB(newarc, StreamDB);
}

// Validates the mapspec and initializes the streamdb
bool LevelPackager_ValidateMapspec(const MapSpec_t& mapspec, OptimizedDB& streamdb, const fspath& basedir, const std::string& ZIP_FOLDER) {
	if (mapspec.listmaps.size() < 5
		|| mapspec.listmaps[0] != "common"
		|| mapspec.listmaps[1] != "warehouse"
		|| mapspec.listmaps[2] != "game/shell/shell"
		|| mapspec.listmaps.back() != "init")
	{
		atlog("ERROR: Malformed packagemapspec");
		return false;
	}

	// Since we're doing streamdb optimization we need to account for this now
	int numDBs = 0;
	for (const std::string& s : mapspec.listfiles) {
		if (s.find(".streamdb") != -1) {
			numDBs++;
			streamdb.realpath = basedir / s;
			streamdb.zipname = ZIP_FOLDER + s;
		}
	}

	if (numDBs != 1) {
		atlog("ERROR: Found %d StreamDB archives, 1 expected", numDBs);
		return false;
	}

	// Initialize the streamdb
	streamdb.dbfile.Read(streamdb.realpath.c_str(), true);
	streamdb.include.resize(streamdb.dbfile.header.numEntries);
	memset(streamdb.include.data(), 0, streamdb.include.size());

	return true;
}

bool LevelPackager_AddArchives(mz_zip_archive* zptr, const fspath& basedir, const std::string& ZIP_FOLDER, const fspath& gamedir) {
	OptimizedDB streamdb;
	MapSpec_t mapspec(basedir / "packagemapspec.json");

	if (!LevelPackager_ValidateMapspec(mapspec, streamdb, basedir, ZIP_FOLDER)) {
		return false;
	}

	// --- Analyze archives and build config's asset lists ---
	std::string atlanmod = "assets {\ngenerated_common {\n";
	AnalyzeGlobalMap(basedir, mapspec, 0, EMPTYMAP_GAMERESOURCES, atlanmod);
	atlanmod.append("}\ngenerated_warehouse{\n");
	AnalyzeGlobalMap(basedir, mapspec, 1, EMPTYMAP_WAREHOUSE, atlanmod);
	AnalyzeShell(basedir, mapspec, 2);
	AnalyzeInit(basedir, mapspec, mapspec.listmaps.size() - 1);

	// Config Map Resources
	atlanmod.append("}\n}\nmapresources {\ncommon {\ngenerated_common\n}\nwarehouse {\ngenerated_warehouse\n}\n}");

	// --- Config NewMaps List ---
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
			atlanmod.append(ZIP_FOLDER);
			atlanmod.append(mapspec.listfiles[fileindex]);
			atlanmod.append("\"\n");
		}
		atlanmod.append("}\n");
	}

	// Config MapSpec import statements
	atlanmod.append("}\nmapspec {\n");
	for (size_t i = 3; i < mapspec.listmaps.size() - 1; i++) {
		atlanmod.append("\"game/dlc2/e5m1_spear/e5m1_spear$");
		atlanmod.append(mapspec.listmaps[i]);
		atlanmod.append("\"\n");

	}
	atlanmod.append("}\n");

	atlog("Creating Global Asset Map");
	const AssetChecksumMap globalResMap = LevelOptimizer_BuildMap(gamedir);
	if (globalResMap.size() == 0) {
		atlog("FATAL ERROR: Failed to construct Global Resource Map");
		return false;
	}

	std::string path_txt_mapspec = ZIP_FOLDER + CFG_NAME_V2;
	mz_zip_writer_add_mem(zptr, path_txt_mapspec.c_str(), atlanmod.c_str(), atlanmod.size(), MZ_DEFAULT_COMPRESSION);
	for (const std::string& relative : addlist) {
		fspath fullpath = basedir / relative;

		std::string zipname = ZIP_FOLDER + relative;
		check_debug(std::filesystem::exists(fullpath));

		if (relative.find(".resources") != -1) {
			atlog("Zipping and Optimizing %s", relative.c_str());
			LevelPackager_OptimizeArchive(fullpath, globalResMap, streamdb);

			std::string temppath = fullpath.string() + ".temp";

			mz_zip_writer_add_file(zptr, zipname.c_str(), temppath.c_str(), NULL, 0, COMPMODE_LARGE_FILES);
			std::filesystem::remove(temppath);
		}
	}

	LevelOptimizer_RebuildStreamDB(zptr, streamdb);
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
		if (config.numLoadLevels > 0) {

			for (char c : *config.listLoadLevels) {
				if ((c >= 'a' && c <= 'z') || c == '_')
					continue;
				atlog("FATAL ERROR: Config file's load_level property may only have lowercase letters and underscores");
				return "";
			}

			atlog("Found valid config file");
			return *config.listLoadLevels;
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

void ZipLevel(const fspath& modfolder, const fspath& leveldir, const fspath& gamedir) {

	std::string load_folder = LevelPackager_GetArchiveDir(modfolder);
	if (!load_folder.size())
		return;
	std::string zip_folder = "modarchives/";
	zip_folder.append(load_folder);
	zip_folder.push_back('/');

	std::string outpath = (modfolder / "AtlanPackage_InstallOnce.zip").string();
	mz_zip_archive ziparchive;
	mz_zip_archive* zptr = &ziparchive;

	mz_zip_zero_struct(zptr);
	if (!mz_zip_writer_init_file(zptr, outpath.c_str(), 0)) {
		atlog("ERROR: Failed to open output zip file");
		return;
	}

	if (!LevelPackager_AddArchives(zptr, leveldir, zip_folder, gamedir)) {
		atlog("Level packaging failed due to fatal error in AddArchives");
		mz_zip_writer_finalize_archive(zptr);
		mz_zip_writer_end(zptr);
		return;
	}

	LevelPackager_AddVirtualTextures(zptr, modfolder);

	LevelPackager_AddConfig(zptr);

	if (!mz_zip_writer_finalize_archive(zptr)) {
		atlog("FATAL ERROR: Failed to finalize zip archive");
	}
	mz_zip_writer_end(zptr);

	atlog("\nLevel Packaging Completed");
}