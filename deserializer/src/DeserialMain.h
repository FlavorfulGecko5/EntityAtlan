#pragma once
#include <filesystem>

typedef std::filesystem::path fspath;

struct deserialconfig_t
{
	bool deserial_entitydefs = true;
	bool deserial_logicdecls = true;
	bool deserial_mapentities = true;
	bool remove_binaries = true;
	bool include_original = false;
	bool indent = true;
};

namespace Deserializer
{
	void DeserialMain(const fspath& gamedir, const fspath& filedir, deserialconfig_t config);
}