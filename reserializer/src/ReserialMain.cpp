#include "serialcore.h"
#include <string_view>
#include <chrono>
#include "io/BinaryWriter.h"
#include "entityslayer/EntityParser.h"
#include "archives/ResourceEnums.h"
#include "atlan/AtlanLogger.h"
#include "ReserialMain.h"

#define TIMESTART(ID) auto EntityProfiling_ID  = std::chrono::high_resolution_clock::now();

#define TIMESTOP(ID, msg) { \
	auto timeStop = std::chrono::high_resolution_clock::now(); \
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(timeStop - EntityProfiling_ID); \
	printf("%s: %zu\n", msg, duration.count());\
}

int Reserializer::Serialize(const EntNode& root, BinaryWriter& writer, ResourceType restype, const char* eofblob, size_t eofbloblength)
{
	reserial::warningcount = 0;
	if (restype == rt_entityDef) {
		reserial::rs_start_entitydef(root, writer);
	}
	else if (restype & rtc_logic_decl) {
		reserial::rs_start_logicdecl(root, writer, restype);
	}
	else if (restype & rt_mapentities) {
		//TIMESTART(maptime)
		reserial::rs_start_mapentity(root, writer, eofblob, eofbloblength);
		//TIMESTOP(maptime, "Serialization Time: ")
		//printf("Reallocations: %d, Used: %zu, Max: %zu\n", writer.GetReallocCount(), writer.GetFilledSize(), writer.GetMaxCapacity());
	}
	return reserial::warningcount;
}

int Reserializer::Serialize(const char* data, size_t length, BinaryWriter& writer, ResourceType restype)
{
	try {
		EntityParser parser(ParsingMode::PERMISSIVE, std::string_view(data, length), false);
		return Serialize(*parser.getRoot(), writer, restype, parser.eofblob, parser.eofbloblength);
	}
	catch (std::exception e) {
		atlog << "ERROR: Failed to read data stream into EntityParser\nMessage: " << e.what();
		return 1;
	}
}

int Reserializer::Serialize(const char* filepath, BinaryWriter& writer, ResourceType restype)
{
	try {
		EntityParser parser(std::string(filepath), ParsingMode::PERMISSIVE);
		return Serialize(*parser.getRoot(), writer, restype, parser.eofblob, parser.eofbloblength);
	}
	catch (std::exception e) {
		atlog << "ERROR: Failed to read file into EntityParser\nMessage: " << e.what();
		return 1;
	}
}

bool Reserializer::IsSerialized(const char* data, size_t length, ResourceType restype)
{
	if (restype == rt_entityDef || restype & rtc_logic_decl) {
		return length > 0 && *data == '\0';
	}

	if (restype == rt_mapentities) {
		return length > 4 && data[3] == '\0'; // data[3] == The last byte of the submap count integer
	}

	return true;
}
