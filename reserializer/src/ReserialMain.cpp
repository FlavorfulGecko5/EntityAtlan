#include "serialcore.h"
#include <string_view>
#include "io/BinaryWriter.h"
#include "entityslayer/EntityParser.h"
#include "archives/ResourceEnums.h"
#include "atlan/AtlanLogger.h"
#include "ReserialMain.h"

// Confirmed - atof seems to match for going from string --> float
// 0x3F333333 = 0.699999988
// 0x3C88889A = 0.0166666992

// -31.5 = 0xC1FC0000
// 44.0000114 = 0x42300003


// But what about from float --> string?


//int main() {

	//printf("reserial main");

	//char buffer[128];
	//float f;
	//snprintf(buffer, 128, "%1.10f", f);

	//EntNode e;
	//BinaryWriter w(100);
	//reserial::rs_float(e, w);

	//EntityParser parser("D:/DA/atlan/entityDef/player.decl", ParsingMode::PERMISSIVE);

	//BinaryWriter writer(2000, 4);
	//reserial::rs_start_entitydef(*parser.getRoot(), writer);

	//writer.SaveTo("../input/please.bin");
	
	//return 0;
//}

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
		reserial::rs_start_mapentity(root, writer, eofblob, eofbloblength);
	}
	return reserial::warningcount;
}

int Reserializer::Serialize(const char* data, size_t length, BinaryWriter& writer, ResourceType restype)
{
	try {
		EntityParser parser(ParsingMode::PERMISSIVE, std::string_view(data, length), false);
		return Serialize(*parser.getRoot(), writer, restype, parser.eofblob, parser.eofbloblength);
	}
	catch (...) {
		atlog << "ERROR: Failed to read data stream into EntityParser\n";
		return 1;
	}
}

int Reserializer::Serialize(const char* filepath, BinaryWriter& writer, ResourceType restype)
{
	try {
		EntityParser parser(std::string(filepath), ParsingMode::PERMISSIVE);
		return Serialize(*parser.getRoot(), writer, restype, parser.eofblob, parser.eofbloblength);
	}
	catch (...) {
		atlog << "ERROR: Failed to read file into EntityParser\n";
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
