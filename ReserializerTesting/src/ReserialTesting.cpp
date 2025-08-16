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

int main()
{
	const fspath gamedir = "D:/Steam/steamapps/common/DOOMTheDarkAges";
	const fspath filedir = "D:/DA/atlan";

	/*
	* This assumes the files were originally deserialized with include_originals = false
	*/
	Deserializer::DeserialInit(gamedir, filedir, false);


	std::vector<fspath> entitydefs;
	using namespace std::filesystem;
	for (const directory_entry& entry : recursive_directory_iterator(filedir / "entityDef")) {
		if(is_directory(entry))
			continue;

		if(entry.path().extension() == ".decl")
			entitydefs.push_back(entry.path());
	}
	std::cout << "Testing " << entitydefs.size() << " entitydefs\n";

	int i = 0;
	for (const fspath& entity : entitydefs) {		

		/*
		* Read the deserialized file into an EntityParser
		*/
		EntityParser original(entity.string(), ParsingMode::PERMISSIVE);
		BinaryWriter serialized(static_cast<size_t>(file_size(entity) * 1.1));

		/*
		* Serialize the parsed file
		*/
		int warnings = Reserializer::Serialize(*original.getRoot(), serialized, rt_entityDef);
		if (warnings != 0) {
			std::cout << entity << "\n";
		}

		/*
		* Deserialize it again
		*/
		BinaryReader reader(serialized.GetBuffer(), serialized.GetFilledSize());
		std::string deserialized;
		deserialized.reserve(10000);
		Deserializer::DeserialSingle(reader, deserialized, rt_entityDef);

		/*
		* Read our deserialized form into an EntityParser
		* and compare with the original deserialized file
		*/
		// Pretty inefficient but eh... TODO: Try to pre-calculate size of text
		EntityParser second(ParsingMode::PERMISSIVE);
		second.EditTree(deserialized, second.getRoot(), 0, 0, 0, 0);
		ReserialCompare(*original.getRoot(), *second.getRoot());

		i++;
	}

	std::cout << "DONE\n";

	return 0;
}