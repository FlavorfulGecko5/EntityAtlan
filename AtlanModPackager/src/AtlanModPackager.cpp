
#include <filesystem>
#include <thread>
#include <vector>
#include <fstream>
#include <unordered_map>
#include "entityslayer/EntityParser.h"
#include "archives/ResourceEnums.h"
#include "atlan/AtlanLogger.h"
#include "ReserialMain.h"
#include "io/BinaryWriter.h"
#include "miniz/miniz.h"
#include "atlan/AtlanOodle.h"

typedef std::filesystem::path fspath;

void PackagerMain()
{
	using namespace std::filesystem;

	const std::unordered_map<std::string, ResourceType> serialtypes = {
		{"entityDef",     rt_entityDef},
		{"logicClass",    rt_logicClass},
		{"logicEntity",   rt_logicEntity},
		{"logicFX",       rt_logicFX},
		{"logicLibrary",  rt_logicLibrary},
		{"logicUIWidget", rt_logicUIWidget},
		{"mapentities",   rt_mapentities}
	};

	std::unordered_map<std::string, std::string> configaliases;

	std::vector<fspath> filepaths;
	const fspath modsfolder = absolute("./mods");

	#define CFG_NAME "darkagesmod.txt"
	fspath configpath; // Will be empty if config was not found

	/* Gather all mod filepaths */
	{
		if(!is_directory(modsfolder)) {
			atlog << "FATAL ERROR: Could not find mods folder\n";
			return;
		}

		// Put this after the mod folder is located, so
		// we don't download Oodle in an incorrect place.
		if (!Oodle::AtlanOodleInit("."))
			return;

		for (const directory_entry& entry : recursive_directory_iterator(modsfolder))
		{
			if(entry.is_directory())
				continue;

			std::string extension = entry.path().extension().string();
			if(extension == ".zip" || extension == ".ZIP")
				continue;

			// If we've found the config file, parse it's aliasing data
			if(entry.path().filename() == CFG_NAME) {
				atlog << "Found " << CFG_NAME << "\n";
				try {
					EntityParser parser(entry.path().string(), ParsingMode::PERMISSIVE);
					EntNode& root = *parser.getRoot();

					EntNode& aliasNode = root["aliasing"];
					for (int i = 0, max = aliasNode.getChildCount(); i < max; i++) {
						EntNode& currentAlias = *aliasNode.ChildAt(i);
						if (currentAlias.IsComment())
							continue;

						// Need to normalize the separators in the alias name to ensure
						// they're correctly matched with their files
						std::string normalizedname(currentAlias.getNameUQ());
						for (char& c : normalizedname) {
							if (c == '\\')
								c = '/';
						}

						configaliases.emplace(normalizedname, currentAlias.getValueUQ());
					}
					if (configaliases.size() > 0)
						atlog << "Found " << configaliases.size() << " alias definitions\n";

				}
				catch (...) {
					atlog << "ERROR: Failed to read " << CFG_NAME << "\n";
				}

				
				configpath = entry.path();
			}

			filepaths.push_back(entry.path());
			//atlog << entry.path() << "\n"; 
		}
	}

	mz_zip_archive zipfile;
	mz_zip_archive* zptr = &zipfile;
	mz_zip_zero_struct(zptr);

	// Keep both values at zero or the finalized zip file will have some sort of offset error
	mz_zip_writer_init_heap(zptr, 0, 0);

	/* iterate through all the files */
	for(const fspath& modfile : filepaths) 
	{
		std::string zippedName = modfile.string().substr(modsfolder.string().size() + 1);
		atlog << "Packaging " << zippedName << "\n";


		std::string typestring = "";
		{
			// Use a separate string for looking up the alias so we still zip the
			// file using it's original name
			std::string queryname = zippedName;

			// Normalize separators to ensure correct alias lookup
			for (char& c : queryname) {
				if (c == '\\')
					c = '/';
			}

			// Use alias path as query string if it exists
			const auto& iter = configaliases.find(queryname);
			if(iter != configaliases.end()) 
				queryname = iter->second;

			// Find the type string
			size_t separator = -1;
			for (size_t i = 0; i < queryname.size(); i++) {
				char c = queryname[i];
				if (c == '/' || c == '\\' || c == '@') {
					separator = i;
					break;
				}
			}

			if (separator != -1) {
				typestring = queryname.substr(0, separator);
			}
		}

		// Fail safe to prevent raw files from a previous package being zipped and overriding the real raw files
		if(typestring == "noload")
			continue;

		const auto& iter = serialtypes.find(typestring);
		if (iter != serialtypes.end()) {

			atlog << "Serializing " << zippedName << "\n";
			ResourceType typeenum = iter->second;
			BinaryWriter serialized(static_cast<size_t>(file_size(modfile) * 0.5));
			Reserializer::Serialize(modfile.string().c_str(), serialized, typeenum);

			std::string bin_name = fspath(zippedName).replace_extension(".bin").string();
			bool result;

			// Oodle Compress Mapentities
			//if(0){
			if (typestring == "mapentities") {
				char* compbuffer = nullptr;
				size_t outputlength = 0, outputBufferLength = 0;

				atlog << "Compressing " << zippedName << "\n";
				if(!Oodle::AtlanCompress(serialized.GetBuffer(), serialized.GetFilledSize(), compbuffer, outputlength, outputBufferLength))
					atlog << "ERROR: Failed to create Atlan Compression File\n";

				assert(Oodle::IsAtlanCompFile(compbuffer, outputlength));
				result = mz_zip_writer_add_mem(zptr, bin_name.c_str(), compbuffer, outputlength, MZ_DEFAULT_COMPRESSION);

				delete[] compbuffer;
			}
			else {
				result = mz_zip_writer_add_mem(zptr, bin_name.c_str(), serialized.GetBuffer(), serialized.GetFilledSize(), MZ_DEFAULT_COMPRESSION);
			}

			if (!result) {
				atlog << "ERROR: Failed to add " << bin_name << " to zip file\n";
			}

			std::string temp = zippedName;
			zippedName = "noload/";
			zippedName += temp;
		}

		bool result = mz_zip_writer_add_file(zptr, zippedName.c_str(), modfile.string().c_str(), "", 0, MZ_DEFAULT_COMPRESSION);
		if(!result)
			atlog << "ERROR: Failed to add " << zippedName << " to zip file\n";		
	}

	void* buffer = nullptr;
	size_t buffersize = 0;
	bool finalize = mz_zip_writer_finalize_heap_archive(zptr, &buffer, &buffersize);
	if (!finalize) {
		atlog << "ERROR: Failed to finalize zip archive\n";
		return;
	}

	mz_zip_writer_end(zptr);

	std::ofstream zipoutput("AtlanPackage.zip", std::ios_base::binary);
	zipoutput.write((char*)buffer, buffersize);
	zipoutput.close();
	delete[] buffer;
}

