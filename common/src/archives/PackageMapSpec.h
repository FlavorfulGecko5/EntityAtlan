#pragma once
#include <filesystem>
#include <vector>
#include <string>

typedef std::filesystem::path fspath;

namespace PackageMapSpec
{
	/* Prints a human-readable version of the PackageMapSpec*/
	void ToString(const fspath gamedir);

	/* Injects an archive into the Common map */
	void InjectCommonArchive(const fspath gamedir, const fspath newarchivepath);

	/*
	* Returns a list of archives paths (taken verbatim from the packagemapspec)
	* Higher priority archives appear first in the vector
	* 
	* WARNING: Not guaranteed to be prioritized correctly if the packagemapspec is modded
	*/
	std::vector<std::string> GetPrioritizedArchiveList(const fspath gamedir, bool IncludeModArchives);
}