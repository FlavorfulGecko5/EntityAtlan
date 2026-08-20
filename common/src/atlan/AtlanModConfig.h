#pragma once
#include <string>
#include <unordered_map>

#define CFG_NAME "darkagesmod.txt"

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

	private:
	bool TryRead_Internal(EntNode& root);
};