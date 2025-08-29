#include "cleaner.h"
#include "entityslayer/EntityParser.h"
#include <cassert>
#include <fstream>

typedef EntNode entnode;

// Takes the JSON form of the idlib and converts it to the traditional C++ header file


template<typename T>
bool ParseWholeNumber(const char* ptr, int len, T& writeTo) {
	if(len == 0)
		return false;

	const char* max = ptr + len;
	bool negative;
	if (*ptr == '-') {
		negative = true;
		ptr++;
	}
	else {
		negative = false;
	}

	T val = 0;
	while (ptr < max) {
		if(*ptr > '9' || *ptr < '0')
			return false;
		val = val * 10 + (*ptr - '0');
		ptr++;
	}

	writeTo = negative ? val * -1 : val;
	return true;
}

template <typename N>
void HexString(EntNode& value, std::string& writeto)
{
	N num;
	assert(ParseWholeNumber(value.ValuePtr(), value.ValueLength(), num));

	writeto.append("0x");
	const uint8_t* lastbyte = reinterpret_cast<uint8_t*>(&num) + sizeof(N) - 1;
	int numchars = sizeof(N) * 2;
	for (int i = 0; i < numchars; i++) {

		uint8_t val = (*lastbyte & 0xF0) >> 4;
		num <<= 4;

		assert(val <= 15);

		if(val < 10)
			writeto.push_back('0' + val);
		else
			writeto.push_back('A' + val - 10);
	}

}

void ConvertEnums(EntNode& list, std::string& writeto) 
{
	for (int i = 0; i < list.getChildCount(); i++)
	{
		entnode& enumobj = *list.ChildAt(i);
		assert(enumobj.getFlags() & entnode::NF_Braces);

		writeto.append("// Hash: ");
		HexString<uint32_t>(enumobj["\"hash\""], writeto);

		writeto.append("\nenum ");
		writeto.append(enumobj["\"name\""].getValueUQ());
		writeto.append(" : TBD {\n"); // Todo: Basetype
		

		entnode& valuelist = enumobj["\"values\""];
		for (int valIndex = 0; valIndex < valuelist.getChildCount(); valIndex++) {
			entnode& val = *valuelist.ChildAt(valIndex);

			writeto.push_back('\t');
			writeto.append(val["\"name\""].getValueUQ());
			writeto.append(" = ");
			writeto.append(val["\"value\""].getValue());
			writeto.append(", // Hash: ");

			HexString<uint64_t>(val["\"hash\""], writeto);
			writeto.push_back('\n');
		}
		writeto.append("};\n\n");
	}
}

