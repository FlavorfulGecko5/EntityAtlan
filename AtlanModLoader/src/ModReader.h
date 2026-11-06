#pragma once
#include <filesystem>
#include <unordered_map>
#include "archives/PackageMapSpec.h"
#include "archives/ResourceEnums.h"
#include "archives/idImage.h"
#include "miniz/miniz.h"
#include "atlan/AtlanLib.h"

struct ModDef;
struct ModFile;

typedef std::filesystem::path fspath;

struct resourcetypeinfo_t {
	std::string_view typestring;
	ResourceType typeenum;
	u8 gamebits;    // Games this resource type is supported in
	u32 version_da; // ResourceEntry::version when game is Dark Ages
	u32 version_et; // ResourceEntry::version when game is Eternal
	std::string_view namestart;
};

enum ModType : uint8_t {
	mt_default = 0,
	mt_folder = 1,
	mt_zipfile = 2
};

struct ModDef {
	int loadPriority = 0;
	ModType modtype = mt_default; // Is this the global unzipped mod?
	bool ActiveZip = false; // If true, zip archive is alive
	std::string modName;
	std::vector<ModFile> modFiles;
	mz_zip_archive zipfile;

	~ModDef();

	ModDef() = default;
	ModDef(const ModDef& other) = delete;
	ModDef(const ModDef&& other) = delete;
	void operator=(const ModDef& other) = delete;
	void operator=(const ModDef&& other) = delete;
};

struct ModFile {
	std::string_view typestring;
	ResourceType typeenum;
	bool ownsData = true; // If true, this ModFile has ownership of it's data buffer. If false, it does not and data could be staled
	ModDef* parentMod = nullptr;
	char* dataBuffer = nullptr;
	size_t dataLength = 0;
	std::string realPath;   // The verbatim path from the zip file or mods folder
	std::string assetPath;  // Path that will be used as the resource name
	uint64_t defaulthash = -1;     // For resources types with a streamdb hash 
	uint32_t resourceVersion; // ResourceEntry::version

	int containerIndex = -1; // Index of this file in it's mod container (a zip index or pk5 index)
};

inline void ModFile_Free(ModFile& mfile) {
	if (mfile.ownsData) {
		delete[] mfile.dataBuffer;
	}
}

inline ModDef::~ModDef() {
	for (ModFile& f : modFiles) {
		ModFile_Free(f);
	}
	if (modtype == mt_zipfile && ActiveZip) {
		mz_zip_reader_end(&zipfile);
	}
}

// Data aggregated from all mod config files
struct GlobalConfig_t {

	struct mapres_t {
		std::vector<std::string> entries; // Entries to insert
		bool LoadAll = false;
	};

	std::unordered_map<std::string, mapres_t> mapresinfo; 

	std::unordered_map <std::string, std::vector<ModFile*>> blanginfo;

	// Packagemapspec import statements
	std::vector<std::string> mapspec;

	std::vector<std::string> levelfolders;

	// Packagemapspec addons
	std::vector<MapSpec_t::AddOn> mapspec_addons;

	ModDef* elevatedmod = nullptr;
	const ModDef* totalOverhaulMod = nullptr;

	void ReadLevelConfigs(const fspath& gamedir);
	void AddCommonFiles(bool includeStreamDB);
	std::vector<std::string> GetNewArchives() const;

	GlobalConfig_t() = default;
	GlobalConfig_t(const GlobalConfig_t& other) = delete;
	GlobalConfig_t(const GlobalConfig_t&& other) = delete;
	GlobalConfig_t& operator=(const GlobalConfig_t& other) = delete;
	GlobalConfig_t& operator=(const GlobalConfig_t&& other) = delete;
};

namespace ModReader {
	void ReadLooseModv2(ModDef& readto, const fspath modsfolder, const fspath& gamedir, GlobalConfig_t& cfg);
	void ReadZipMod(ModDef& readto, const fspath& gamedir, const fspath& zipPath, GlobalConfig_t& cfg);

	// Used for Just-In-Time loading of large zipped mod files
	bool LoadModData(ModFile& modfile, charbuffer_t& buffer);
}

namespace ModBuilder {
	void BuildAudioArchives(const fspath soundsfolder, const std::vector<ModFile*>& samplefiles, gamebit_t gameid);
}
