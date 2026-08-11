#include "PackageMapSpec.h"
#include "entityslayer\EntityParser.h"
#include "atlan/AtlanLogger.h"
#include "atlan/AtlanLib.h"
#include <cassert>

#ifndef _DEBUG
#undef assert
#define assert(OP) (OP)
#endif

MapSpec_t::MapSpec_t(const fspath& p_mapspecpath)
{
	try {
		EntityParser parser(p_mapspecpath.string(), ParsingMode::JSON);
		EntNode& root = *parser.getRoot()->ChildAt(0);

		EntNode& files = root["\"files\""];
		assert(&files != EntNode::SEARCH_404);
		listfiles.reserve(files.getChildCount() + 32);

		const int NUM_FILES = files.getChildCount();

		for (int i = NUM_FILES - 1; i > -1; i--) {
			listfiles.emplace_back(files[i][0].getValueUQ());
		}

		for (const EntNode& m : root["\"maps\""]) {
			listmaps.emplace_back(m[0].getValueUQ());
		}

		listrefs.resize(listmaps.size());
		for (const EntNode& ref : root["\"mapFileRefs\""]) {
			int fileindex = -1, mapindex = -1;

			ref[0].ValueInt(fileindex, -1, 999999);
			ref[1].ValueInt(mapindex, -1, 999999);

			assert(fileindex != -1 && mapindex != -1);

			// Reverse the index to sync them with the reversed file list
			listrefs[mapindex].push_back(NUM_FILES - 1 - fileindex);
		}


	}
	catch (...) { return; }

	good = true;
}

bool MapSpec_t::Modify(const std::string* list_newfiles, const size_t num_newfiles, const std::string* list_imports, const size_t num_imports)
{
	// Add New Files
	for (int64_t i = num_newfiles - 1; i > -1; i--) {
		// FOR NOW: We assume that new files are being added to the common map.
		listrefs[0].push_back((int)listfiles.size());
		
		listfiles.push_back(list_newfiles[i]);
	}

	// Do Imports
	if(num_imports == 0)
		return true;

	// Our goal: Ensure broad import statements
	// don't cause issues by only permitting the same file to be referenced by a map once
	// Using a separate array also ensures we're not modifying a reflist that we then use
	// in a future import statement. We want to keep our imports based purely on the vanilla reflists
	std::vector<std::vector<int>> newrefs;
	newrefs.resize(listrefs.size());

	atlog("PackageMapSpec: Processing %zu import statements", num_imports);

	for (size_t IMPORT_INDEX = 0; IMPORT_INDEX < num_imports; IMPORT_INDEX++) {
		const std::string& import = list_imports[IMPORT_INDEX];

		const size_t from_delimiter = import.find('$');
		if (from_delimiter == -1) {
			atlog("ERROR: %s Incorrectly formatted import statement", import.c_str());
			return false;
		}

		// Need to add parentheses since listfiles keeps the quotes
		const std::string from_map = import.substr(0, from_delimiter);
		std::string to_map, filter;

		size_t to_delimiter = import.find('$', from_delimiter + 1);
		if (to_delimiter == -1) {
			to_map = import.substr(from_delimiter + 1);
		}
		else {
			to_map = import.substr(from_delimiter + 1, to_delimiter - from_delimiter - 1);
			filter = import.substr(to_delimiter + 1);
		}

		//printf("[%s] [%s] [%s]\n", from_map.data(), to_map.data(), filter.data());

		int64_t from_index = -1, to_index = -1;
		for (int64_t i = 0; i < (int64_t)listmaps.size(); i++) {
			if (from_index == -1 && listmaps[i] == from_map)
				from_index = i;
			if (to_index == -1   && listmaps[i] == to_map)
				to_index = i;
		}

		if (from_index == -1 || to_index == -1) {
			atlog("ERROR: %s Could not find map written in import statement", import.c_str());
			return false;
		}

		// This approach ensures each file is only referenced once in a map
		std::set<int> refset;

		// Add vanilla references and references made from previous import statements
		if (newrefs[to_index].size()) {
			atlog("Warning: Map %s affected by multiple import statements", listmaps[to_index].c_str());
			for (int i : newrefs[to_index])  
				refset.insert(i);           
		}
		else {
			for (int i : listrefs[to_index]) 
				refset.insert(i);
		}

		// Import the new refs
		if (filter.length()) {
			for (int i : listrefs[from_index]) {
				if (listfiles[i].rfind(filter) != -1) {
					refset.insert(i);
				}
			}
		}
		else {
			for (int i : listrefs[from_index])
				refset.insert(i);
		}


		newrefs[to_index].clear();
		newrefs[to_index].reserve(refset.size());
		for(int i : refset)
			newrefs[to_index].push_back(i);
	}

	// Final Step: Replace original reflists with modified reflists if used
	for (size_t i = 0; i < listrefs.size(); i++) {
		if (newrefs[i].size()) {
			listrefs[i] = newrefs[i];
		}
	}

	return true;
}

