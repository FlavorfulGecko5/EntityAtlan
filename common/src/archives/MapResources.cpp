#include "MapResources.h"
#include "io/BinaryReader.h"
#include "io/BinaryWriter.h"

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
		r >> e.unknown[0] >> e.unknown[1] >> e.unknown[2];
	}

	if (version == VERSION_IDTECH8) {
		check(r.ReachedEOF());
		return true;
	}

	// Maps
	r.ReadBig(num_maps);
	list_maps = new mapstring_t[num_maps];
	MapResource_ReadStringList(r, list_maps, num_maps);


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

bool MapResource::AddFiles(std::string* entries, size_t num_newentries, BinaryWriter& writer) {
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
			atlog("Adding new resource type %s", typestring.c_str());

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
		writer << e.unknown[0] << e.unknown[1] << e.unknown[2];
	}
	for (const newentry_t& n : newentries) {
		writer.WriteBig(n.typeindex);
		writer << (u32)n.valstring.length();
		writer.WriteBytes(n.valstring.data(), n.valstring.length());
		writer << (u64)0 << (u64)0 << 0x8000'0000'0000'0000ULL;
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
