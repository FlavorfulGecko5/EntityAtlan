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

#define RESTYPE_NOLOAD "noload"

typedef std::filesystem::path fspath;

// Key is the start of the file path
const std::unordered_map<std::string, resourcetypeinfo_t> ValidResourceTypes = {
	{"rs_streamfile", {"rs_streamfile", rt_rs_streamfile, allow_mod_yes}},
	{"decls",         {"rs_streamfile", rt_rs_streamfile, allow_mod_yes, "generated/decls/"}},
	{"entityDef",     {"entityDef",     rt_entityDef,     allow_mod_yes}},
	{"logicClass",    {"logicClass",    rt_logicClass,    allow_mod_yes}},
	{"logicEntity",   {"logicEntity",   rt_logicEntity,   allow_mod_yes}},
	{"logicFX",       {"logicFX",       rt_logicFX,       allow_mod_yes}},
	{"logicLibrary",  {"logicLibrary",  rt_logicLibrary,  allow_mod_yes}},
	{"logicUIWidget", {"logicUIWidget", rt_logicUIWidget, allow_mod_yes}},
	{"mapentities",   {"mapentities",   rt_mapentities,   allow_mod_yes}},
	{"image",         {"image",         rt_image,         allow_mod_yes}},

	// Audio will be handled differently by the loader
	{"audio", {"audio", rt_audio, allow_mod_yes}}
};

/*
* Check if version requirement is met. If it isn't, we should skip reading this mod
*/
bool ModReader_HasRequiredVersion(ModDef& moddef, const AtlanModConfig& cfg) {
	if (MOD_LOADER_VERSION < cfg.requiredVersion) {
		atlog << "ERROR: Mod requires Mod Loader Version " << cfg.requiredVersion << " or greater. (Your version is " << MOD_LOADER_VERSION << ")\n";
		return false;
	}
	return true;
}

