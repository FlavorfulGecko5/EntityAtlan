#pragma once
#include <string>
#include <unordered_map>

#define CFG_NAME "darkagesmod.txt"

class EntNode;

struct AtlanModConfig {

	int requiredVersion = 1;
	int loadPriority = 0;
	std::unordered_map<std::string, std::string> alias;

	void GetNormalizedName(const std::string& in_zipname, std::string& out_assettype, std::string& out_assetname) const;

	bool TryRead(const std::string& filepath);
	bool TryRead(const char* data, size_t length);

	private:
	bool TryRead_Internal(EntNode& root);
};