enum specifierflags : uint64_t {
	SPECIFIERFLAG_CONST = 1, // Hash: 0xF420CFE2DDF746AC
	SPECIFIERFLAG_VOLATILE = 2, // Hash: 0xD4DBA108F8FCB55E
	SPECIFIERFLAG_AUTO = 4, // Hash: 0xFFCE3BC4F53745BB
	SPECIFIERFLAG_REGISTER = 8, // Hash: 0x294C162A6AD97024
	SPECIFIERFLAG_STATIC = 16, // Hash: 0xEA31A90BB6A66FAC
	SPECIFIERFLAG_EXTERN = 32, // Hash: 0xC2524F2EC5105613
	SPECIFIERFLAG_MUTABLE = 64, // Hash: 0x2A0B762A4D6E7BFC
	SPECIFIERFLAG_INLINE = 128, // Hash: 0xBE713158FB94B70E
	SPECIFIERFLAG_VIRTUAL = 256, // Hash: 0xD6F2DBDCB6A5B412
	SPECIFIERFLAG_EXPLICIT = 512, // Hash: 0x55577C666538C195
	SPECIFIERFLAG_FRIEND = 1024, // Hash: 0xD6EA5E264051FB9B
	SPECIFIERFLAG_TYPEDEF = 2048, // Hash: 0xAAE5C2903704537A
	SPECIFIERFLAG_METASTATE = 4096, // Hash: 0xB458233E9DFCBF66
	SPECIFIERFLAG_UNINITIALIZEDTYPE = 8192, // Hash: 0x123E74498701FD9C
	SPECIFIERFLAG_SAVESKIP = 16384, // Hash: 0xFFC628B62503862B
	SPECIFIERFLAG_GPU = 32768, // Hash: 0x6C883887E67F0251
	SPECIFIERFLAG_EDIT = 65536, // Hash: 0x436A03BDFABC56F2
	SPECIFIERFLAG_DESIGN = 131072, // Hash: 0x27E3AB9871EBB049
	SPECIFIERFLAG_DEF = 262144, // Hash: 0x09FB635649260C55
	SPECIFIERFLAG_ENUMBITFLAGS = 524288, // Hash: 0xC4EA75B7BEE8DEB2
	SPECIFIERFLAG_SCRIPTDEFINE = 1048576, // Hash: 0x8B7A9D7BF10C6892
	SPECIFIERFLAG_BOLD = 2097152, // Hash: 0x89C64E6C30FD5993
	SPECIFIERFLAG_META = 4194304, // Hash: 0xBC96ADEEF8E973C5
	SPECIFIERFLAG_HIDDEN = 8388608, // Hash: 0x344DFAFEDE6EDD75
	SPECIFIERFLAG_EXPORTED = 16777216, // Hash: 0x82F03A6AC31181CA
	SPECIFIERFLAG_PURE = 33554432, // Hash: 0xB8EB9A120305CE49
	SPECIFIERFLAG_DISPLAY = 67108864, // Hash: 0xFE28AAA8458BB255
	SPECIFIERFLAG_NODEINPUT = 134217728, // Hash: 0x1EB0DD31D8090E0D
	SPECIFIERFLAG_NODEOUTPUT = 268435456, // Hash: 0x875C7B1B937FB7A7
	SPECIFIERFLAG_PARM_MUTABLE = 536870912, // Hash: 0xFEF8B0AF4D7624CD
	SPECIFIERFLAG_NOCOMMENT = 1073741824, // Hash: 0x6CBD9759867FBF33
	SPECIFIERFLAG_ALLOWDEFAULTLOAD = 2147483648, // Hash: 0x21AD3DAE4B8CCC64
	SPECIFIERFLAG_NO_SPAWN = 4294967296, // Hash: 0x91DBB2E8E9582049
	SPECIFIERFLAG_EXTERN_TEMPLATE = 8589934592, // Hash: 0xF81E928E16C9D0E6
	SPECIFIERFLAG_DELETE = 17179869184, // Hash: 0x7B2747FA0298EEB1
	SPECIFIERFLAG_DEFAULT = 34359738368, // Hash: 0xBABCEAD1F8B63EDF
	SPECIFIERFLAG_NOTNULL = 68719476736, // Hash: 0x7BE24DB8D5E3D3AE
	SPECIFIERFLAG_ENUMSKIPLAST = 137438953472, // Hash: 0xBAB8CDAED30A6D4B
	SPECIFIERFLAG_COMPONENT_OPTIONAL = 274877906944, // Hash: 0xA62DD1554A33B384
	SPECIFIERFLAG_COMPONENT_EXCLUDE = 549755813888, // Hash: 0x28501B5AA61B49D3
	SPECIFIERFLAG_UI_BIND = 1099511627776, // Hash: 0xBB992D7A42A67EF1
	SPECIFIERFLAG_UI_PROP = 2199023255552, // Hash: 0x2F2FC875E7A48935
	SPECIFIERFLAG_UI_TRACK_PREVIOUS = 4398046511104, // Hash: 0xD53202076DC332D2
	SPECIFIERFLAG_UI_OPTIONAL = 8796093022208, // Hash: 0x7C59B5C6738EF0F3
	SPECIFIERFLAG_LOGIC_CUSTOM_EVENT = 17592186044416, // Hash: 0xDD68E8081163C01C
	SPECIFIERFLAG_DEFERREDLOAD = 35184372088832, // Hash: 0xC79EE6A986789526
	SPECIFIERFLAG_IGNORECOMPARISON = 70368744177664, // Hash: 0x6FCE9FFD0B525100	
};

void FlagStrings(uint64_t flags, std::string& writeto)
{
	#define e(PARMFLAG) if(flags & SPECIFIERFLAG_##PARMFLAG) { writeto.append(#PARMFLAG); writeto.push_back('|');}

	e(CONST) e(VOLATILE) e(AUTO) e(REGISTER) e(STATIC) e(EXTERN) e(MUTABLE) e(INLINE)
	e(VIRTUAL) e(EXPLICIT) e(FRIEND) e(TYPEDEF) e(METASTATE) e(UNINITIALIZEDTYPE) e(SAVESKIP)
	e(GPU) e(EDIT) e(DESIGN) e(DEF) e(ENUMBITFLAGS) e(SCRIPTDEFINE) e(BOLD) e(META) e(HIDDEN)
	e(EXPORTED) e(PURE) e(DISPLAY) e(NODEINPUT) e(NODEOUTPUT) e(PARM_MUTABLE) e(NOCOMMENT)
	e(ALLOWDEFAULTLOAD) e(NO_SPAWN) e(EXTERN_TEMPLATE) e(DELETE) e(DEFAULT) e(NOTNULL)
	e(ENUMSKIPLAST) e(COMPONENT_OPTIONAL) e(COMPONENT_EXCLUDE) e(UI_BIND) e(UI_PROP)
	e(UI_TRACK_PREVIOUS) e(UI_OPTIONAL) e(LOGIC_CUSTOM_EVENT) e(DEFERREDLOAD) e(IGNORECOMPARISON)

	if(flags != 0)
		writeto.pop_back(); // Pop the trailing OR symbol
}

