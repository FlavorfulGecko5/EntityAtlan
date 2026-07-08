#include "PackageMapSpec.h"
#include "entityslayer\EntityParser.h"
#include "atlan/AtlanLogger.h"
#include "atlan/AtlanLib.h"
#include <cassert>

#ifndef _DEBUG
#undef assert
#define assert(OP) (OP)
#endif

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
