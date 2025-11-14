#include <iostream>
#include <filesystem>
#include <cassert>
#include <fstream>
#include "archives/ResourceEnums.h"
#include "entityslayer/EntityParser.h"
#include "io/BinaryWriter.h"
#include "io/BinaryReader.h"
#include "ReserialMain.h"
#include "DeserialMain.h"


typedef std::filesystem::path fspath;


void ReserialCompare(EntNode& a, EntNode& b)
{
	assert(a.getFlags() == b.getFlags());
	assert(a.getChildCount() == b.getChildCount());
	assert(a.NameLength() == b.NameLength());

	assert(memcmp(a.NamePtr(), b.NamePtr(), a.NameLength()) == 0);

	// MONITOR: It seems our floating point conversion is round-trip.
	// But if this changes we'll start getting errors here
	assert(a.ValueLength() == b.ValueLength());
	assert(memcmp(a.ValuePtr(), b.ValuePtr(), a.ValueLength()) == 0);

	//if ((a.ValueLength() != b.ValueLength()) ||  ( memcmp(a.ValuePtr(), b.ValuePtr(), a.ValueLength()) != 0) ) {
	//	std::cout << "value mismatch\n";
	//}

	for (int i = 0; i < a.getChildCount(); i++) {
		ReserialCompare(*a.ChildAt(i), *b.ChildAt(i));
	}
}

void ReserialCompare_MapRoot(EntNode& original, EntNode& transformed)
{
	// Need to adjust how we compare the mapentities roots because
	// we're no longer serializing the meta section
	// In both cases, don't compare the final children since that's the header chunk
	if (original.getChildCount() == transformed.getChildCount()) {
		for(int i = 0; i < original.getChildCount() - 1; i++)
			ReserialCompare(original[i], transformed[i]);
	}
	else {
		assert(original.getChildCount() - 1 == transformed.getChildCount());
		assert(original[0].getName() == "metadata");
		assert(transformed[0].getName() != "metadata");

		for (int i = 1; i < original.getChildCount() - 1; i++) {
			ReserialCompare(original[i], transformed[i-1]);
		}
	}

	// First line of the header chunk will be modified by the serialization process
	// We must manually compare it to ensure accuracy

	EntNode& lastOriginal = original[original.getChildCount() - 1];
	EntNode& lastTransformed = transformed[transformed.getChildCount() - 1];

	if (lastOriginal.getName() != "headerchunk") {
		assert(lastTransformed.getName() != "headerchunk");
		ReserialCompare(lastOriginal, lastTransformed);
	}
	else {
		assert(lastOriginal.getChildCount() == lastTransformed.getChildCount());

		// Compare everything except for the first line (which is modified)
		// Should be sufficient for verifying the process is round-trip
		for (int i = 1; i < lastOriginal.getChildCount(); i++) {
			ReserialCompare(lastOriginal[i], lastTransformed[i]);
		}
	}
}

void RunTest(const fspath& folder, const fspath& extension, ResourceType restype)
{
	using namespace std::filesystem;

	std::vector<fspath> targetfiles;

	for (const directory_entry& entry : recursive_directory_iterator(folder)) {
		if(is_directory(entry))
			continue;

		if (entry.path().extension() == extension)
			targetfiles.push_back(entry.path());
	}

	std::cout << "Testing " << targetfiles.size() << " files in " << folder << "\n";

	int i = 0;
	for (const fspath& entity : targetfiles) {

		std::cout << entity << "\n";

		/*
		* Read the deserialized file into an EntityParser
		*/
		EntityParser original(entity.string(), ParsingMode::PERMISSIVE);
		BinaryWriter serialized(static_cast<size_t>(file_size(entity) * 1.1));

		/*
		* Serialize the parsed file
		*/
		int warnings = Reserializer::Serialize(*original.getRoot(), serialized, restype, original.eofblob, original.eofbloblength);
		if (warnings != 0) {
			std::cout << entity << "\n";
		}

		/*
		* Deserialize it again
		*/
		BinaryReader reader(serialized.GetBuffer(), serialized.GetFilledSize());
		std::string deserialized;
		deserialized.reserve(10000);
		Deserializer::DeserialSingle(reader, deserialized, restype);

		//std::ofstream TEMP("../input/temp_map.txt", std::ios_base::binary);
		//TEMP << deserialized;
		//TEMP.close();

		/*
		* Read our deserialized form into an EntityParser
		* and compare with the original deserialized file
		*/
		EntityParser second(ParsingMode::PERMISSIVE, std::string_view(deserialized), false);

		if (restype == rt_mapentities) {
			ReserialCompare_MapRoot(*original.getRoot(), *second.getRoot());
		}
		else {
			ReserialCompare(*original.getRoot(), *second.getRoot());
		}
		

		i++;
	}

}

int main()
{
	const fspath gamedir = "D:/Steam/steamapps/common/DOOMTheDarkAges";
	const fspath filedir = "D:/DA/atlan";

	/*
	* This assumes the files were originally deserialized with include_originals = false
	*/
	Deserializer::DeserialInit(gamedir, filedir, false);

	//RunTest(filedir / "entityDef", ".decl", rt_entityDef);
	//RunTest(filedir / "logicClass", ".decl", rt_logicClass);
	//RunTest(filedir / "logicEntity", ".decl", rt_logicEntity);
	//RunTest(filedir / "logicFX", ".decl", rt_logicFX);
	//RunTest(filedir / "logicLibrary", ".decl", rt_logicLibrary);
	//RunTest(filedir / "logicUIWidget", ".decl", rt_logicUIWidget);

	RunTest(filedir / "mapentities", ".mapentities", rt_mapentities);

	std::cout << "DONE\n";

	return 0;
}