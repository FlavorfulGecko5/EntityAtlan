#include "GlobalConfig.h"
#include "ModReader.h"
#include "archives/PackageMapSpec.h"
#include "entityslayer/Oodle.h"
#include "hash/HashLib.h"
#include "io/BinaryReader.h"
#include "archives/ResourceStructs.h"
#include <set>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <thread>
#include <cassert>

#ifndef _DEBUG
#undef assert
#define assert(OP) (OP)
#endif

/*
* Resource Type Strings that will be placed at the start of the string table
*/
const char* StringTableStart[] = { "rs_streamfile", "entityDef" };

void BuildArchive(const std::vector<const ModFile*>& modfiles, fspath outarchivepath) {
	ResourceArchive archive;
	ResourceHeader& h = archive.header;
	
	/*
	* Header Constants
	*/
	h.magic[0] = 'I'; h.magic[1] = 'D'; h.magic[2] = 'C'; h.magic[3] = 'L';
	h.version = ARCHIVE_VERSION;
	h.flags = 0;
	h.numSegments = 1;
	h.segmentSize = 1099511627775UL;
	h.metadataHash = 0;
	h.numSpecialHashes = 0;
	h.numMetaEntries = 0;
	h.metaEntriesSize = 0;
	h.resourceEntriesOffset = sizeof(ResourceHeader);
	h.numResources = static_cast<uint32_t>(modfiles.size());
	

	/*
	* Build String Chunk
	*/
	{
		h.stringTableOffset = h.resourceEntriesOffset + h.numResources * sizeof(ResourceEntry);
		archive.stringChunk.numStrings = sizeof(StringTableStart) / sizeof(StringTableStart[0]) + modfiles.size();
		archive.stringChunk.offsets = new uint64_t[archive.stringChunk.numStrings];
		h.stringTableSize = static_cast<uint32_t> (
			sizeof(uint64_t) // String Count Variable
			+ archive.stringChunk.numStrings * sizeof(uint64_t) // Offsets Section (8 bytes per string)
		);
		/*
		* For the sake of simplicity, we write the string blob to a resizeable array, then copy it over
		* to the final data buffer when finished
		*/
		std::string stringblob;
		stringblob.reserve(modfiles.size() * 75);
		uint64_t runningIndex = 0, runningOffset = 0;

		for(int i = 0; i < sizeof(StringTableStart) / sizeof(StringTableStart[0]); i++) {
			const char* current = StringTableStart[i];
			size_t len = strlen(current);

			archive.stringChunk.offsets[runningIndex++] = runningOffset;
			runningOffset+= len + 1; // Null char

			stringblob.append(current, len);
			stringblob.push_back('\0');
		}
		for(const ModFile* f : modfiles) {
			archive.stringChunk.offsets[runningIndex++] = runningOffset;
			runningOffset += f->assetPath.length() + 1;

			stringblob.append(f->assetPath);
			stringblob.push_back('\0');
		}
		assert(runningOffset == stringblob.length());
		assert(runningIndex == archive.stringChunk.numStrings);
		h.stringTableSize += static_cast<uint32_t>(runningOffset);
		archive.stringChunk.paddingCount = 8 - h.stringTableSize % 8;
		h.stringTableSize += static_cast<uint32_t>(archive.stringChunk.paddingCount);

		/*
		* Copy over blob
		*/
		archive.stringChunk.dataBlock = new char[stringblob.length()];
		memcpy(archive.stringChunk.dataBlock, stringblob.data(), stringblob.length());
	}


	/*
	* Build String Indices
	*/
	{
		h.numStringIndices = static_cast<uint32_t>(modfiles.size() * 2);
		archive.stringIndex = new uint64_t[h.numStringIndices];

		uint64_t* ptr = archive.stringIndex;
		for (uint64_t i = 0; i < modfiles.size(); i++) {
			const ModFile& f = *modfiles[i];
			*ptr = static_cast<uint64_t>(f.assetType);
			*(ptr + 1) = i + sizeof(StringTableStart) / sizeof(StringTableStart[0]);
			ptr += 2;
		}
	}

	/*
	* Build Dependencies
	* TODO: This section (and likely the string table) will need to be heavily revised.
	* For now it's fine, since rs_streamfiles have no dependencies. But entitydefs do
	*/

	h.resourceDepsOffset = h.stringTableOffset + h.stringTableSize;
	h.numDependencies = 0;
	h.numDepIndices = 0;
	h.metaEntriesOffset = h.resourceDepsOffset;
	h.resourceSpecialHashOffset = h.resourceDepsOffset + h.numDependencies * sizeof(ResourceDependency);

	
	/*
	* Configure IDCL Size and Data Offset
	*/
	archive.idclOffset = h.resourceDepsOffset + h.numDependencies * sizeof(ResourceDependency) 
		+ h.numDepIndices * sizeof(uint32_t) + h.numStringIndices * sizeof(uint64_t);
	archive.idclSize = 4;
	if(archive.idclOffset % 8 == 0) // Ensure the data offset has an 8 byte alignment
		archive.idclSize += 4;
	h.dataOffset = archive.idclOffset + archive.idclSize;
	assert(h.dataOffset % 8 == 0);


	/*
	* Build the resource entries
	*/
	archive.entries = new ResourceEntry[modfiles.size()];
	uint64_t runningDataOffset = h.dataOffset;
	for(size_t i = 0; i < modfiles.size(); i++) {
		ResourceEntry& e = archive.entries[i];
		const ModFile& f = *modfiles[i];

		/*
		* Set universal values
		*/
		e.resourceTypeString = 0;
		e.nameString = 1;
		e.descString = -1;
		e.strings = i * 2;
		e.specialHashes = 0;
		e.metaEntries = 0;
		e.reserved0 = 0;
		e.reserved2 = 0;
		e.reservedForVariations = 0;
		e.numStrings = 2;
		e.numSources = 0;
		e.numSpecialHashes = 0;
		e.numMetaEntries = 0;

		// TODO: Does this need to be non-zero? Probably not, but monitor
		// TODO: Padding? Probably not necessary?
		e.generationTimeStamp = 0; 

		/*
		* Set values that vary based on resource type
		*/
		if (f.assetType == ModFileType::rs_streamfile) {
			e.depIndices = 0;
			e.version = 0;
			e.flags = 0;
			e.compMode = 0; 
			e.variation = 0;
			e.numDependencies = 0;
			e.dataSize = f.dataLength;
			e.uncompressedSize = f.dataLength;
			e.dataCheckSum = HashLib::ResourceMurmurHash(std::string_view(static_cast<char*>(f.dataBuffer), f.dataLength));
			e.defaultHash = e.dataCheckSum;

			e.dataOffset = runningDataOffset;
			runningDataOffset += e.dataSize;

			// TODO: There's a fair bit of padding between each resource data block.
			// At a minimum, a data block has 8-byte alignment. It's unknown what the implications of ignoring
			// these practices are
			runningDataOffset += 8 - runningDataOffset % 8;
		}
		else {
			atlog << "\nERROR: Unsupported resource type made it into build";
			return;
		}
	}

	Audit_ResourceArchive(archive);

	/*
	* Write the archive
	*/
	#define rc(PARM) reinterpret_cast<char*>(PARM)

	std::ofstream writer(outarchivepath, std::ios_base::binary);
	writer.write(rc(&archive.header), sizeof(ResourceHeader));
	writer.write(rc(archive.entries), sizeof(ResourceEntry) * h.numResources);

	// String Chunk
	uint64_t blobSize = h.stringTableSize - sizeof(uint64_t) - sizeof(uint64_t) * archive.stringChunk.numStrings - archive.stringChunk.paddingCount;
	writer.write(rc(&archive.stringChunk.numStrings), sizeof(uint64_t));
	writer.write(rc(archive.stringChunk.offsets), archive.stringChunk.numStrings * sizeof(uint64_t));
	writer.write(archive.stringChunk.dataBlock, blobSize);
	for(uint64_t i = 0; i < archive.stringChunk.paddingCount; i++)
		writer.put('\0');

	// Dependencies
	writer.write(rc(archive.dependencies), h.numDependencies * sizeof(ResourceDependency));
	writer.write(rc(archive.dependencyIndex), h.numDepIndices * sizeof(uint32_t));
	writer.write(rc(archive.stringIndex), h.numStringIndices * sizeof(uint64_t));

	// IDCL
	writer.write("IDCL", 4);
	for(int i = 0; i < archive.idclSize - 4; i++);
		writer.put('\0');

	// Write data chunks
	for (size_t i = 0; i < modfiles.size(); i++) {
		ResourceEntry& e = archive.entries[i];
		const ModFile& f = *modfiles[i];

		writer.seekp(e.dataOffset, std::ios_base::beg);
		writer.write(rc(f.dataBuffer), f.dataLength);
	}
	writer.close();

}