// Unused
void AlternateMode(int argc, char* argv[])
{
	atlog << "\n";

	if (argc != 4 || strcmp(argv[1], "--serializemap") != 0) {
		atlog << "Specialized Mode: AtlanModPackager.exe --serializemap <input_path> <output_path>\n";
		return;
	}

	using namespace std::filesystem;

	fspath inputpath = absolute(argv[2]);
	fspath outputpath = absolute(argv[3]);

	if(!exists(inputpath) || is_directory(inputpath)) {
		atlog << "FATAL ERROR: Input file does not exist\n";
		return;
	}

	if (is_directory(outputpath) || !is_directory(outputpath.parent_path())) {
		atlog << "FATAL ERROR: Invalid output location\n";
		return;
	}

	BinaryWriter writer(static_cast<size_t>(file_size(inputpath) * 0.5f));
	atlog << "Serializing Mapentities " << inputpath << "\n";

	int warningcount = Reserializer::Serialize(inputpath.string().c_str(), writer, rt_mapentities);

	atlog << "Total Warnings: " << warningcount << "\n";
	atlog << "Writing output to " << outputpath << "\n";

	std::ofstream outwriter(outputpath, std::ios_base::binary);
	outwriter.write(writer.GetBuffer(), writer.GetFilledSize());
	outwriter.close();
}

int main(int argc, char* argv[])
{
	#define logpath "packager_log.txt"

	#ifndef _DEBUG
	try {
	#endif

		AtlanLogger::init(logpath);
		atlog << "Atlan Mod Packager v1.1 by FlavorfulGecko5\n";
		PackagerMain();
		

	#ifndef _DEBUG
	}
	catch (std::exception e) {
		atlog << "\n\nFATAL ERROR: An unexpected crash has occurred\n"
			<< "This may have left your packaged zip incomplete or corrupted.\n"
			<< "Error Message: " << e.what();
	}
	#endif

	atlog << "\n\nThis window will close in 10 seconds\n";
	atlog << "Output written to " << logpath << "\n";
	AtlanLogger::exit();
	
	#ifndef _DEBUG
	std::this_thread::sleep_for(std::chrono::seconds(10));
	#endif
}