void SizeString(EntNode& sizenode, std::string& writeto)
{
	// Some empty structs have an FF'd size, so we must ensure the final output is a signed number
	union {
		uint32_t u;
		int32_t s;
	} structsize;

	//entnode& sizenode = structnode["\"size\""];
	assert(ParseWholeNumber(sizenode.ValuePtr(), sizenode.ValueLength(), structsize.u));
	writeto.append(std::to_string(structsize.s));
}

void ConvertStruct(EntNode& structnode, std::string& writeto)
{
	assert(structnode.getFlags() & entnode::NF_Braces);

	writeto.append("// Size: ");
	SizeString(structnode["\"size\""], writeto);

	writeto.append(", Hash: ");
	HexString<uint32_t>(structnode["\"hash\""], writeto);
	writeto.push_back('\n');

	writeto.append("struct __cppobj ");
	writeto.append(structnode["\"name\""].getValueUQ());

	{
		std::string_view supertype = structnode["\"super_type\""].getValueUQ();
		if (!supertype.empty()) {
			writeto.append(" : ");
			writeto.append(supertype);
		}
	}
	writeto.append(" {\n");

	entnode& valuelist = structnode["\"variables\""];
	for (int i = 0; i < valuelist.getChildCount(); i++) {
		entnode& val = *valuelist.ChildAt(i);

		// First comment containing offset + size + hash
		writeto.append("\t// Offset: ");
		SizeString(val["\"offset\""], writeto);
		writeto.append(", Size: ");
		SizeString(val["\"size\""], writeto);
		writeto.append(", Hash: ");
		HexString<uint64_t>(val["\"hash\""], writeto);
		writeto.push_back('\n');

		// Second (Optional) comment containing specifier flags
		{
			uint64_t specflags;
			entnode& flagsnode = val["\"flags\""];
			assert(ParseWholeNumber(flagsnode.ValuePtr(), flagsnode.ValueLength(), specflags));

			if (specflags != 0) {
				writeto.append("\t// ");
				FlagStrings(specflags, writeto);
				writeto.push_back('\n');
			}
		}

		// Third (Optional) developer comment
		{
			std::string_view comment = val["\"comment\""].getValueUQ();
			if (!comment.empty()) {
				writeto.append("\t// ");
				writeto.append(comment);
				writeto.push_back('\n');
			}
		}

		writeto.push_back('\t');
		writeto.append(val["\"type\""].getValueUQ());
		writeto.append(val["\"ops\""].getValueUQ()); // Cleaner phase 1 should be flexible enough for this
		writeto.push_back(' ');
		writeto.append(val["\"name\""].getValueUQ());
		writeto.append(";\n");
	}

	writeto.append("};\n\n");
}

void idlibCleaning::JsonToHeader() 
{
	std::string writeto;
	writeto.reserve(15000000);

	EntityParser parser("idlib.json", ParsingMode::JSON);
	entnode& root = *parser.getRoot();
	assert(root.getChildCount() == 1);

	entnode& projects = *root.ChildAt(0);
	assert(projects.getFlags() & entnode::NF_Brackets);
	assert(projects.getChildCount() == 2);
	
	// Enums
	for (int i = 0; i < projects.getChildCount(); i++) {
		entnode& p = *projects.ChildAt(i);
		assert(p.getFlags() & entnode::NF_Braces);

		entnode& enumlist = p["\"enums\""];
		assert(enumlist.getFlags() & entnode::NF_Brackets);

		ConvertEnums(enumlist, writeto);
	}

	// Structs
	for (int i = 0; i < projects.getChildCount(); i++) {
		entnode& p = *projects.ChildAt(i);
		entnode& structlist = p["\"classes\""];

		assert(structlist.getFlags() & entnode::NF_Brackets);

		for (int structindex = 0; structindex < structlist.getChildCount(); structindex++) {
			ConvertStruct(*structlist.ChildAt(structindex), writeto);
		}
	}


	std::ofstream output("idlib.h", std::ios_base::binary);
	output << writeto;
	output.close();
}