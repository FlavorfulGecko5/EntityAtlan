#include <filesystem>
#include <fstream>
#include <chrono>
#include <cassert>
#include <set>
#include "archives/ResourceStructs.h"
#include "archives/PackageMapSpec.h"
#include "staticsparser.h"
#include "io/BinaryReader.h"
#include "deserialcore.h"
#include "hash/HashLib.h"
#include "entityslayer/EntityParser.h"
#include "atlan/AtlanLogger.h"
#include "DeserialMain.h"

#ifndef _DEBUG
#undef assert
#define assert(op) (op)
#endif

#define TIMESTART(ID) auto EntityProfiling_ID  = std::chrono::high_resolution_clock::now();

#define TIMESTOP(ID, msg) { \
	auto timeStop = std::chrono::high_resolution_clock::now(); \
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(timeStop - EntityProfiling_ID); \
	printf("%s: %zu", msg, duration.count());\
}

void StaticsTest() {
	BinaryOpener filedata("input/m2_hebeth.mapentities");
	BinaryReader r = filedata.ToReader();

	StaticsParser::Parse(r);
}

void DeserialInit(const fspath gamedir, const fspath filedir) {
	atlog << "Building Decl Farmhash Map\n";

	const fspath basepath = gamedir / "base";
	const fspath entitydir = filedir / "entityDef";
	const std::set<std::string> ValidTypes = { "mapentities", "entityDef", "logicFX", "logicLibrary", "logicClass", "logicUIWidget", "logicEntity" };
	const std::vector<std::string> archiveList = PackageMapSpec::GetPrioritizedArchiveList(gamedir, false);
	
	deserial::declHashMap.reserve(15000);
	deserial::entityclassmap.reserve(5000);

	// For entitydef data
	size_t rawsize = 24000;
	size_t decompsize = 24000;
	char* raw = new char[rawsize];
	char* decomp = new char [decompsize];


	for (const std::string& archivename : archiveList) {
		ResourceArchive r;
		Read_ResourceArchive(r, basepath / archivename, RF_SkipData);
		std::ifstream archivestream(basepath / archivename, std::ios_base::binary);

		for (uint32_t i = 0; i < r.header.numResources; i++) {
			const ResourceEntry& e = r.entries[i];

			const char* typestring, *namestring;
			Get_EntryStrings(r, e, typestring, namestring);

			if(ValidTypes.count(typestring) == 0)
				continue;
			
			/* The dependency list gives us a complete hashmap for resource paths */
			for (uint32_t k = 0; k < e.numDependencies; k++) {
				uint32_t depindex = r.dependencyIndex[e.depIndices + k];
				const ResourceDependency& d = r.dependencies[depindex];

				const char* deptypestring = r.stringChunk.dataBlock + r.stringChunk.offsets[d.type];
				const char* depnamestring = r.stringChunk.dataBlock + r.stringChunk.offsets[d.name];

				uint64_t depfarmhash = HashLib::DeclHash(deptypestring, depnamestring);
				std::string hashString = deptypestring;
				hashString.push_back('/');
				hashString.append(depnamestring);

				const auto& iter = deserial::declHashMap.find(depfarmhash);
				assert(iter == deserial::declHashMap.end() || iter->second == hashString);
				deserial::declHashMap.emplace(depfarmhash, hashString);
			}

			/* If this is an entitydef, put it's data into the entity class map */
			if (strcmp(typestring, "entityDef") == 0) {
				uint64_t farmhash = HashLib::DeclHash(typestring, namestring);

				// Don't allow older file versions to have priority in the class map
				{
					const auto& iter = deserial::entityclassmap.find(farmhash);
					if(iter != deserial::entityclassmap.end())
						continue;
				}

				entityclass_t classdef;

				classdef.filepath = (entitydir / namestring).replace_extension(".bin").string();
				assert(std::filesystem::exists(classdef.filepath));

				const ResourceEntryData_t entrydata = Get_EntryData(e, archivestream, raw, rawsize, decomp, decompsize);
				assert(entrydata.returncode == EntryDataCode::OK);

				/*
				* Do a hacky read-through of the entitydef
				* to extract the inheritance hash and class hash (if it exists)
				*/
				uint64_t temphash;
				uint32_t length;

				BinaryReader reader(entrydata.buffer, entrydata.length);
				assert(reader.GoRight(5));              // Skip null byte and file length
				assert(reader.ReadLE(classdef.parent)); // Read the inherit hash
				assert(reader.GoRight(6)); // ExpandInheritance(?), 5 padding bytes
				assert(reader.ReadLE(length)); // Length of editorVars block
				assert(reader.GoRight(length)); // Skip editorVars block
				assert(reader.GoRight(5));      // Skip padding
				assert(reader.ReadLE(length)); // Read system variables block length


				if (length > 0) {
					assert(length > 14); // Ensure there's at least one property
					assert(reader.GoRight(15)); // Edit block hash + sub-length of block + 0 byte code
					assert(reader.ReadLE(temphash));

					// Farmhash of "entityClass" - *should* always be first if it exists
					// Todo: might be risky to assume it will always be first
					if (temphash == 17091029760865742588UL) {
						assert(reader.GoRight(5)); // Leaf node byte + Length
						assert(reader.ReadLE(classdef.typehash));
					}
				}
				deserial::entityclassmap.emplace(farmhash, classdef);
				//std::cout << declpath << " " << classdef.typehash << "\n";
				//if (classdef.typehash == 0)
				//	std::cout << "MISSING\n";
			}
		}
	}

	delete[] raw;
	delete[] decomp;

	/*
	* STEP 2: Populate inherited typehash information
	*/
	for (auto& current : deserial::entityclassmap) {
		if(current.second.typehash != 0)
			continue;

		auto parentiter = deserial::entityclassmap.find(current.second.parent);
		while (true) {
			if(parentiter == deserial::entityclassmap.end())
				assert(0); // Todo: Refactor so there's no infinite loop in release builds

			if (parentiter->second.typehash == 0) {
				parentiter = deserial::entityclassmap.find(parentiter->second.parent);
			}
			else {
				current.second.typehash = parentiter->second.typehash;
				break;
			}
		}
	}

	for (auto& current : deserial::entityclassmap) {
		assert(current.second.typehash != 0);
	}

	atlog << "Decl Hash Map Size: " << deserial::declHashMap.size() << "\n";
}