bool ModReader_ValidatePath(ModFile& modfile, const AtlanModConfig& cfg, std::string* EncodingInfo = nullptr) {
	std::string TypeString, NameString;
	cfg.GetNormalizedName(modfile.realPath, TypeString, NameString);

	if (TypeString.length() == 0) {
		atlog << "ERROR: Missing resource type string for file " << modfile.realPath << "\n";
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
			atlog << "ERROR: Unsupported resource type for file \"" << modfile.realPath << "\"\n";
			return false;
		}

		if (!iter->second.allow) {
			atlog << "ERROR: Disabled resource type for file \"" << modfile.realPath << "\"\n";
			return false;
		}
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
			atlog << "ERROR: File " << modfile.realPath << " requires a number at the end of it's filename\n";
			return false;
		}
	}
	else {
		if (hasBadChars) {
			atlog << "WARNING: Fixed capital letters or other bad characters in path " << modfile.realPath << "\n";
		}
		if (nameStart >= nameEnd) {
			atlog << "ERROR: File " << modfile.realPath << " has an invalid name\n";
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

void ModReader_ConfirmModFile(ModDef& moddef, ModFile& modfile, int argflags) {
	if (argflags & argflag_verbose) {
		atlog << "OK: " << modfile.realPath << " --> " << modfile.assetPath << "\n";
	}
	else {
		atlog.logfileonly("OK: ").logfileonly(modfile.realPath).logfileonly(" --> ").logfileonly(modfile.assetPath).logfileonly("\n");
	}

	/*
	* Finish processing the file
	*/
	modfile.parentMod = &moddef;
	moddef.modFiles.push_back(modfile);
}

void ModReader::ReadLooseModv2(ModDef& moddef, const fspath modsfolder, const fspath& gamedir, int argflags)
{
	using namespace std::filesystem;

	const size_t modsfolder_length = modsfolder.string().length();
	std::vector<fspath> ModFilePaths;
	AtlanModConfig cfg;

	ModFilePaths.reserve(20);

	moddef.modName = "[Unzipped] ";
	moddef.modName.append(modsfolder.stem().string());
	moddef.IsUnzipped = true;

	atlog << "\n\nReading " << moddef.modName << "\n---\n";

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
				atlog << "Ignoring " CFG_NAME << " inside a subfolder\n";
				continue;
			}

			atlog << "Found " CFG_NAME "\n";
			cfg.TryRead(entrypath.string());
			continue;
		}

		ModFilePaths.push_back(entrypath);
	}

	moddef.loadPriority = -999;
	if (!ModReader_HasRequiredVersion(moddef, cfg)) {
		return;
	}

	atlog << "Found " << ModFilePaths.size() << " Unzipped Mod Files\n";

	idImageEncodingContext EncodingContext;
	idImageEncodingResults EncodingResults;

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
			if (!EncodingContext.m_initialized) {
				atlog << "Initializing Image Encoder\n";

				// Use a faster compression level than the mod packager to improve iteration times.
				if (!EncodingContext.InitializeContext(gamedir.string(), 3)) { 
					return;
				}
			}

			std::string EncodingLog;
			atlog << "Encoding " << modfile.realPath << " (" << modfile.assetPath << ")\n";
			bool result = EncodingContext.EncodeImage(modfile.assetPath, EncodingInfo, FilePath.c_str(), EncodingResults, EncodingLog);
			if(EncodingLog.length())
				atlog << EncodingLog;
			if (!result) {
				continue;
			}

			// Give modfile it's own copy of the data
			modfile.dataLength = EncodingResults.file_length;
			modfile.dataBuffer = new char[modfile.dataLength];
			memcpy(modfile.dataBuffer, EncodingResults.buffer, modfile.dataLength);
		}
		else {
			std::ifstream filereader(FilePath, std::ios_base::binary);
			if (!filereader.good()) {
				atlog << "ERROR: Failed to open file " << modfile.realPath << "\n";
				continue;
			}

			filereader.seekg(0, std::ios_base::end);
			modfile.dataLength = filereader.tellg();
			modfile.dataBuffer = new char[modfile.dataLength];
			filereader.seekg(0, std::ios_base::beg);
			filereader.read((char*)modfile.dataBuffer, modfile.dataLength);
			filereader.close();
		}
		ModReader_ConfirmModFile(moddef, modfile, argflags);
	}
}

void ReadZipMod_Internal(mz_zip_archive* zptr, ModDef& readto, int argflags);

void ModReader::ReadZipMod(ModDef& mod, const fspath& zipPath, int argflags)
{
	atlog << "\n\nReading " << zipPath.filename() << "\n---\n";

	// Open the zip file
	mz_zip_archive zipfile;
	mz_zip_archive* zptr = &zipfile;
	mz_zip_zero_struct(zptr);
	if (!mz_zip_reader_init_file(zptr, zipPath.string().c_str(), 0))
	{
		atlog << "ERROR: Failed to open zip file\n";
		return;
	}

	mod.modName = zipPath.stem().string();
	ReadZipMod_Internal(zptr, mod, argflags);
	mz_zip_reader_end(zptr);
}

void ReadZipMod_Internal(mz_zip_archive* zptr, ModDef& mod, int argflags)
{
	
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
			atlog << "WARNING: Could not find " CFG_NAME "\n";
		}
		else {
			atlog << "Found " CFG_NAME "\n";
			cfg.TryRead(dataBuffer, dataLength);
			delete[] dataBuffer;
		}
	}

	mod.loadPriority = cfg.loadPriority;
	if (!ModReader_HasRequiredVersion(mod, cfg)) {
		return;
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

		void* dataBuffer = nullptr;
		size_t dataLength = 0;
		dataBuffer = mz_zip_reader_extract_to_heap(zptr, i, &dataLength, 0);
		if (!dataBuffer) {
			atlog << "ERROR: Failed to extract file " << modfile.realPath << "\n";
			continue;
		}
		modfile.dataBuffer = dataBuffer;
		modfile.dataLength = dataLength;

		ModReader_ConfirmModFile(mod, modfile, argflags);
	}

	delete[] ZipNameBuffer;
}