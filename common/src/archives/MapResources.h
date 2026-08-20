#pragma once
#include <string>

class BinaryWriter;

// Parsed MapResources Data
// This struct does not own any of the actual string data
// It is expected that the strings are stored elsewhere for
// the duration of this structure's lifetime
struct MapResource {

	enum version_t {
		VERSION_IDTECH7,
		VERSION_IDTECH8
	};

	struct mapstring_t {
		unsigned int length = 0;
		const char* data = nullptr;

		std::string string() const {
			return std::string(data, length);
		}
	};

	struct entry_t {
		unsigned int typeindex;
		mapstring_t path;
		uint64_t layermask[3];

		bool IsAlwaysLoaded() const;
		bool IsLayerLoaded(uint32_t index) const;
		void SetAlwaysLoaded();
	};

	size_t original_file_length = 0;
	version_t version;
	unsigned int timestamp;

	mapstring_t* list_layers  = nullptr;
	mapstring_t* list_types   = nullptr;
	entry_t*     list_entries = nullptr;
	mapstring_t* list_maps    = nullptr;

	unsigned num_layers = 0;
	unsigned num_types = 0;
	unsigned num_entries = 0;
	unsigned num_maps = 0;

	~MapResource() {
		delete[] list_layers;
		delete[] list_types;
		delete[] list_entries;
		delete[] list_maps;
	}
	
	bool Parse(const char* data, size_t datalength);

	// If LoadAll: Iterate through the entries and disable all their layermasks
	bool AddFiles(std::string* entries, size_t numentries, bool LoadAll, BinaryWriter& writer);

	#if 0
	// Merges data from another .mapresources file into this one
	// Note: Blanket-merging mapresources files like this is a bad idea and should
	// be avoided. Doing this can easily overload the game and cause various crashes,
	// such as from reaching the material2 limit. It will also import the mapentities
	// of the level you're merging from.
	bool Merge(const MapResource& from);
	#endif

	std::string ToString();

};
