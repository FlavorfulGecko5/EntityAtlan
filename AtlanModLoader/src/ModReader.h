#pragma once
#include <filesystem>
#include <unordered_map>
#include "archives/ResourceEnums.h"
#include "archives/idImage.h"
#include "miniz/miniz.h"

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

struct ModDef {
	int loadPriority = 0;
	bool IsUnzipped = false; // Is this the global unzipped mod?
	bool ActiveZip = false; // If true, zip archive is alive
	std::string modName;
	std::vector<ModFile> modFiles;
	mz_zip_archive zipfile;

	~ModDef();

	ModDef() {}
	ModDef(const ModDef& other) = delete;
	void operator=(const ModDef& other) = delete;
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
	if (ActiveZip) {
		mz_zip_reader_end(&zipfile);
	}
}

struct JustInTimeBuffer_t {
	char* buffer = nullptr;
	size_t filelength = 0;
	size_t maxcapacity = 0;

	~JustInTimeBuffer_t() {
		delete[] buffer;
	}
};

// Data aggregated from all mod config files
struct GlobalConfig_t {

	struct mapres_t {
		std::vector<std::string> entries; // Entries to insert
		bool LoadAll = false;
	};

	std::unordered_map<std::string, mapres_t> mapresinfo; 

	std::vector<std::string> mapspec;
};

namespace ModReader {
	void ReadLooseModv2(ModDef& readto, const fspath modsfolder, const fspath& gamedir, GlobalConfig_t& cfg);
	void ReadZipMod(ModDef& readto, const fspath& zipPath, GlobalConfig_t& cfg);

	// Used for Just-In-Time loading of large zipped mod files
	bool LoadModData(ModFile& modfile, JustInTimeBuffer_t& buffer);
}

namespace ModBuilder {
	void BuildAudioArchives(const fspath soundsfolder, const std::vector<ModFile*>& samplefiles);
}
