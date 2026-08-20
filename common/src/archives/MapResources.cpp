#include "MapResources.h"
#include "io/BinaryReader.h"
#include "io/BinaryWriter.h"

#define LAYERMASK_ALWAYS_LOADED 0x8000'0000'0000'0000ULL

void MapResource_ReadString(BinaryReader& r, MapResource::mapstring_t& s) {
	r >> s.length;
	r.ReadBytes(s.data, s.length);
}

void MapResource_ReadStringList(BinaryReader& r, MapResource::mapstring_t* list, unsigned num) {
	for (unsigned i = 0; i < num; i++) {
		r >> list[i].length;
		r.ReadBytes(list[i].data, list[i].length);
	}
}

bool MapResource::entry_t::IsAlwaysLoaded() const {
	return layermask[2] & LAYERMASK_ALWAYS_LOADED;
}

bool MapResource::entry_t::IsLayerLoaded(uint32_t index) const {
	return layermask[ index / 64ull] & (1ull << (index % 64ull));
}

void MapResource::entry_t::SetAlwaysLoaded() {
	layermask[0] = 0;
	layermask[1] = 0;
	layermask[2] = LAYERMASK_ALWAYS_LOADED;
}

bool MapResource::Parse(const char* data, size_t datalength)
{
	original_file_length = datalength;
	BinaryReader r(data, datalength);

	r.ReadBig(num_layers);
	if (num_layers & 0xFFFF0000) {
		timestamp = num_layers;
		version = VERSION_IDTECH7;
		r.ReadBig(num_layers);
	}
	else {
		version = VERSION_IDTECH8;
	}

	// Layers List
	list_layers = new mapstring_t[num_layers];
	MapResource_ReadStringList(r, list_layers, num_layers);

	// Unknown
	r.check32(0);

	// Resource Type Strings
	r.ReadBig(num_types);
	check(num_types < 2000);
	list_types = new mapstring_t[num_types];
	MapResource_ReadStringList(r, list_types, num_types);

	// Entries
	r.ReadBig(num_entries);
	check(num_entries < 100'000);
	list_entries = new entry_t[num_entries];

	for (unsigned i = 0; i < num_entries; i++) {
		entry_t& e = list_entries[i];

		r.ReadBig(e.typeindex);
		r >> e.path.length;
		r.ReadBytes(e.path.data, e.path.length);
		r >> e.layermask[0] >> e.layermask[1] >> e.layermask[2];
	}

	if (version == VERSION_IDTECH8) {
		check(r.ReachedEOF());
		return true;
	}

	// Maps
	r.ReadBig(num_maps);
	list_maps = new mapstring_t[num_maps];
	MapResource_ReadStringList(r, list_maps, num_maps);

	#ifdef _DEBUG
	for (u32 i = 0; i < num_entries; i++) {
		check(list_entries[i].typeindex < num_types);
	}
	#endif


	return r.ReachedEOF();
}

void MapResource_WriteStringList(BinaryWriter& w, MapResource::mapstring_t* list, unsigned int num) {
	w.WriteBig(num);
	for (unsigned i = 0; i < num; i++) {
		w << list[i].length;
		w.WriteBytes(list[i].data, list[i].length);
	}
}

#include <vector>
#include <unordered_map>
#include "atlan/AtlanLogger.h"

bool MapResource::AddFiles(std::string* entries, size_t num_newentries, bool LoadAll, BinaryWriter& writer) {
	writer.EnsureAvailable(original_file_length + num_newentries * 120);

	struct newentry_t {
		u32 typeindex;
		std::string valstring;
	};

	std::vector<newentry_t>  newentries;
	std::vector<std::string> newtypes;
	std::unordered_map<std::string, uint32_t> typeindexmap;

	// Initialize our data structures	
	newentries.reserve(num_newentries);
	typeindexmap.reserve(num_types * 1.5);
	for (u32 i = 0; i < num_types; i++) {
		typeindexmap[std::string(list_types[i].data, list_types[i].length)] = i;
	}

	// Parse our new entries
	// Notes: The game appears to be fine with duplicate or non-existant entries
	// so we don't need to filter those out
	for (size_t STRING_INDEX = 0; STRING_INDEX < num_newentries; STRING_INDEX++) {
		const std::string& newstring = entries[STRING_INDEX];

		size_t delimiter = newstring.find('/');
		if (delimiter == -1) {
			atlog("ERROR: Could not find typestring for %s", newstring.c_str());
			return false;
		}

		std::string typestring = newstring.substr(0, delimiter);
		std::string namestring = newstring.substr(delimiter + 1);

		//atlog("%s %s", typestring.c_str(), namestring.c_str());

		uint32_t typeindex;
		const auto& iter = typeindexmap.find(typestring);
		if (iter == typeindexmap.end()) {
			#ifdef _DEBUG
			atlog("Adding new resource type %s", typestring.c_str());
			#endif

			typeindex = num_types + (uint32_t)newtypes.size();
			typeindexmap[typestring] = typeindex;
			newtypes.push_back(typestring);
		}
		else {
			typeindex = iter->second;
		}

		newentries.push_back({typeindex, namestring});
	}

	/*
	* Write the file back out
	*/


	if (version == VERSION_IDTECH7) {
		writer.WriteBig(timestamp);
	}

	MapResource_WriteStringList(writer, list_layers, num_layers);
	writer << (int)0;

	// Write Type List
	const u32 MAX_TYPES = num_types + (u32) newtypes.size();
	writer.WriteBig(MAX_TYPES);
	for (u32 i = 0; i < num_types; i++) {
		writer << list_types[i].length;
		writer.WriteBytes(list_types[i].data, list_types[i].length);
	}
	for (const std::string& s : newtypes) {
		writer << (u32)s.length();
		writer.WriteBytes(s.data(), s.length());
	}

	// Write Entry List
	const u32 MAX_ENTRIES = num_entries + (u32)newentries.size();
	writer.WriteBig(MAX_ENTRIES);
	for (u32 i = 0; i < num_entries; i++) {
		entry_t& e = list_entries[i];
		writer.WriteBig(e.typeindex);
		writer << e.path.length;
		writer.WriteBytes(e.path.data, e.path.length);
		if(LoadAll)
			writer << (u64)0 << (u64)0 << LAYERMASK_ALWAYS_LOADED;
		else writer << e.layermask[0] << e.layermask[1] << e.layermask[2];
	}
	for (const newentry_t& n : newentries) {
		writer.WriteBig(n.typeindex);
		writer << (u32)n.valstring.length();
		writer.WriteBytes(n.valstring.data(), n.valstring.length());
		writer << (u64)0 << (u64)0 << LAYERMASK_ALWAYS_LOADED;
	}


	if (version == VERSION_IDTECH7) {
		MapResource_WriteStringList(writer, list_maps, num_maps);
	}

	#ifdef _DEBUG
	MapResource doublecheck;
	check(doublecheck.Parse(writer.GetBuffer(), writer.GetFilledSize()));
	#endif

	return true;
}

#if 0
bool MapResource::Merge(const MapResource& from)
{
	mapstring_t* new_types = new mapstring_t[num_types + from.num_types];
	entry_t* new_entries = new entry_t[num_entries + from.num_entries];
	u32 num_newtypes = 0, num_newentries = 0;

	std::unordered_map<std::string, u32> TypeMap;

	/*
	* STEP 1: Merge the Type Arrays
	* We preserve the type indices of the map we're merging into
	* because the likely use case will be merging smaller .mapresources
	* into massive ones (i.e. a level-specific .mapresources into common)
	* Doing things this way enables a simple memcpy of the large file's arrays
	*/

	// Copy over the "to" types
	for (u32 i = 0; i < num_types; i++) {
		TypeMap[list_types[i].string()] = i;
	}
	memcpy(new_types, list_types, num_types * sizeof(mapstring_t));
	num_newtypes = num_types;
	
	// Copy over the "from" types
	for (u32 i = 0; i < from.num_types; i++) {
		const auto pair = TypeMap.try_emplace(from.list_types[i].string(), num_newtypes);
		if (pair.second) {
			new_types[num_newtypes++] = from.list_types[i];
		}
	}

	/*
	* STEP 2: Merge the Entry Arrays
	* We're going to generously assume that having duplicate entries does not
	* impact resource loading because the game (hopefully) has checks to prevent
	* duplicate asset loading
	*/

	//std::set<std::string> EntrySet;
	// Copy over the "to" entries
	//for (u32 i = 0; i < num_entries; i++) {
	//	EntrySet.insert(list_types[list_entries[i].typeindex].string() + list_entries[i].path.string());
	//}

	// Copy over the "to" entries
	memcpy(new_entries, list_entries, num_entries * sizeof(entry_t));
	num_newentries = num_entries;

	// Copy over the "from" entries and adjust their type indices
	memcpy(new_entries + num_newentries, from.list_entries, from.num_entries * sizeof(entry_t));
	num_newentries += from.num_entries;

	// Adjust merged entries
	for (u32 i = num_entries; i < num_newentries; i++) {

		// Must adjust the type index
		new_entries[i].typeindex = TypeMap[
			from.list_types[
				new_entries[i].typeindex
			].string()
		];

		// We must enable always-load for merged entries
		// because their layermask will no longer be valid
		// (and may potentially be out of bounds)
		new_entries[i].SetAlwaysLoaded();

		#if 0
		if (new_types[new_entries[i].typeindex].string() == "mapentities") {
			new_entries[i].layermask[2] = 0;
		}
		#endif
	}

	/*
	* STEP 3: Replace this MapResource's arrays with the new ones
	*/
	delete[] list_types;
	delete[] list_entries;
	list_types = new_types;
	list_entries = new_entries;
	num_types = num_newtypes;
	num_entries = num_newentries;


	#ifdef _DEBUG

	BinaryWriter outwriter;
	this->AddFiles(nullptr, 0, false, outwriter);
	MapResource errorchecker;
	errorchecker.Parse(outwriter.GetBuffer(), outwriter.GetFilledSize());

	#endif

	return true;
}
#endif

std::string MapResource::ToString()
{
	std::string output;

	output.append("layers {\n");
	for (u32 i = 0; i < num_layers; i++) {
		mapstring_t& layer = list_layers[i];
		output.append("\t\"");
		output.append(layer.string());
		output.append("\"\n");
	}
	output.append("}\n");

	for (u32 i = 0; i < num_entries; i++) {
		entry_t& e = list_entries[i];

		mapstring_t type = list_types[e.typeindex];

		output.push_back('"');
		output.append(type.data, type.length);
		output.push_back('/');
		output.append(e.path.data, e.path.length);

		if (!e.IsAlwaysLoaded()) {
			output.append("\" \"");
			for (u32 layer_index = 0; layer_index < num_layers; layer_index++) {
				if (e.IsLayerLoaded(layer_index)) {
					output.append(list_layers[layer_index].string());
					output.push_back(' ');
				}
			}
			output.append("\"\n");
		}
		else output.append("\"\n");
	}
	return output;
}
