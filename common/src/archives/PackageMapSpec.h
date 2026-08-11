#pragma once
#include <filesystem>
#include <vector>
#include <string>

typedef std::filesystem::path fspath;

struct MapSpec_t {
	
	bool good = false;

	// filepaths stored in REVERSE ORDER (first in the JSON, last in the vector). 
	// This makes adding new files easier, as we don't need to shift the whole string array
	// and we don't need to manually increment the reference indices
	std::vector<std::string>      listfiles; 
	std::vector<std::string>      listmaps;  // Maps strings, stored in the same order they're listed in the JSON
	std::vector<std::vector<int>> listrefs;  // Values are inversed (so they point to the correct files in the reversed files list).

	MapSpec_t(const fspath& p_mapspecpath);

	bool operator==(const MapSpec_t& other) const;

	void Print() const;
	std::string ToJson() const;
	void SaveToFile(const fspath& outpath) const;

	// Modifies the PackageMapSpec
	// list_newfiles: New files to add to the packagemapspec. First in array == first in the JSON
	// list_imports: List of import statements from mod config files Format is "FROM_MAP$TO_MAP$FilterString"
	// FilterString is optional. If missing, everything will be imported from that map
	bool Modify(const std::string* list_newfiles, const size_t num_newfiles, const std::string* list_imports, const size_t num_imports);
};

namespace PackageMapSpec
{
	/* Prints a human-readable version of the PackageMapSpec*/
	void ToString(const fspath gamedir);

	/* Injects an archive into the Common map */
	void InjectCommonArchive(const fspath gamedir, const fspath newarchivepath, bool includeStreamDB);

	/*
	* Returns a list of archives paths (taken verbatim from the packagemapspec)
	* Higher priority archives appear first in the vector
	* 
	* WARNING: Not guaranteed to be prioritized correctly if the packagemapspec is modded
	*/
	std::vector<std::string> GetPrioritizedArchiveList(const fspath gamedir, bool IncludeModArchives);

	std::vector<std::string> GetStreamDBList(const fspath gamedir, bool IncludeModArchives);

	std::vector<fspath> GetArchiveList(const fspath& mapspecpath, bool IncludeModFiles = false);
}