void AddIndentation(const std::string& path) {
	try {
		EntityParser parser(path, ParsingMode::PERMISSIVE);

		parser.WriteToFile(path, false);
	}
	catch (...) {
		atlog << "ERROR: EntityParser failed to indent " << path << "\n";
	}
}

void DeserializeEntitydefs(bool remove_binaries, bool add_indent) {
	deserial::deserialmode = DeserialMode::entitydef;
	deserial::warning_count = 0;

	std::string writeto;
	writeto.reserve(500000);

	int totaldeserialized = 0;
	while (totaldeserialized < deserial::entityclassmap.size()) {

		for (auto& entitydef : deserial::entityclassmap) {

			// Skip this entity if it's already been deserialized
			if(entitydef.second.deserialized)
				continue;

			// Do not deserialize if the parent isn't deserialized
			const auto parententity = deserial::entityclassmap.find(entitydef.second.parent);
			if (parententity != deserial::entityclassmap.end()) {
				if(!parententity->second.deserialized)
					continue;
			}

			writeto.clear();
			int previousWarningCount = deserial::warning_count;

			BinaryOpener opener = BinaryOpener(entitydef.second.filepath);
			if (!opener.Okay()) {
				atlog << "ERROR: Failed to read entitydef " << entitydef.second.filepath
					<< "\nAborting entitydef extraction\n";
				return;
			}
			BinaryReader reader = opener.ToReader();
			deserial::ds_start_entitydef(reader, writeto, entitydef.first);

			if (previousWarningCount != deserial::warning_count)
				atlog << entitydef.second.filepath << "\n";
			totaldeserialized++;
			entitydef.second.deserialized = true;

			// Write the file
			fspath outpath = entitydef.second.filepath;
			outpath.replace_extension(".decl");

			std::ofstream output(outpath, std::ios_base::binary);
			output << writeto;
			output.close();

			if (remove_binaries) {
				std::filesystem::remove(entitydef.second.filepath);
			}
			
			if (add_indent) {
				AddIndentation(outpath.string());
			}
		}
	}
	atlog << "EntityDef Warning Count: " << deserial::warning_count << " Files: " << totaldeserialized << "\n";
}

