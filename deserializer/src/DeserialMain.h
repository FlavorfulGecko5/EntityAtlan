#pragma once
#include <filesystem>

enum ResourceType : unsigned;
class BinaryReader;
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
	// Will be called by DeserialMain automatically
	// This is here for using the Deserializer independently of DeserialMain
	void DeserialInit(const fspath& gamedir, const fspath& filedir, bool p_include_originals);

	void DeserialSingle(BinaryReader& reader, std::string& writeto, ResourceType restype);

	void DeserialMain(const fspath& gamedir, const fspath& filedir, deserialconfig_t config);
}