#pragma once
#include <filesystem>
#include <unordered_map>
#include "archives/ResourceEnums.h"

struct ModDef;
struct ModFile;

typedef std::filesystem::path fspath;

enum AllowModFile : uint8_t {
	allow_mod_no = 0,
	allow_mod_yes = 1
};

struct resourcetypeinfo_t {
	std::string_view typestring;
	ResourceType typeenum;
	AllowModFile allow;
};

struct ModDef {
	int loadPriority = 0;
	bool IsUnzipped = false; // Is this the global unzipped mod?
	std::string modName;
	std::vector<ModFile> modFiles;
};

struct ModFile {
	const resourcetypeinfo_t* typedata = nullptr;
	ModDef* parentMod = nullptr;
	void* dataBuffer = nullptr;
	size_t dataLength = 0;
	std::string realPath;   // The verbatim path from the zip file or mods folder
	std::string assetPath;  // Path that will be used as the resource name
	uint64_t defaulthash;     // For resources types with a streamdb hash 
	uint32_t resourceVersion; // For mapentities since they span multiple versions
};

inline void ModFile_Free(ModFile& mfile) {
	delete[] mfile.dataBuffer;
}

inline void ModDef_Free(ModDef& mod) {
	for (ModFile& f : mod.modFiles) {
		ModFile_Free(f);
	}
	mod.modFiles.clear();
}

namespace ModReader {
	
	void ReadLooseMod(ModDef& readto, const fspath& modsfolder, const std::vector<fspath>& pathlist, int argflags);
	void ReadZipMod(ModDef& readto, const fspath& zipPath, int argflags);
}