void DeserializeMapEntities(const fspath filedir, bool remove_binaries, bool add_indent) {
	deserial::deserialmode = DeserialMode::mapentities;
	deserial::warning_count = 0;

	std::vector<fspath> binpaths;

	using namespace std::filesystem;

	if (!is_directory(filedir / "mapentities")) {
		atlog << "ERROR: mapentities folder does not exist\n";
		return;
	}

	for (const directory_entry& entry : recursive_directory_iterator(filedir / "mapentities")) {
		if(is_directory(entry))
			continue;

		if (entry.path().extension() == ".bin") {
			binpaths.push_back(entry.path());
		}
	}

	std::string outtext;
	outtext.reserve(30000000);

	for (const fspath& file : binpaths) {
		atlog << file.filename() << "\n";
		outtext.clear();

		BinaryOpener open(file.string());
		assert(open.Okay());
		BinaryReader reader = open.ToReader();

		deserial::ds_start_mapentities(reader, outtext);

		fspath outpath = file;
		outpath.replace_extension(".mapentities");
		std::ofstream outfile(outpath, std::ios_base::binary);
		outfile << outtext;
		outfile.close();

		if (remove_binaries) {
			std::filesystem::remove(file);
		}

		if (add_indent) {
			AddIndentation(outpath.string());
		}
	}

	atlog << "Map Entities Warning Count: " << deserial::warning_count << " Files: " << binpaths.size() << "\n";
}

void DeserializeLogicdecls(const fspath filedir, bool remove_binaries, bool add_indent) {
	deserial::deserialmode = DeserialMode::logic;

	struct logicfolder_t {
		const fspath foldername;
		const LogicType type;
	};

	const logicfolder_t logicfolders[LT_MAXIMUM] = { 
		{"logicClass", LT_LogicClass},
		{"logicEntity", LT_LogicEntity},
		{"logicFX", LT_LogicFX},
		{"logicLibrary", LT_LogicLibrary},
		{"logicUIWidget", LT_LogicUIWidget}
	};

	for (int lt = LT_LogicClass; lt < LT_MAXIMUM; lt++) {
		using namespace std::filesystem;
		const fspath dir = filedir / logicfolders[lt].foldername;

		if (!is_directory(dir)) {
			atlog << "ERROR: " << logicfolders[lt].foldername << " folder does not exist!\n";
			continue;
		}
		atlog << "Deserializing " << logicfolders[lt].foldername << " Decls\n";
		deserial::warning_count = 0;

		/* Get list of files to deserialize */
		std::vector<fspath> binpaths;
		for (const directory_entry& entry : recursive_directory_iterator(dir)) {
			if(is_directory(entry))
				continue;

			if (entry.path().extension() == ".bin") {
				binpaths.push_back(entry.path());
			}
		}

		std::string outputText;
		outputText.reserve(1000000);
		for (const fspath& filepath : binpaths) {
			outputText.clear();

			int warningCount = deserial::warning_count;

			BinaryOpener open(filepath.string());
			assert(open.Okay());
			BinaryReader reader = open.ToReader();

			deserial::ds_start_logicdecl(reader, outputText, logicfolders[lt].type);

			int newWarningCount = deserial::warning_count;
			if(newWarningCount != warningCount)
				atlog << filepath << "\n";

			fspath outpath = filepath;
			outpath.replace_extension(".decl");

			std::ofstream outwriter(outpath, std::ios_base::binary);
			outwriter << outputText;
			outwriter.close();

			if (remove_binaries) {
				std::filesystem::remove(filepath);
			}

			if (add_indent) {
				AddIndentation(outpath.string());
			}
		}

		atlog << "Total Warning Count: " << deserial::warning_count << " Files: " << binpaths.size() << "\n";
	}
}

void Deserializer::DeserialMain(const fspath& gamedir, const fspath& filedir, deserialconfig_t config)
{
	DeserialInit(gamedir, filedir);
	deserial::include_originals = config.include_original;

	if (config.deserial_entitydefs) {
		atlog << "Deserializing EntityDefs\n";
		DeserializeEntitydefs(config.remove_binaries, config.indent);
		atlog << "Finished EntityDefs\n";
	}
	else {
		atlog << "Skipping EntityDefs\n";
	}


	if (config.deserial_logicdecls) {
		atlog << "Deserializing Logic Decls\n";
		DeserializeLogicdecls(filedir, config.remove_binaries, config.indent);
		atlog << "Finished Logic Decls\n";
	}
	else {
		atlog << "Skipping Logic Decls\n";
	}
	

	if (config.deserial_mapentities) {
		atlog << "Deserializing Map Entities\n";
		DeserializeMapEntities(filedir, config.remove_binaries, config.indent);
		atlog << "Finished Map Entities\n";
	}
	else {
		atlog << "Skipping Map Entities\n";
	}

}

//int main() {
//
//	deserialconfig_t config;
//	config.deserial_entitydefs  = 0;
//	config.deserial_logicdecls  = 0;
//	config.deserial_mapentities = 0;
//	config.include_original     = 0;
//	config.remove_binaries      = 0;
//	config.indent               = 0;
//
//	Deserializer::DeserialMain("D:/Steam/steamapps/common/DOOMTheDarkAges", "D:/DA/atlan", config);
//
//	return 0;
//}