#include "entityslayer/Oodle.h"
#include "entityslayer/EntityParser.h"
#include "archives/ResourceStructs.h"
#include "archives/PackageMapSpec.h"
#include "atlan/AtlanLogger.h"
#include "DeserialMain.h"
#include <iostream>
#include <fstream>
#include <set>
#include <thread>

typedef std::set<std::string> restypeset_t;

struct configdata_t {
	fspath inputdir = "";
	fspath outputdir = "";
	bool run_extractor = true;
	bool run_deserializer = true;

	restypeset_t restypes;

	deserialconfig_t dsconfig;

};

/*
* CONSOLIDATED RESOURCE EXTRACTOR FUNCTION
*/
void ExtractorMain() {
	/*
	* REMEMBER TO UPDATE VERSION NUMBER
	*/
	atlog << "Atlan Consolidated Resource Extractor v2.0 by FlavorfulGecko5\n";

	/*
	* Parse and validate config file
	*/
	configdata_t config;
	try
	{
		#ifdef _DEBUG
		#define configpath "extractor_config_debug.txt"
		#else
		#define configpath "extractor_config.txt"
		#endif

		EntityParser parser(configpath, ParsingMode::PERMISSIVE);

		EntNode& root = *parser.getRoot();
		EntNode& core = root["core"];

		config.inputdir = core["input_folder"].getValueUQ();
		config.outputdir = core["output_folder"].getValueUQ();

		if (!std::filesystem::is_directory(config.inputdir)) {
			atlog << "FATAL ERROR: " << config.inputdir << " is not a valid directory\n"
				<< "Did you remember to set your input/output folders in " << configpath << "?";
			return;
		}
		if (!std::filesystem::is_directory(config.outputdir)) {
			atlog << "FATAL ERROR: " << config.outputdir << " is not a valid directory"
				<< "Did you remember to set your input/output folders in " << configpath << "?";
			return;
		}

		config.inputdir = std::filesystem::absolute(config.inputdir);
		config.outputdir = std::filesystem::absolute(config.outputdir);

		if (config.outputdir.string().size() >= 16) {
			atlog << "FATAL ERROR: Output directory must be less than 16 characters.\n"
				<< "This is to prevent export errors due to long filepaths.\n"
				<< "Your output directory " << config.outputdir << " is " << config.outputdir.string().size() << " characters";
			return;
		}

		if (!core["run_extractor"].ValueBool(config.run_extractor)) {
			atlog << "WARNING: Failed to read config bool core/run_extractor: assuming default\n";
		}
		if(!core["run_deserializer"].ValueBool(config.run_deserializer)) {
			atlog << "WARNING: Failed to read config bool core/run_deserializer: assuming default\n";
		}


		EntNode& restypes = root["extractor"]["resource_types"];
		for (int i = 0; i < restypes.getChildCount(); i++) {
			EntNode& rt = *restypes.ChildAt(i);

			if(rt.IsComment())
				continue;

			config.restypes.insert(std::string(rt.getNameUQ()));
		}
		atlog << "Found " << config.restypes.size() << " resource types\n";

		/* Deserialization Settings */
		EntNode& deserial = root["deserializer"];
		if(!deserial["deserialize_entity_defs"].ValueBool(config.dsconfig.deserial_entitydefs)) {
			atlog << "WARNING: Failed to read config bool deserializer/deserialize_entity_defs: assuming default\n";
		}
		if(!deserial["deserialize_logic_decls"].ValueBool(config.dsconfig.deserial_logicdecls)) {
			atlog << "WARNING: Failed to read config bool deserializer/deserialize_logic_decls: assuming default\n";
		}
		if(!deserial["deserialize_level_files"].ValueBool(config.dsconfig.deserial_mapentities)) {
			atlog << "WARNING: Failed to read config bool deserializer/deserialize_level_files: assuming default\n";
		}
		if (!deserial["remove_binary_files"].ValueBool(config.dsconfig.remove_binaries)) {
			atlog << "WARNING: Failed to read config bool deserializer/remove_binary_files: assuming default\n";
		}
		if (!deserial["add_indentation"].ValueBool(config.dsconfig.indent)) {
			atlog << "WARNING: Failed to read config bool deserializer/add_indentation: assuming default\n";
		}
		if (!deserial["include_originals"].ValueBool(config.dsconfig.include_original)) {
			atlog << "WARNING: Failed to read config bool deserializer/include_originals: assuming default\n";
		}


	}
	catch(...) {
		atlog << "FATAL ERROR: failed to parse " << configpath << "\n";
		return;
	}

	/*
	* Read and verify PackageMapSpec data
	*/
	config.outputdir /= "atlan";
	std::vector<std::string> packages = PackageMapSpec::GetPrioritizedArchiveList(config.inputdir, false);

	if (packages.empty()) {
		atlog << "FATAL ERROR: Could not find PackageMapSpec.json\n"
			<< "Did you enter the correct path to your Dark Ages folder?";
		return;
	}
	else {
		atlog << "Found DOOM The Dark Ages Folder\n"
			<< "Dumping data to " << config.outputdir << "\n";
	}
	std::filesystem::create_directories(config.outputdir);

	/*
	* Download and load Oodle
	* (Do it here so it doesn't get downloaded to the wrong folder if install folder is input wrong)
	*/
	const fspath oo2corepath = config.inputdir / "oo2core_9_win64.dll";
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
	if (!Oodle::init(oo2corepath.string().c_str())) {
		atlog << "FATAL ERROR: Failed to initialize " << oo2corepath << "\n";
		return;
	}


	if (config.run_extractor) {
		atlog << "Performing resource extraction\n";

		const fspath basepath = config.inputdir / "base";
		std::set<std::string> extractedfiles;

		size_t compsize = 24000;
		size_t decompsize = 24000;
		char* compbuffer = new char[compsize];
		char* decompbuffer = new char[decompsize];

		for(size_t i = 0; i < packages.size(); i++) {
			fspath respath = basepath / packages[i];
			int filecount = 0;

			atlog << "Extracting from " << respath.filename() << "\n";

			ResourceArchive archive;
			Read_ResourceArchive(archive, respath.string(), RF_SkipData);
			std::ifstream archivestream(respath, std::ios_base::binary);

			for(uint32_t entryindex = 0; entryindex < archive.header.numResources; entryindex++) {
				const ResourceEntry& e = archive.entries[entryindex];

				const char* typestring, *namestring;
				Get_EntryStrings(archive, e, typestring, namestring);

				// Don't extract files with undesired types
				if (config.restypes.count(typestring) == 0)
					continue;

				// Only proceed if a higher-priority archive doesn't have this file
				{
					std::string setstring = typestring;
					setstring.push_back('/');
					setstring.append(namestring);

					
					if (extractedfiles.count(setstring) != 0)
						continue;
					extractedfiles.insert(setstring);
					filecount++;
				}

				// Hacky fix for logic object descriptors
				//std::string weirdpathfixup = namestring;
				//if (strcmp(typestring, "logicObjectDescriptor") == 0) {
				//	for(char& c : weirdpathfixup) {
				//		if(c == ':')
				//			c = '@';
				//	}
				//	namestring = weirdpathfixup.data();
				//}

				// Setup the output path
				fspath output_path = (config.outputdir / typestring) / namestring;
				{
					if (!output_path.has_extension()) {
						output_path.replace_extension(".bin");
					}
					std::filesystem::create_directories(output_path.parent_path());

					if (output_path.string().length() > 250)
						atlog << "WARNING: Filepath " << output_path << " exceeding safe limit. Unexpected behavior may occur\n";
				}


				// Get the entry data
				ResourceEntryData_t entrydata = Get_EntryData(e, archivestream, compbuffer, compsize, decompbuffer, decompsize);
				if (entrydata.returncode != EntryDataCode::OK) {
					if(entrydata.returncode == EntryDataCode::UNKNOWN_COMPRESSION) {
						atlog << "ERROR: Unknown compression format " << e.compMode << " on file " << output_path << "\n";
					}
					else {
						atlog << "ERROR: Failure code " << static_cast<int>(entrydata.returncode) << " on file " << output_path << "\n";
						continue;
					}
				}

				// Write the file
				std::ofstream outputstream(output_path, std::ios_base::binary);
				outputstream.write(entrydata.buffer, entrydata.length);
				outputstream.close();
			}

			atlog << "Extracted " << filecount << " files from archive\n";
		}

		delete[] compbuffer;
		delete[] decompbuffer;

		atlog << "Extraction Complete: " << extractedfiles.size() << " files extracted in total\n";
	}
	else {
		atlog << "Skipping resource extraction\n";
	}

	if(config.run_deserializer) {
		Deserializer::DeserialMain(config.inputdir, config.outputdir, config.dsconfig);
	}
	else {
		atlog << "Skipping deserialization\n";
	}
}

int main(int argc, char* argv[]) {
	#define logpath "extractor_log.txt"

	#ifdef _DEBUG
	AtlanLogger::init(logpath);
	ExtractorMain();
	AtlanLogger::exit();
	#else

	try {
		AtlanLogger::init(logpath);
		ExtractorMain();
	}
	catch (std::exception e) {
		atlog << "\n\nFATAL ERROR: An unexpected crash has occurred\n"
			<< "This may have left your extracted files incomplete or corrupted.\n"
			<< "Error Message: " << e.what();
	}

	atlog << "\n\nThis window will close in 10 seconds\n";
	atlog << "Output written to " << logpath << "\n";
	AtlanLogger::exit();
	
	std::this_thread::sleep_for(std::chrono::seconds(10));
	#endif
}