#define MODDED_TIMESTAMP 123456

void RebuildContainerMask(const fspath metapath, const fspath newarchivepath) {
	// Read the entire archive into memory
	BinaryOpener open(metapath.string());
	assert(open.Okay());

	
	// Get addresses of relevant data pieces
	char* archive = const_cast<char*>(open.ToReader().GetBuffer());
	ResourceHeader* h = reinterpret_cast<ResourceHeader*>(archive);
	ResourceEntry* e = reinterpret_cast<ResourceEntry*>(archive + sizeof(ResourceHeader));
	char* compressed = archive + e->dataOffset;

	// A few checks to ensure everything is normal
	assert(h->numResources == 1);
	assert(h->dataOffset == e->dataOffset);
	//assert(e->compMode == 2); COULD CHANGE TO UNCOMPRESSED BETWEEN UPDATES
	assert(e->defaultHash == e->dataCheckSum);

	// TODO: If adding multiple archives, must do this for every archive
	// Get data we'll be inserting into container mask
	containerMaskEntry_t newentry = GetContainerMaskHash(newarchivepath);

	// Number of uint64_t's in our bitmask.
	// Ensure at least 1 just incase having 0 is bad
	uint32_t bitmasklongs = static_cast<uint32_t>(newentry.numResources / 64 + (newentry.numResources % 64 ? 1 : 0) + 1);

	// Hash + bitmasklongs + byte count of our bitmask
	size_t extraSize = sizeof(uint64_t) + sizeof(uint32_t) + bitmasklongs * sizeof(uint64_t);

	// Decompress the Oodle-compressed container mask
	char* decomp = new char[e->uncompressedSize + extraSize];
	if (e->compMode == 2) {
		if (!Oodle::DecompressBuffer(compressed, e->dataSize, decomp, e->uncompressedSize)) {
			atlog << "ERROR: FAILED TO DECOMPRESS CONTAINER MASK\n";
			return;
		}
	}
	else {
		assert(e->compMode == 0);
		memcpy(decomp, compressed, e->dataSize);
	}


	// Important file offsets
	uint32_t* hashCount = reinterpret_cast<uint32_t*>(decomp);
	uint64_t* newHash = reinterpret_cast<uint64_t*>(decomp + e->uncompressedSize);
	uint32_t* blobSize = reinterpret_cast<uint32_t*>(decomp + e->uncompressedSize + sizeof(uint64_t));
	uint64_t* bitmask = reinterpret_cast<uint64_t*>(decomp + e->uncompressedSize + sizeof(uint64_t) + sizeof(uint32_t));

	// Add new bitmask to the file
	*hashCount = *hashCount + 1;
	*newHash = newentry.hash;
	*blobSize = bitmasklongs;
	for(size_t i = 0; i < bitmasklongs; i++) {
		*(bitmask + i) = -1;
	}

	/*
	* - Unnecessary: Change data offset
	* - Change data size
	* - Change uncompressed size
	* - Change defaultHash and data Check Sum
	* - Change compMode to 0 (if we opt not to recompress it)
	*/
	// Modify the ResourceEntry - disabling compression on the file should be fine
	e->dataSize = e->uncompressedSize + extraSize;
	e->uncompressedSize = e->dataSize;
	e->compMode = 0;
	e->defaultHash = HashLib::ResourceMurmurHash(std::string_view(decomp, e->dataSize));
	e->dataCheckSum = e->defaultHash;

	// IMPORTANT: Our gameupdate detection system relies on checking this value
	// to determine if meta.resources is modded or not.
	e->generationTimeStamp = MODDED_TIMESTAMP; 

	// Rewrite the file
	std::ofstream writer(metapath, std::ios_base::binary);
	writer.write(archive, h->dataOffset);
	writer.write(decomp, e->dataSize);

	delete[] decomp;
}