bool MapSpec_t::operator==(const MapSpec_t& other) const {
	return good == other.good 
		&& listfiles == other.listfiles
		&& listmaps == other.listmaps
		&& listrefs == other.listrefs;
}

void MapSpec_t::Print() const {
	for (size_t i = 0; i < listmaps.size(); i++) {
		printf("%s\n----\n", listmaps[i].c_str());
		for (const int fileindex : listrefs[i]) {
			printf("-%s\n", listfiles[fileindex].c_str());
		}
		printf("\n");
	}
}

#include <fstream>

// It ain't pretty...but it works as intended
std::string MapSpec_t::ToJson() const {
	std::string s;
	s.reserve(150'000);

	s.append(R"({ "files": [ )");

	// Unreverse the files list
	for (int64_t i = listfiles.size() - 1; i > -1; i--) {
		s.append(R"(
{ "name": ")");
		s.append(listfiles[i]);
		s.append("\"},");
	}
	s.pop_back(); // Pop trailing comma (stupid json)

	// mapFileRefs list
	s.append(R"(],
"mapFileRefs": [ )");
	const int NUM_FILES = (int)listfiles.size();
	int vecindex = 0;
	for (const std::vector<int>& VECTOR : listrefs) {
		for (const int i : VECTOR) {
			s.append(R"(
{ "file": )");
			s.append(std::to_string(NUM_FILES - 1 - i)); // Unreverse the file indices
			s.append(R"(, "map": )");
			s.append(std::to_string(vecindex));
			s.append(R"(},)");
		}
		vecindex++;
	}
	s.pop_back();

	// maps list
	s.append(R"(],
"maps": [ )");

	for (const std::string& map : listmaps) {
		s.append(R"(
{"name": ")");
		s.append(map);
		s.append("\"},");
	}
	s.pop_back();
	s.append("]}");

	return s;
}

void MapSpec_t::SaveToFile(const fspath& outputpath) const {
	std::string json = ToJson();

	std::ofstream outwriter(outputpath, std::ios_base::binary);
	outwriter.write(json.data(), json.length());
	outwriter.close();
}

void PackageMapSpec::ToString(const fspath gamedir) {
	EntityParser entparser((gamedir / "base/packagemapspec.json").string(), ParsingMode::JSON);
	EntNode& jsonroot = *entparser.getRoot()->ChildAt(0);
	
	std::vector<std::string_view> filenames;
	for (const EntNode& file : jsonroot["\"files\""]) {
		filenames.push_back(file[0].getValueUQ());
	}

	std::vector<std::string_view> mapnames;
	for (const EntNode& map : jsonroot["\"maps\""]) {
		mapnames.push_back(map[0].getValueUQ());
	}

	std::vector<std::vector<int>> filemapping;
	filemapping.resize(mapnames.size());

	for (const EntNode& ref : jsonroot["\"mapFileRefs\""]) {
		int fileindex = -1, mapindex = -1;
		ref[0].ValueInt(fileindex, -9999, 9999);
		ref[1].ValueInt(mapindex, -9999, 9999);

		check_always(fileindex != -1 && mapindex != -1);
		filemapping[mapindex].push_back(fileindex);
	}


	for(size_t i = 0; i < filemapping.size(); i++) {
		atlog("\n%.*s", (int)mapnames[i].length(), mapnames[i].data());
		for(int fileindex : filemapping[i]) {
			atlog("-%.*s", (int)filenames[fileindex].length(), filenames[fileindex].data());
		}
	}

	atlog("Files: %zu Maps: %zu", filenames.size(), mapnames.size());
}

// TODO: Will need to revisit this upon adding more advanced features
// and allow for packagemapspec manipulation
void PackageMapSpec::InjectCommonArchive(const fspath gamedir, const fspath newarchivepath, bool includeStreamDB)
{
	const fspath pmspath = gamedir / "base/packagemapspec.json";
	EntityParser entparser(pmspath.string(), ParsingMode::JSON);
	EntNode& jsonroot = *entparser.getRoot()->ChildAt(0);

	// Get the relative path appropriate for the packagemapspec
	size_t substringIndex = pmspath.parent_path().string().size() + 1;
	std::string archrelativepath = newarchivepath.string().substr(substringIndex);
	for (char& c : archrelativepath) {
		if (c == '\\') c = '/';
	}
	//std::cout << archrelativepath;

	// Ensure first map is the common map
	assert(jsonroot["\"maps\""].ChildAt(0)->ChildAt(0)->getValueUQ() == "common");

	char buffer[512];

	// Insert archive into beginning of file list
	// We must insert it at the beginning since this list dictates patch priority
	{
		EntNode& filelist = jsonroot["\"files\""];
		if (includeStreamDB) {
			snprintf(buffer, 512, R"({ "name": "%s" })", "modarchives/common_mod.streamdb");
			entparser.EditTree(buffer, &filelist, 0, 0, 0, 0);
		}
		snprintf(buffer, 512, R"({ "name": "%s" })", archrelativepath.c_str());
		entparser.EditTree(buffer, &filelist, 0, 0, 0, 0);
	}

	// Now we must increment every file number in the map list to account for the insertion
	{
		EntNode& mapfilerefs = jsonroot["\"mapFileRefs\""];

		for (int i = 0; i < mapfilerefs.getChildCount(); i++) {
			EntNode& mapping = *mapfilerefs.ChildAt(i);
			EntNode& file = mapping["\"file\""];
			int index = -1;

			assert(&file != EntNode::SEARCH_404);
			assert(file.ValueInt(index, -999999, 999999));
			index += includeStreamDB ? 2 : 1;

			std::string newText = "\"file\"";
			newText.append(std::to_string(index));
			entparser.EditText(newText, &file, 6, 0); // Note: This is a very hacky function. 6 is length of "file" (including quotes)
		}

		// Finally, we insert the new archive into common's mapFileRef
		if (includeStreamDB) {
			entparser.EditTree(R"({ "file": 1, "map": 0 })", &mapfilerefs, 0, 0, 0, 0);
		}
		entparser.EditTree(R"({ "file": 0, "map": 0 })", &mapfilerefs, 0, 0, 0, 0);
	}

	entparser.WriteToFile(pmspath.string(), 0);
}

std::vector<std::string> PackageMapSpec_MakeFileList(const fspath gamedir, bool IncludeModArchives, std::string extString)
{
	fspath pathMapSpec = gamedir / "base/packagemapspec.json";
	if(!std::filesystem::exists(pathMapSpec))
		return {};

	std::vector<std::string> packages;
	try {
		EntityParser parser(pathMapSpec.string(), ParsingMode::JSON);

		EntNode* root = parser.getRoot()->ChildAt(0);
		EntNode& files = (*root)["\"files\""];
		packages.reserve(files.getChildCount());
		// Get all resource archive names and their priorities
		for (int i = 0; i < files.getChildCount(); i++) {
			EntNode& name = (*files.ChildAt(i))["\"name\""];

			assert(&name != EntNode::SEARCH_404);

			std::string_view nameString = name.getValueUQ();
			if (nameString.find("modarchives") != std::string::npos) {
				if(!IncludeModArchives)
					continue;
			}

			// All of this...because the C++ 17 STL doesn't have EndsWith
			size_t extIndex = nameString.rfind(extString);
			if (extIndex != std::string_view::npos && extIndex + extString.length() == nameString.length())
			{
				//printf("%.*s\n", (int)nameString.length(), nameString.data());
				packages.push_back(std::string(nameString));
			}
		}
	}
	catch (std::exception e) {
		return {};
	}

	return packages;
}

// TODO: This is a refactor-in-progress. Eventual goal is to fully
// replace the above function with this one and other functions that call it
std::vector<fspath> PackageMapSpec_MakeFileList2(const fspath& mapspecpath, bool IncludeModFiles, std::string extString) {
	if (!std::filesystem::exists(mapspecpath)) {
		atlog("ERROR: PackageMapSpec_MakeFileList: PackageMapSpec not found");
		return {};
	}

	const fspath mapspec_dir = mapspecpath.parent_path();
	std::vector<fspath> packages;
	try {
		EntityParser parser(mapspecpath.string(), ParsingMode::JSON);

		EntNode* root = parser.getRoot()->ChildAt(0);
		EntNode& files = (*root)["\"files\""];
		packages.reserve(files.getChildCount() / 5);

		for (const EntNode& entry : files) {
			std::string_view nameString = entry[0].getValueUQ();

			if (nameString.find("modarchives") != -1) {
				if(!IncludeModFiles)
					continue;
			}

			// All of this...because the C++ 17 STL doesn't have EndsWith
			size_t extIndex = nameString.rfind(extString);
			if (extIndex == -1 || extIndex + extString.length() != nameString.length()) {
				continue;
			}

			packages.emplace_back(mapspec_dir / nameString);

			// If the DLC archives only get downloaded if you own the DLC
			// then we'll need to ensure only archives that exist are returned
			if(!std::filesystem::exists(packages.back()))
				packages.pop_back();
		}
	}
	catch (std::exception e) {
		atlog("ERROR: PackageMapSpec_MakeFileList: Failed to parse packagemapspec");
		return {};
	}

	return packages;
}

std::vector<std::string> PackageMapSpec::GetPrioritizedArchiveList(const fspath gamedir, bool IncludeModArchives)
{
	return PackageMapSpec_MakeFileList(gamedir, IncludeModArchives, ".resources");
}

std::vector<std::string> PackageMapSpec::GetStreamDBList(const fspath gamedir, bool IncludeModArchives)
{
	return PackageMapSpec_MakeFileList(gamedir, IncludeModArchives, ".streamdb");
}

std::vector<fspath> PackageMapSpec::GetArchiveList(const fspath& mapspecpath, bool IncludeModFiles)
{
	return PackageMapSpec_MakeFileList2(mapspecpath, IncludeModFiles, ".resources");
}
