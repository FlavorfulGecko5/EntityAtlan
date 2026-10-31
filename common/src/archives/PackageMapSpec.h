#pragma once
#include <filesystem>
#include <vector>
#include <string>

typedef std::filesystem::path fspath;

struct MapSpec_t {

	struct AddOn {
		std::string newfile;
		std::string mapname;
	};
	
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
	bool Modify(const AddOn* list_newfiles, const size_t num_newfiles, const std::string* list_imports, const size_t num_imports);

	// Returns -1 if map cannot be found
	size_t MapIndex(const std::string& mapname) const;

	// Returns -1 if file cannot be found
	size_t FileIndex(const std::string& filename, size_t startIndex = 0) const;

	// Returns true if this lightmap does not match up with any level in this packagemapspec
	// Returns false if this lightmap does match with a level, or if this filepath does not
	// have the expected format
	// lightmap_path is presumed to have the format "virtualtextures/maps/.../mapname.lightdb"
	bool IsUnusedLightmap(fspath lightmap_path) const;
};

namespace PackageMapSpec
{
	/* Prints a human-readable version of the PackageMapSpec*/
	void ToString(const fspath gamedir);

	/*
	* Returns a list of archives paths (taken verbatim from the packagemapspec)
	* Higher priority archives appear first in the vector
	* 
	* WARNING: Not guaranteed to be prioritized correctly if the packagemapspec is modded
	*/

	std::vector<fspath> GetStreamDBList(const fspath& mapspecpath, bool IncludeModArchives);

	std::vector<fspath> GetArchiveList(const fspath& mapspecpath, bool IncludeModFiles = false);
}