bool IsModded_MapSpec(const fspath& path) {
	BinaryOpener open(path.string());
	BinaryReader reader = open.ToReader();

	std::string_view view(reader.GetBuffer(), reader.GetLength());
	return view.find("modarchives") != std::string_view::npos;
}

bool IsModded_Meta(const fspath& path) {
	ResourceEntry e;

	std::ifstream reader(path, std::ios_base::binary);
	reader.seekg(sizeof(ResourceHeader), std::ios_base::beg);
	reader.read(reinterpret_cast<char*>(&e), sizeof(ResourceEntry));

	return e.generationTimeStamp == MODDED_TIMESTAMP;
}

void InjectorLoadMods(const fspath gamedir, const int argflags) {
	fspath modsdir = gamedir / "mods";
	fspath loosezippath = modsdir / "TEMPORARY_unzipped_modfiles.zip";
	fspath basedir = gamedir / "base";
	fspath outdir = basedir / "modarchives";
	fspath outarchivepath = outdir / "common_mod.resources";
	fspath pmspath = basedir / "packagemapspec.json";
	fspath metapath = basedir / "meta.resources";

	std::vector<fspath> loosemodpaths;
	std::vector<fspath> zipmodpaths;
	loosemodpaths.reserve(20);
	zipmodpaths.reserve(20);


	/*
	* CREATE / RESTORE BACKUPS; CLEANUP PREVIOUS INJECTION FILES
	*/
	{
		using namespace std::filesystem;

		std::error_code lastCode;
		//atlog << "Managing backups and cleaning up previous injection files.\n";

		#define NUM_BACKUPS 2
		const fspath backedupfiles[NUM_BACKUPS] = {pmspath, metapath};
		bool IsModded[NUM_BACKUPS] = { IsModded_MapSpec(pmspath), IsModded_Meta(metapath)};

		// Handle backups
		for(int i = 0; i < NUM_BACKUPS; i++) {
			const fspath& original = backedupfiles[i];
			const fspath backup = original.string() + ".backup";

			// Ensure the original file exists
			if (!exists(original)) {
				atlog << "ERROR: Could not find " << absolute(original);
				return;
			}

			// If the backup doesn't exist, assume this is a first time setup
			// and copy it no matter what
			if (!exists(backup)) {
				copy_file(original, backup, copy_options::none, lastCode);
			}
			else {
				// The game updated, and the file isn't modded. This means either:
				// 1. An updated version was downloaded
				// 2. meThe file wasn't updated, but is still vanilla
				// Either way, replace the original backup with this new version
				if ((argflags & argflag_gameupdated) && !IsModded[i]) {
					copy_file(original, backup, copy_options::overwrite_existing, lastCode);
				}

				// If this triggers, either:
				// 1. There's no game update detected
				// 2. There's a game update, but the file is modded. This means a new version
				// wasn't downloaded. Hence, we restore from our existing backup
				else {
					copy_file(backup, original, copy_options::overwrite_existing, lastCode);
				}
			}
		}

		// Create input/output directories if they don't exist yet
		if (!exists(modsdir))
			create_directory(modsdir, lastCode);
		if(!exists(outdir))
			create_directory(outdir, lastCode);

		// Delete archives created from previous injections
		std::vector<fspath> filesToDelete;
		filesToDelete.reserve(10);
		for (const directory_entry& dirEntry : directory_iterator(outdir)) {
			if(dirEntry.is_directory())
				continue;

			if(dirEntry.path().extension() == ".resources") {
				filesToDelete.push_back(dirEntry.path());
			}
		}
		for (const fspath& fp : filesToDelete) {
			remove(fp, lastCode);
		}

		// Ensure the temporary loose mod zip doesn't exist
		if (exists(loosezippath)) {
			remove(loosezippath, lastCode);
		}
	}

	if(argflags & argflag_resetvanilla) {
		atlog << "Uninstalled all mods\n";
		return;
	}

	/*
	* GATHER MOD FILE PATHS
	*/
	{
		using namespace std::filesystem;

		// Build list of zip mod files
		for (const directory_entry& dirEntry : directory_iterator(modsdir)) {
			if(dirEntry.is_directory())
				continue;

			if(dirEntry.path().extension() == ".zip")
				zipmodpaths.push_back(dirEntry.path());
		}

		// Build list of loose mod files
		for (const directory_entry& dirEntry : recursive_directory_iterator(modsdir)) {
			if (dirEntry.is_directory())
				continue;

			if (dirEntry.path().extension() != ".zip")
				loosemodpaths.push_back(dirEntry.path());
		}

		//atlog << "\nZipped Paths\n";
		//for(const fspath& f : zipmodpaths)
		//	atlog << f << "\n";
		//atlog << "\nLoose Paths\n";
		//for(const fspath& f : loosemodpaths)
		//	atlog << f << "\n";
		atlog << "\nZipped Mods Found: " << zipmodpaths.size() << " Loose Mods Found: " << loosemodpaths.size() << "\n";
	}
	

	/*
	* READ MOD DATA
	*/

	atlog << "\n\nReading Mods:\n----------\n";

	int totalmods = static_cast<int>(zipmodpaths.size() + 1); // + 1 for the loose mod
	ModDef* realmods = new ModDef[totalmods];

	realmods[0].loadPriority = -999;
	ModReader::ReadLooseMod(realmods[0], loosezippath, loosemodpaths, argflags);
	for (int i = 1; i < totalmods; i++) {
		ModReader::ReadZipMod(realmods[i], zipmodpaths[i - 1], argflags);
	}

	/*
	* BUILD THE SUPER MOD - This is a consolidated "mod" containing the highest
	* priority version of every mod file. All mod file conflicts will be resolved here
	* (conflicts from different mods, or from the same mods - in the case of bad aliasing setups)
	*/
	atlog << "\n\nChecking for Conflicts:\n----------\n";

	std::vector<const ModFile*> supermod;
	{
		// TODO: May need to make this map<ResourceType, map<string, ModFile*>> when
		// we support more than just rs_streamfiles
		std::unordered_map<std::string, const ModFile*> priorityAssets;

		for(int i = 0; i < totalmods; i++) {
			const ModDef& current = realmods[i];

			for(const ModFile& file : current.modFiles) {

				auto iter = priorityAssets.find(file.assetPath);
				if(iter == priorityAssets.end()) {
					priorityAssets.emplace(file.assetPath, &file);
				} 
				else {
					bool replaceMapping = current.loadPriority < iter->second->parentMod->loadPriority;

					atlog << "CONFLICT FOUND: " << file.assetPath
						<< "\n(A):" << current.modName << " - " << file.realPath
						<< "\n(B):" << iter->second->parentMod->modName << " - " << iter->second->realPath
						<< "\nWinner: " << (replaceMapping ? "(A)\n" : "(B)\n---\n");

					if(replaceMapping) {
						iter->second = &file;
					}
				}
			}
		}

		supermod.reserve(priorityAssets.size());
		for (const auto& pair : priorityAssets) {
			supermod.push_back(pair.second);
		}
	}

	/*
	* BUILD THE RESOURCE ARCHIVES
	*/

	if (supermod.size() > 0) {
		BuildArchive(supermod, outarchivepath);
		PackageMapSpec::InjectCommonArchive(gamedir, outarchivepath);
		RebuildContainerMask(metapath, outarchivepath);
	}
	else {
		atlog << "\n\nNo mods will be loaded. All previously loaded mods are removed.\n";
	}

	/*
	* DEALLOCATE EVERYTHING
	*/
	for(int i = 0; i < totalmods; i++) {
		ModDef_Free(realmods[i]);
	}
	delete[] realmods;
}

