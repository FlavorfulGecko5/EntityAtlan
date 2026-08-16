#include <filesystem>
#include <unordered_map>
#include <set>
#include <string>
#include <fstream>
#include "archives/MapResources.h"
#include "archives/PackageMapSpec.h"
#include "archives/ResourceStructs.h"
#include "atlan/AtlanLogger.h"

typedef std::filesystem::path fspath;

void Script_DumpResourceTypes(const fspath& gamedir, const fspath& outputdir)
{
	atlog("Running Script: Dumping Resource Types and MapResources");
	std::set<std::string> restypes;
	std::set<std::string> maprestypes;

	std::unordered_map<std::string, std::string> Anomalies;
	Anomalies.reserve(50'000);

	const fspath dirmapresources = outputdir / "decoded_mapresources";
	if(!std::filesystem::exists(dirmapresources))
		std::filesystem::create_directory(dirmapresources);

	idcl::ArchiveIterator iter(gamedir / "base/packagemapspec.json", true);
	iter.allowUnmasked = true;
	iter.allowDisabled = true;

	for (const ResourceEntry& e : iter) {

		// Dependency Type Strings are correctly capitalized
		// There are a few extra types listed in these that aren't in the .mapresources
		if (iter.newarchiveloaded) {
			iter.newarchiveloaded = false;

			for (ResourceDependency* d = iter.archive.dependencies; d < iter.archive.dependencies + iter.archive.header.numDependencies; d++) {
				const char* deptype, *depname;

				Get_DependencyStrings(iter.archive, *d, deptype, depname);
				maprestypes.insert(deptype);
			}
		}

		restypes.insert(iter.typestring);

		if (strcmp(iter.typestring, "file"))
			continue;

		if (std::string(iter.namestring).find(".mapresources") == -1)
			continue;

		ResourceEntryBuffers_t temp;
		ResourceEntryData_t entrydata = Get_EntryData(e, iter.archive.filehandle, temp);
		MapResource resfile;
		resfile.Parse(entrydata.buffer, entrydata.length);

		for (size_t k = 0; k < resfile.num_types; k++) {
			maprestypes.insert(std::string(resfile.list_types[k].data, resfile.list_types[k].length));
		}

		if(!iter.isEnabled)
			continue;

		// Gather anomalies
		for (u32 i = 0; i < resfile.num_entries; i++) {
			MapResource::entry_t& mapentry = resfile.list_entries[i];

			if(!resfile.isAnomalous(mapentry))
				continue;

			std::string& anomlog = Anomalies[resfile.getEntryName(mapentry)];

			anomlog.append("\t\"");
			anomlog.append(resfile.getEntryBytes(mapentry));
			anomlog.append("\" \"");
			anomlog.append(fspath(iter.namestring).stem().string());
			anomlog.append("\"\n");
		}

		// Output the MapResources file
		const std::string MapResourceText = resfile.ToString();

		// In Eternal...the base campaign Hub and the DLC hub share the same resource name
		// This forces us to do this if we don't want one file overriding the other
		std::string namestring = iter.namestring;
		if(namestring.find("dlc/hub/hub.mapresources") != -1)
			namestring = "hub_dlc.mapresources";

		fspath MapResourcePath = dirmapresources / fspath(namestring).filename();
		MapResourcePath.replace_extension(".txt");

		std::ofstream outwriter(MapResourcePath, std::ios_base::binary);
		outwriter << MapResourceText;
		outwriter.close();
	}

	// Output resource types
	std::ofstream outwriter(outputdir / "ResourceTypes.txt", std::ios_base::binary);
	outwriter << "// List of all resource types that appear in the resource archives\n";
	for (const std::string& s : restypes) {
		outwriter << s << "\n";
	}
	outwriter.close();

	// Output capitalized resource types
	outwriter.open(outputdir / "CapitalizedResourceTypes.txt", std::ios_base::binary);
	outwriter << "// All correctly capitalized resource types that appear in .mapresources and serialized files\n";
	for (const std::string& s : maprestypes) {
		outwriter << s << "\n";
	}
	outwriter.close();

	// Output anomaly log
	// Sort the keys alphabetically so the final file is easier to comprehend
	outwriter.open(outputdir / "MapResourceAnomalies.txt");
	std::set<std::string> SortedAnomalyKeys;
	for (const auto& pair : Anomalies) {
		SortedAnomalyKeys.insert(pair.first);
	}
	for (const std::string& s : SortedAnomalyKeys) {
		outwriter << '"' << s << "\" {\n";
		outwriter << Anomalies[s] << "}\n";
	}
	outwriter.close();
}

void RunExtraScripts(fspath gamedir, fspath outputdir)
{
	atlog("Running Extra Scripts");

	outputdir /= "extra_scripts";
	if(!std::filesystem::exists(outputdir))
		std::filesystem::create_directory(outputdir);

	Script_DumpResourceTypes(gamedir, outputdir);
}