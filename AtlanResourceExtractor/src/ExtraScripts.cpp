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
}

void Script_AggregateEntryVars(const fspath& gamedir, const fspath& outputdir) {
	atlog("Running Script: Aggregate Resource Entry Variables");

	struct aggregate_t {
		std::set<u32> versions;
		std::set<u32> offversions;
		std::set<u32> flags;
		std::set<u32> offflags;
		std::set<u16> variations;
		std::set<u16> offvariations;
		bool hasDefaultHash = false;
	};

	std::unordered_map<std::string, aggregate_t> Map;
	Map.reserve(35);

	idcl::ArchiveIterator iter(gamedir / "base/packagemapspec.json", true);
	iter.allowUnmasked = false;
	iter.allowDisabled = true;

	for (const ResourceEntry& e : iter) {
		aggregate_t& agg = Map[iter.typestring];

		if (iter.isEnabled) {
			agg.versions.insert(e.version);
			agg.flags.insert(e.flags);
			agg.variations.insert(e.variation);
		}
		else {
			agg.offversions.insert(e.version);
			agg.offflags.insert(e.flags);
			agg.offvariations.insert(e.variation);
		}

		if(e.dataCheckSum != e.defaultHash)
			agg.hasDefaultHash = true;
	}
	
	std::set<std::string> SortedTypes;
	for(const auto& pair : Map)
		SortedTypes.insert(pair.first);

	std::ofstream outwriter(outputdir / "AggregateEntryInfo.txt");
	outwriter << "Aggregate information for all resource types (container mask active)\n";

	for (const std::string& Type : SortedTypes) {
		outwriter << "\n" << Type;
		const aggregate_t& agg = Map[Type];

		outwriter << "\n   Enabled Versions: ";
		for(u32 v : agg.versions)
			outwriter << std::to_string(v) << ", ";
		
		outwriter << "\n   Enabled Flags: ";
		for(u32 f : agg.flags)
			outwriter << std::to_string(f) << ", ";

		outwriter << "\n   Enabled Variations: ";
		for(u16 v : agg.variations)
			outwriter << std::to_string(v) << ", ";

		if(agg.hasDefaultHash)
			outwriter << "\n   StreamDB Entries";

		outwriter << "\n   Disabled Versions: ";
		for (u32 v : agg.offversions)
			outwriter << std::to_string(v) << ", ";

		outwriter << "\n   Disabled Flags: ";
		for (u32 f : agg.offflags)
			outwriter << std::to_string(f) << ", ";

		outwriter << "\n   Disabled Variations: ";
		for (u16 v : agg.offvariations)
			outwriter << std::to_string(v) << ", ";
	}

}

void RunExtraScripts(fspath gamedir, fspath outputdir)
{
	atlog("Running Extra Scripts");

	outputdir /= "extra_scripts";
	if(!std::filesystem::exists(outputdir))
		std::filesystem::create_directory(outputdir);

	Script_DumpResourceTypes(gamedir, outputdir);

	Script_AggregateEntryVars(gamedir, outputdir);
}