void InjectorMain(int argc, char* argv[]) {

	atlog << R"(
----------------------------------------------
Atlan Mod Loader v)" << MOD_LOADER_VERSION << R"( for DOOM: The Dark Ages
By FlavorfulGecko5
With Special Thanks to: Proteh, Zwip-Zwap-Zapony, Tjoener, and many other talented modders!
https://github.com/FlavorfulGecko5/EntityAtlan/
----------------------------------------------
)";

	int argflags = 0;
	
	// Do not end in a / or there may be problems when running system commands 
	// (it won't translate string literal slashes to the appropriate slash like it does when using the / operator)
	fspath gamedirectory = "."; 

	/*
	* Parse Command Line Arguments
	*/
	for(int i = 1; i < argc; i++) {
		std::string_view arg = argv[i];

		if(arg == "--verbose") {
			atlog << "ARGS: Verbose Logging Enabled\n";
			argflags |= argflag_verbose;
		}

		else if (arg == "--nolaunch") {
			atlog << "ARGS: Game will not launch after loading mods\n";
			argflags |= argflag_nolaunch;
		}

		else if (arg == "--forceload") {
			atlog << "ARGS: Mod loading will proceed if DarkAgesPatcher fails\n"
				<< "WARNING: Loading mods when DarkAgesPatcher fails may cause the game to permanently crash on startup.\n"
				<< "If this happens, you will need to unload all mods for the game to work again.\n"
				<< "Press ENTER to acknowledge this warning.\n";
			char c = getchar();
			argflags |= argflag_forceload;
		}

		else if(arg == "--gamedir") { // This is for debug builds
			if(++i == argc)
				goto LABEL_EXIT_HELP;
			gamedirectory = argv[i];
			atlog << "ARGS: Custom game directory accepted\n";
		}

		else {
			LABEL_EXIT_HELP:
			atlog << "AtlanModLoader.exe [--verbose] [--nolaunch] [--forceload] [--gamedir <Dark Ages Installation Folder>]\n";
			return;
		}
	}

	const fspath configpath = "./modloader_config.txt";
	const fspath oo2corepath = "./oo2core_9_win64.dll";
	const fspath cachepath = "./modloader_cache.bin";
	const fspath manifestpath = gamedirectory / "base/build-manifest.bin";

	struct LoaderCache_t {
		uint64_t manifesthash = -1;
		uint64_t patchersucceeded = 0; // If > 0, then Dark Ages Patcher worked successfully
	} loadercache, newcache;


	/* Check the game directory is valid */
	if (!std::filesystem::exists(gamedirectory) || !std::filesystem::is_directory(gamedirectory)) {
		atlog << "FATAL ERROR: " << gamedirectory << " is not a valid directory";
		return;
	}

	/*
	* Read the Cache File if it exists
	*/
	if(std::filesystem::exists(cachepath))
	{
		std::ifstream cachereader(cachepath, std::ios_base::binary);
		cachereader.seekg(0, std::ios_base::end);
		if(cachereader.tellg() == sizeof(LoaderCache_t)) {
			cachereader.seekg(0, std::ios_base::beg);
			cachereader.read(reinterpret_cast<char*>(&loadercache), sizeof(LoaderCache_t));
		}
		else {
			atlog << "WARNING: Corrupted Mod Loader Cache file detected. Falling back to defaults\n";
		}
		cachereader.close();
	}

	/*
	* To determine if the game has been updated, we hash part of the build-manifest
	* and compare it with what's on file
	*/
	{
		if (!std::filesystem::exists(manifestpath)) {
			atlog << "FATAL ERROR: Could not find " << manifestpath << "\n";
			return;
		}
		std::ifstream manreader(manifestpath, std::ios_base::binary);

		#define READ_COUNT 256
		char buffer[READ_COUNT];
		manreader.read(buffer, READ_COUNT);
		newcache.manifesthash = HashLib::FarmHash64(buffer, READ_COUNT);
		manreader.close();
	}

	/*
	* Download Oodle Compression dll if it doesn't exist
	* No need to do this with every first time setup
	*/

	if(!std::filesystem::exists(oo2corepath)) {
		// Because nobody ever needed a simple STL function to convert a string to a wide string....
		#define OODLE_URL    "https://github.com/WorkingRobot/OodleUE/raw/refs/heads/main/Engine/Source/Programs/Shared/EpicGames.Oodle/Sdk/2.9.10/win/redist/oo2core_9_win64.dll"
		#define OODLE_URL_W L"https://github.com/WorkingRobot/OodleUE/raw/refs/heads/main/Engine/Source/Programs/Shared/EpicGames.Oodle/Sdk/2.9.10/win/redist/oo2core_9_win64.dll"
		atlog << "Downloading " << oo2corepath << " from " << OODLE_URL << "\n";

		bool success = Oodle::Download(OODLE_URL_W, oo2corepath.wstring().c_str());
		if (!success) {
			atlog << "FATAL ERROR: Failed to download " << oo2corepath << "\n";
			return;
		}
		atlog << "Download Complete (Oodle is a file decompression library)\n";
	}
	if (!Oodle::init()) {
		atlog << "FATAL ERROR: Failed to initialize " << oo2corepath << "\n";
		return;
	}


	bool gameUpdated = newcache.manifesthash != loadercache.manifesthash;
	if (gameUpdated) {
		argflags |= argflag_gameupdated;
		atlog << "Game has been updated, or mod loader cache file could not be found. Performing update operations\n";
	}

	bool runPatcher = gameUpdated || loadercache.patchersucceeded == 0;

	/*
	* Run Proteh's Dark Ages Patcher
	*/
	if(runPatcher)
	{
		// Do not put slashes in any string literals here
		const fspath patcherpath = gamedirectory / "DarkAgesPatcher.exe";
		const fspath exepath = gamedirectory / "DOOMTheDarkAges.exe";

		atlog << "\nRunning DarkAgesPatcher.exe by Proteh\n";
		if(!std::filesystem::exists(patcherpath))
		{
			atlog << "FATAL ERROR: Could not find " << patcherpath << "\n";
			return;
		}

		//atlog << "~" << patcherpath << "~" << exepath << "~\n";
		std::string updateCommand = patcherpath.string() + " --update";
		std::string patchCommand = patcherpath.string() + " --patch ";
		patchCommand.append(exepath.string());

		struct {
			uint16_t code;
			uint8_t successfulpatches;
			uint8_t failedpatches;
		} returndata;

		//atlog << patchCommand;
		system(updateCommand.c_str());
		*reinterpret_cast<int*>(&returndata) = system(patchCommand.c_str());

		bool patchsuccess;
		switch(returndata.code) {
			case 6: // Executable already fully patched
			patchsuccess = true;
			break;

			case 0: // Patches applied - may be partial or complete success
			patchsuccess = returndata.failedpatches == 0;
			break;

			default: // Failure for other reasons
			patchsuccess = false;
			break;
		}

		atlog << "Patcher Return Codes: " << returndata.code << " " << returndata.successfulpatches << " " << returndata.failedpatches << "\n";
		if (!patchsuccess) {
			
			atlog << "ERROR: Dark Ages Patcher partially or fully failed to patch your game executable.\n";

			if(argflags & argflag_forceload) {
				atlog << "Proceeding with mod loading due to --forceload\n";
			}
			else {
				// Should be fine to abort right here without saving the cache file - like this injection attempt never happened
				atlog << "Loading mods when DarkAgesPatcher fails may cause the game to permanently crash on startup.\n"
					<< "Mod loading is being aborted out of caution.\n"
					<< "At your own risk, you may run the mod loader with --forceload to bypass this safety measure.\n";
				return;
			}
		}

		newcache.patchersucceeded = patchsuccess;
	}
	else {
		newcache.patchersucceeded = loadercache.patchersucceeded;
	}

	/*
	* Write the new loader cache file
	*/
	if(memcmp(&loadercache, &newcache, sizeof(LoaderCache_t)) != 0)
	{
		std::ofstream cachewriter(cachepath, std::ios_base::binary);
		cachewriter.write(reinterpret_cast<char*>(&newcache), sizeof(LoaderCache_t));
		cachewriter.close();
	}

	/*
	* Run the mod loader
	*/
	InjectorLoadMods(gamedirectory, argflags);

	/*
	* Finish up
	*/
	atlog << "\nMod Loading Complete\n----------\n";

	if (argflags & argflag_nolaunch) {
		atlog << "Game will not launch due to nolaunch argument\n";
		return;
	}

	const fspath absdir = std::filesystem::absolute(gamedirectory);
	if(std::filesystem::exists(gamedirectory / "steam_api64.dll")) {
		atlog << "Launching Game with Steam\n";
		std::system("start \"\" \"steam://run/3017860//\"");
	}
	else {
		atlog << "Could not determine how to automatically launch your game\n"
			<< "Please launch it manually.\n";
	}
	
}

int main(int argc, char* argv[]) {

	#define LOGPATH "modloader_log.txt"

	try {
		AtlanLogger::init(LOGPATH);
		InjectorMain(argc, argv);
	}
	catch (std::exception e) {
		atlog << "\n\nFATAL ERROR: An unexpected crash has occurred\n"
			<< "This sudden crash may have left you with broken game files.\n"
			<< "Please re-run the mod loader with no mods loaded to restore them\n"
			<< "Or use \"Restore Backups\" in Dark Ages Mod Manager\n"
			<< "Error Message: " << e.what();
	}
	
	atlog << "\n\nThis window will close in 10 seconds\n";
	std::cout << "Output written to " << LOGPATH << "\n";
	AtlanLogger::exit();

	#ifndef _DEBUG
	std::this_thread::sleep_for(std::chrono::seconds(10));
	#endif
}