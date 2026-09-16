#pragma once
#include <string>
#include <unordered_map>
#include <filesystem>
#include "archives/GameEnums.h"

typedef std::filesystem::path fspath;
struct mz_zip_archive;

#define CFG_NAME "darkagesmod.txt"
#define CFG_NAME_V2 "atlanmod.txt"

class EntNode;

struct AtlanModConfig {

	struct namedlist_t {
		std::string listname;
		std::string* entries = nullptr;
		int numentries = 0;

		~namedlist_t() {
			delete[] entries;
		}
	};

	// If set, no other mod may have gameplay-affecting files loaded
	bool elevated = false;
	gamebit_t gameid = game_all;
	int requiredVersion = 1;
	int loadPriority = 0;
	std::unordered_map<std::string, std::string> alias;

	namedlist_t* listassets = nullptr;
	int numAssetlists = 0;

	namedlist_t* listedits = nullptr;
	int numEdits = 0;

	std::string* listmapspec = nullptr;
	int nummapspec = 0;

	~AtlanModConfig() {
		delete[] listassets;
		delete[] listmapspec;
		delete[] listedits;
	}

	void GetNormalizedName(const std::string& in_zipname, std::string& out_assettype, std::string& out_assetname) const;

	bool TryRead(const std::string& filepath);
	bool TryRead(const char* data, size_t length);

	// Returns false if an error occurred
	// Returns true if config successfully parsed, or no config was found
	// AddConfigTo: Optional zip file to add config to
	bool TryRead_FromDir(const fspath& ModDir, mz_zip_archive* AddConfigTo = nullptr);

	// Returns false if an error occurred
	// Returns true if config successfully parsed, or no config was found
	bool TryRead_FromZip(mz_zip_archive* zptr);

	// Get a list of all mod filepaths in a folder, excluding the config file
	static std::vector<fspath> GetModFiles(const fspath& moddir);

	private:
	bool TryRead_Internal(EntNode& root);

	public:
	AtlanModConfig() = default;
	AtlanModConfig(const AtlanModConfig& other) = delete;
	AtlanModConfig(const AtlanModConfig&& other) = delete;
	AtlanModConfig& operator=(const AtlanModConfig& other) = delete;
	AtlanModConfig& operator=(const AtlanModConfig&& other) = delete;
};