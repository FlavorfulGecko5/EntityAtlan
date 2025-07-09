#include "PackageMapSpec.h"
#include "entityslayer\EntityParser.h"
#include <cassert>
#include <iostream>

void PackageMapSpec::ToString(const fspath gamedir) {
	EntityParser entparser((gamedir / "base/packagemapspec.json").string(), ParsingMode::JSON);
	EntNode& jsonroot = *entparser.getRoot()->ChildAt(0);
	
	std::vector<std::string_view> filenames;
	EntNode& filelist = jsonroot["\"files\""];
	for(int i = 0; i < filelist.getChildCount(); i++) {
		filenames.push_back(filelist.ChildAt(i)->ChildAt(0)->getValueUQ());
		//std::cout << filenames.back() << "\n";
	}

	std::vector<std::string_view> mapnames;
	EntNode& maplist = jsonroot["\"maps\""];
	for(int i = 0; i < maplist.getChildCount(); i++) {
		mapnames.push_back(maplist.ChildAt(i)->ChildAt(0)->getValueUQ());
	}

	std::vector<std::vector<int>> filemapping;
	filemapping.resize(mapnames.size());

	EntNode& mapfilerefs = jsonroot["\"mapFileRefs\""];
	for(int i = 0; i < mapfilerefs.getChildCount(); i++) {
		int fileindex = -1, mapindex = -1;
		mapfilerefs.ChildAt(i)->ChildAt(0)->ValueInt(fileindex, -9999, 9999);
		mapfilerefs.ChildAt(i)->ChildAt(1)->ValueInt(mapindex, -9999, 9999);

		assert(fileindex != -1);
		assert(mapindex != -1);

		filemapping[mapindex].push_back(fileindex);
	}


	for(size_t i = 0; i < filemapping.size(); i++) {
		std::cout << "\n" << mapnames[i] << "\n";

		for(int fileindex : filemapping[i]) {
			std::cout << "-" << filenames[fileindex] << "\n";
		}

	}
}

// TODO: Will need to revisit this upon adding more advanced features
// and allow for packagemapspec manipulation
void PackageMapSpec::InjectCommonArchive(const fspath gamedir, const fspath newarchivepath)
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

	// Get relevant nodes
	EntNode& filelist = jsonroot["\"files\""];
	EntNode& mapfilerefs = jsonroot["\"mapFileRefs\""];

	// Insert the file name into the packagemapspec
	char buffer[512];
	snprintf(buffer, 512, R"({ "name": "%s" })", archrelativepath.c_str());
	entparser.EditTree(buffer, &filelist, filelist.getChildCount(), 0, 0, 0);

	snprintf(buffer, 512, R"({ "file": %s, "map": 0 })", std::to_string(filelist.getChildCount() - 1).c_str());
	entparser.EditTree(buffer, &mapfilerefs, 0, 0, 0, 0);

	entparser.WriteToFile(pmspath.string(), 0);
}

std::vector<std::string> PackageMapSpec::GetPrioritizedArchiveList(const fspath gamedir)
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

			// All of this...because the C++ 17 STL doesn't have EndsWith
			std::string_view extString = ".resources";
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
