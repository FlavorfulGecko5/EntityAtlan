#include "deserialcore.h"
#include "generated/deserialgenerated.h"
#include "io/BinaryReader.h"
#include "atlan/AtlanLogger.h"
#include "archives/ResourceEnums.h"
#include <set>
#include <cassert>

// The responsible thing to do is crash the program instead of allowing it to run on unknown data
#ifndef _DEBUG
#undef assert
#define assert(OP) if(!(OP)) {throw std::exception("Deserializer is outdated! Please update AtlanResourceExtractor when a newer version is available!");}
#endif

#define dsfunc_m(NAME) void NAME(BinaryReader& reader, std::string& writeTo)

thread_local DeserialMode deserial::deserialmode = DeserialMode::entitydef;
thread_local bool deserial::include_originals = true;

// Built before deserialization begins
std::unordered_map<uint64_t, std::string> deserial::declHashMap;
std::unordered_map<uint64_t, entityclass_t> deserial::entityclassmap;

// Built during deserialization
// Strings follow the format of:
// property/stack/trace/entityfarmhash
std::unordered_map<std::string, deserialTypeInfo> typeinfoHistory;

// Used for stack tracing
thread_local std::vector<std::string_view> propertyStack;
thread_local deserialTypeInfo lastAccessedTypeInfo;
thread_local int deserial::warning_count = 0;
thread_local int fileCount = 0;
thread_local const void* fileStartAddress = nullptr; // For debugger view
thread_local uint64_t currentEntHash = 0;

void LogWarning(std::string_view msg) {
	std::string propString;
	propString.reserve(200);
	for (std::string_view s : propertyStack) {
		propString.append(s);
		propString.push_back('/');
	}

	if(!propString.empty())
		propString.pop_back();

	atlog << "WARNING: " << propString << " " << msg << "\n";
	//printf("WARNING: %.*s %.*s\n", (int)propString.length(), propString.data(), (int)msg.length(), msg.data());
	deserial::warning_count++;
}


void deserializer::Exec(BinaryReader& reader, std::string& writeTo) const {
	propertyStack.emplace_back(name);

	writeTo.append(name);
	writeTo.append(" = ");

	// A property hash is immediately followed by the stem/leaf byte, and it's block length
	uint8_t hasChildren;
	uint32_t length;
	assert(reader.ReadLE(hasChildren));
	assert(reader.ReadLE(length));
	assert(hasChildren == 0 || hasChildren == 1);

	// Create a new binary reader for the property to be read and skip ahead in the original
	BinaryReader propReader(reader.GetNext(), length);
	assert(reader.GoRight(length));

	if (arrayLength > 0) {
		deserial::ds_staticList(propReader, writeTo, *this);
	}
	else {
		callback(propReader, writeTo);
	}

	propertyStack.pop_back();
}

#pragma pack(push, 1)
struct inherithash_t {
	uint8_t leaf = 1;
	uint32_t length = 8;
	uint64_t hash = 0;
};
#pragma pack(pop)

void deserial::ds_start_entitydef(BinaryReader& reader, std::string& writeTo, uint64_t entityhash)
{
	lastAccessedTypeInfo = {nullptr, nullptr};
	fileStartAddress = (const void*)reader.GetBuffer();

	#define HASH_EDIT 0xC2D0B77C0D10391CUL

	uint8_t bytecode;
	uint32_t length;
	inherithash_t inherit;
	uint64_t editHash;
	
	assert(reader.ReadLE(bytecode));
	assert(bytecode == 0);
	assert(reader.ReadLE(length));
	assert(length == reader.GetRemaining());
	
	// Read the inheritance hash
	assert(reader.ReadLE(inherit.hash));
	if(inherit.hash != 0)
	{
		const deserializer inheritVar = { &ds_pointerdecl, "inherit", 0 };
		BinaryReader inheritReader(reinterpret_cast<char*>(&inherit), sizeof(inherithash_t));
		inheritVar.Exec(inheritReader, writeTo);
	}

	currentEntHash = entityhash;

	// In map entities this byte is 0; in entitydefs this is 1
	// Presumably this is expandInheritance
	assert(reader.ReadLE(bytecode));
	writeTo.append("expandInheritance = ");
	if (deserialmode == DeserialMode::entitydef) {
		assert(bytecode == 1);
		writeTo.append("true;\n");
	}
	else {
		assert(bytecode == 0);
		writeTo.append("false;\n");
	}


	// Block #1 - Padded Wrapper --> Serialization Hash --> Editor Vars
	{
		// Wrapper Block
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 1);
		assert(reader.ReadLE(length));
		assert(length == 0);
		assert(reader.ReadLE(length));

		if (length != 0) {
			// Edit Block
			assert(reader.ReadLE(bytecode));
			assert(bytecode == 0);
			assert(reader.ReadLE(editHash));
			assert(editHash == HASH_EDIT);

			const deserializer editorVars = {&ds_idEntityDefEditorVars, "editorVars", 0};
			editorVars.Exec(reader, writeTo);
		}
	}


	// Block #2 - Padded Wrapper --> gamesystemVariables_t (Entity class and some booleans)
	{
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 1);
		assert(reader.ReadLE(length));
		assert(length == 0);
		assert(reader.ReadLE(length));

		if (length != 0) {
			// Edit Block
			assert(reader.ReadLE(bytecode));
			assert(bytecode == 0);
			assert(reader.ReadLE(editHash));
			assert(editHash == HASH_EDIT);

			const deserializer systemVars = {&ds_idDeclEntityDef__gameSystemVariables_t, "systemVars", 0};
			systemVars.Exec(reader, writeTo);
		}
	}

	// If the class is not explicitly defined due to inheritance, look it up
	// Use the parent hash to make this work with both entitydefs and mapentities
	if (lastAccessedTypeInfo.callback == nullptr) {
		auto hashiter = entityclassmap.find(inherit.hash);
		assert(hashiter != entityclassmap.end());

		auto typeiter = typeInfoPtrMap.find(hashiter->second.typehash);
		assert(typeiter != typeInfoPtrMap.end());
		lastAccessedTypeInfo = typeiter->second;

		// Insert the class string into the text block
		// Must account for it being defined with the class missing,
		// or not defined at all
		if (length == 0) {
			writeTo.append("systemVars = {\nentityType = \"");
		}
		else {
			writeTo.pop_back();
			writeTo.pop_back(); // Pop the newline and close brace
			writeTo.append("entityType = \"");
		}
		writeTo.append(lastAccessedTypeInfo.name);
		writeTo.append("\";\n}\n");
	}
		

	fileCount++;
	// Block #3 - Unpadded Wrapper --> Serialization Hash --> Edit Block
	{
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 0);
		assert(reader.ReadLE(length));
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 1);
		assert(reader.ReadLE(length));
		assert(length == 0);
		assert(reader.ReadLE(length));

		if (length != 0) {
			// Edit Block
			assert(reader.ReadLE(bytecode));
			assert(bytecode == 0);
			assert(reader.ReadLE(editHash));
			assert(editHash == HASH_EDIT);

			const deserializer editBlock = {lastAccessedTypeInfo.callback, "edit", 0};
			editBlock.Exec(reader, writeTo);
		}
	}


	// Block #4 - Unserialized Edit Block
	{
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 0);
		assert(reader.ReadLE(length));

		const char* unserialized = nullptr;
		assert(reader.ReadBytes(unserialized, length));

		if (include_originals) {
			writeTo.append("original = {");
			writeTo.append(unserialized, length);
			writeTo.append("}\n");
		}
	}

	// Done
	assert(reader.ReachedEOF());
}

void ds_submapentity(BinaryReader& reader, std::string& writeTo)
{

}

void ds_submap(BinaryReader& reader, BinaryReader& shortmask, std::string& writeTo, std::string& StringTable, bool BuildStringTable)
{
	writeTo.append("submap {\n");
	uint8_t bytecode;
	uint32_t len;

	/* Start of submap chunk */
	assert(reader.ReadLE(len));
	assert(len == 0x0A);
	assert(reader.ReadLE(bytecode));
	assert(bytecode == 1);
	assert(reader.ReadLE(len));
	assert(len == 0);

	/* Compilation Metadata Strings */
	uint32_t numMetaProperties;
	assert(reader.ReadLE(numMetaProperties));
	if (numMetaProperties > 0) {
		std::string temptable = "metadata = {\n";
		
		// Each meta property consists of a key string, and a value string
		for (uint32_t i = 0; i < numMetaProperties; i++) {

			const char* bytes = nullptr;
			assert(reader.ReadLE(len));
			assert(reader.ReadBytes(bytes, len));

			temptable.append(bytes, len);
			temptable.append(" = \"");

			assert(reader.ReadLE(len));
			assert(reader.ReadBytes(bytes, len));
			temptable.append(bytes, len);
			temptable.append("\";\n");
		}

		/*
		* It's the same string table duplicated across every
		* submap chunk for some reason
		*/

		temptable.append("}\n");
		if (BuildStringTable) {
			StringTable = temptable;
		}
		else {
			assert(StringTable == temptable);
		}
	}

	/* End of String Chunk */
	assert(reader.ReadLE(len));
	assert(len == 0);

	/* 
	* This is written into the first 4 bytes of the world entity
	* So we must technically skip reading those first 4 bytes just for this entity
	*/
	uint32_t totalentities;
	uint32_t currententity = 0;
	assert(reader.ReadLE(totalentities));
	const char* debug_lastent = nullptr;
	goto LABEL_SKIP_FIRST_4_BYTES;

	
	while (currententity < totalentities) {

		assert(reader.ReadLE(len));
		assert(len == 0);

		LABEL_SKIP_FIRST_4_BYTES:
		writeTo.append("entity {\n");

		// Seems to correlate with a layer being defined.
		// Some sort of layer id? 
		// Layer Ids may vary by submap
		uint16_t shortmaskvalue;
		assert(shortmask.ReadLE(shortmaskvalue));
		if (shortmaskvalue != 0) {
			writeTo.append("layerID = ");
			writeTo.append(std::to_string(shortmaskvalue));
			writeTo.append(";\n");
		}

		uint32_t namelength = 0;
		const char* entname = nullptr;

		// Normally 0, but if this is one there's another string
		// Highly likely this is the layers information!!!
		assert(reader.ReadLE(len));
		if (len == 1) {
			assert(shortmaskvalue != 0);
			assert(reader.ReadLE(namelength));
			assert(reader.ReadBytes(entname, namelength));

			writeTo.append("layer = \"");
			writeTo.append(entname, namelength);
			writeTo.append("\";\n");
		}
		else {
			assert(shortmaskvalue == 0);
			assert(len == 0);
		}

		uint32_t instanceid;
		assert(reader.ReadLE(instanceid));
		if (instanceid != 0) {
			writeTo.append("instanceId = ");
			writeTo.append(std::to_string(instanceid));
			writeTo.append(";\n");
		}


		assert(reader.ReadLE(namelength));
		assert(reader.ReadBytes(entname, namelength));
		
		// If the instance id is zero the entity's "true" name
		// will follow it's original name
		if (instanceid != 0) {
			writeTo.append("originalName = \"");
			writeTo.append(entname, namelength);
			writeTo.append("\";\n");
			assert(reader.ReadLE(namelength));
			assert(reader.ReadBytes(entname, namelength));
		}

		writeTo.append("name = \"");
		writeTo.append(entname, namelength);
		writeTo.append("\";\n");

		debug_lastent = entname;
		//printf("%.*s\n", entname, namelength);

		assert(reader.ReadLE(len));
		BinaryReader entreader(reader.GetNext(), len);
		deserial::ds_start_entitydef(entreader, writeTo, -1);

		assert(reader.GoRight(len));

		writeTo.append("}\n");

		currententity++;
	}

	assert(reader.ReadLE(len));
	assert(len == 0);
	assert(reader.ReachedEOF());
	assert(shortmask.ReachedEOF());

	writeTo.append("}\n");
}

void deserial::ds_start_mapentities(BinaryReader& reader, std::string& writeTo)
{
	int totalmaps;
	assert(reader.ReadLE(totalmaps));
	assert(reader.Goto(0x8)); // Position of the entity count of the first submap

	struct submapheader_t {
		uint32_t entities = 0;
		uint32_t length = 0;

		BinaryReader mapreader;
		BinaryReader shortmask;
	};

	submapheader_t* submapheaders = new submapheader_t[totalmaps];

	// Extract the submap entity totals and block lengths from the header
	for (int i = 0; i < totalmaps; i++) {
		assert(reader.ReadLE(submapheaders[i].entities));
		assert(reader.GoRight(0xC));
		assert(reader.ReadLE(submapheaders[i].length));
		assert(reader.GoRight(0xC));
	}

	// Since we don't know how to calculate the length of the header chunk,
	// we must iterate backwards through the file to extract the submap chunks
	const char* tempptr = reader.GetBuffer() + reader.GetLength();
	for (int i = totalmaps - 1; i > -1; i--) {
		submapheader_t& sm = submapheaders[i];

		tempptr -= sm.length;
		sm.mapreader = BinaryReader(tempptr, sm.length);
		tempptr -= sm.entities * 2;
		sm.shortmask = BinaryReader(tempptr, sm.entities * 2);

	}

	std::string stringtable;


	for (int i = 0; i < totalmaps; i++) {
		ds_submap(submapheaders[i].mapreader, submapheaders[i].shortmask, writeTo, stringtable, i == 0);
	}

	writeTo.append(stringtable);

	delete[] submapheaders;
	//printf("%s", stringtable.c_str());
}

void deserial::ds_start_logicdecl(BinaryReader& reader, std::string& writeTo, ResourceType declclass) {
	#define HASH_EDIT 0xC2D0B77C0D10391CUL
	uint8_t bytecode;
	uint32_t length;
	uint64_t inherits;

	// File starts with a 0 byte
	assert(reader.ReadLE(bytecode));
	assert(bytecode == 0);

	// Length of file past this point
	assert(reader.ReadLE(length));
	assert(length == reader.GetRemaining());

	// Parent decl hash - should always be 0
	assert(reader.ReadLE(inherits));
	assert(inherits == 0);

	// Bytecode of 1, followed by word of padding
	assert(reader.ReadLE(bytecode));
	assert(bytecode == 1);
	assert(reader.ReadLE(length));
	assert(length == 0);

	// Length of deserialization block
	assert(reader.ReadLE(length));
	assert(length == reader.GetRemaining());

	// Edit Property Hash
	assert(reader.ReadLE(bytecode));
	assert(bytecode == 0);
	assert(reader.ReadLE(inherits));
	assert(inherits == HASH_EDIT);

	deserializer editblock = { nullptr, "edit", 0 };
	
	switch (declclass) {
		case rt_logicClass:
		editblock.callback = &ds_idDeclLogicClass;
		break;

		case rt_logicEntity:
		editblock.callback = &ds_idDeclLogicEntity;
		break;

		case rt_logicFX:
		editblock.callback = &ds_idDeclLogicFX;
		break;

		case rt_logicLibrary:
		editblock.callback = &ds_idDeclLogicLibrary;
		break;

		case rt_logicUIWidget:
		editblock.callback = &ds_idDeclLogicUIWidget;
		break;

		default:
		assert(0);
		break;
	}
	editblock.Exec(reader, writeTo);
}

dsfunc_m(deserial::ds_pointerbase)
{
	LogWarning("Called ds_pointerbase - deserialization algorithm unknown");
	deserial::ds_unsigned_long_long(reader, writeTo);
}

// TODO: Figure out what these hashes represent - unfortunately they're not a dependency hash
dsfunc_m(deserial::ds_pointerdeclinfo)
{
	//LogWarning("DeclInfo");
	deserial::ds_unsigned_long_long(reader, writeTo);
}

dsfunc_m(deserial::ds_pointerdecl)
{
	assert(*(reader.GetBuffer() - 5) == 1); // Leaf node
	assert(reader.GetLength() == 8);

	uint64_t hash;
	assert(reader.ReadLE(hash));

	if (hash == 0) {
		writeTo.append("NULL;\n");
		return;
	}

	const auto& iter = declHashMap.find(hash);

	if (iter != declHashMap.end()) {
		writeTo.push_back('"');
		writeTo.append(iter->second);
		writeTo.append("\";\n");
	}
	else {
		char hashString[17];
		snprintf(hashString, 17, "%I64X", hash);
		std::string msg = "Unknown Decl Hash ";
		msg.append(hashString, 16);
		LogWarning(msg);

		writeTo.append(std::to_string(hash));
		writeTo.append(";\n");
	}
	
}

dsfunc_m(deserial::ds_idTypeInfoPtr)
{
	assert(*(reader.GetBuffer() - 5) == 1); // Leaf node
	assert(reader.GetLength() == 4);

	uint32_t hash;
	reader.ReadLE(hash);
	if (hash == 0) {
		assert(0);
		//lastAccessedTypeInfo = {nullptr, nullptr};
		//writeTo.append("\"\";\n");
	}
	else {
		const auto& iter = typeInfoPtrMap.find(hash);
		assert(iter != typeInfoPtrMap.end());

		writeTo.push_back('"');
		writeTo.append(iter->second.name);
		writeTo.append("\";\n");

		lastAccessedTypeInfo = iter->second;
	}
}

dsfunc_m(deserial::ds_idTypeInfoObjectPtr)
{
	/*
	* Goal: Simplify the typeinfo syntax using the format variable_name "className" { <object_definition> }
	*/
	writeTo.pop_back(); // Remove the "= "
	writeTo.pop_back();

	assert(*(reader.GetBuffer() - 5) == 0);
	
	#define HASH_className 0x18986161CE41CA86UL
	#define HASH_object 0x0D83405E5171CB03UL

	uint8_t bytecode;
	uint32_t u32;
	uint64_t hash;

	assert(reader.ReadLE(bytecode));
	assert(bytecode == 0);
	assert(reader.ReadLE(hash));

	// Class is inherited from parent decl.
	// We must query the typeinfo history to determine what it is
	bool historyLookupRequired = false;
	if (hash != HASH_className) {
		historyLookupRequired = true;

		auto iter = typeinfoHistory.end();
		uint64_t historyhash = currentEntHash;

		std::string propstackstring;
		for (std::string_view s : propertyStack) {
			propstackstring.append(s);
			propstackstring.push_back('/');
		}

		while (iter == typeinfoHistory.end()) {
			entityclass_t classdef = entityclassmap[historyhash];
			assert(classdef.parent != 0);
			historyhash = classdef.parent;

			std::string historystring = propstackstring;
			historystring.append(std::to_string(historyhash));
			iter = typeinfoHistory.find(historystring);
		}

		lastAccessedTypeInfo = iter->second;
	}
	else {
		// Read the className typeinfo variable
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 1);
		assert(reader.ReadLE(u32));
		assert(u32 == 4);
		assert(reader.ReadLE(u32));
		
		auto iter = typeInfoPtrMap.find(u32);
		assert(iter != typeInfoPtrMap.end());

		lastAccessedTypeInfo = iter->second;

		// Add to the type history
		// Logic and map entities don't have inheritance and thus don't need to use
		// the history system
		if (deserialmode == DeserialMode::entitydef) {
			std::string propstackstring;
			for (std::string_view s : propertyStack) {
				propstackstring.append(s);
				propstackstring.push_back('/');
			}
			propstackstring.append(std::to_string(currentEntHash));
			typeinfoHistory.emplace(propstackstring, lastAccessedTypeInfo);
		}
	}

	writeTo.push_back('"');
	writeTo.append(lastAccessedTypeInfo.name);
	writeTo.append("\" "); // {\n will be added by structbase

	// If we read the object hash instead of the className hash...we have to skip the part where we
	// attempt to read the object hash
	if(historyLookupRequired)
		goto LABEL_SKIP_HASH_READ;
	if (reader.GetRemaining() > 0) {

		assert(reader.ReadLE(bytecode));
		assert(bytecode == 0);
		assert(reader.ReadLE(hash));

		LABEL_SKIP_HASH_READ:
		assert(hash == HASH_object);
		assert(reader.ReadLE(bytecode));

		// These *should* always be braced objects and never primitives.
		//assert(bytecode == 0 || bytecode == 1);
		assert(bytecode == 0);

		assert(reader.ReadLE(u32));


		BinaryReader objReader(reader.GetNext(), u32);
		assert(reader.GoRight(u32));
		lastAccessedTypeInfo.callback(objReader, writeTo);
	}
	else { // If the object property is not serialized, we must manually add braces
		writeTo.append("{\n}\n");
	}

	assert(reader.GetRemaining() == 0);
	//writeTo.append("}\n");
}

void deserial::ds_enumbase(BinaryReader& reader, std::string& writeTo, const dsenummap_t& enumMap)
{
	assert(*(reader.GetBuffer() - 5) == 1); // Leaf node

	size_t len = reader.GetLength();
	assert(len % 8 == 0);

	writeTo.push_back('"');
	while (len > 0) {
		uint64_t hash;
		assert(reader.ReadLE(hash));

		auto pair = enumMap.find(hash);
		if (pair != enumMap.end()) {
			writeTo.append(pair->second);
			writeTo.push_back(' ');
		}
		else {
			char hashString[9];
			snprintf(hashString, 9, "%I64X", hash);
			std::string msg = "Unknown Enum Hash ";
			msg.append(hashString, 8);
			LogWarning(msg);
		}

		len -= 8;
	}
	if(writeTo.back() == ' ')
		writeTo.pop_back();
	writeTo.append("\";\n");
}

void deserial::ds_structbase(BinaryReader& reader, std::string& writeTo, const dspropmap_t& propMap)
{
	// Stem node
	if (*(reader.GetBuffer() - 5) != 0) {
		//std::string debug(reader.GetBuffer(), reader.GetLength());
		//size_t length = reader.GetLength();
		//assert(length == 0);
		LogWarning("Structure is not a stem node!");
		writeTo.append("\"BAD STRUCT\";\n");
		return;
	}

	writeTo.append("{\n");
	while (reader.GetRemaining() > 0) {
		uint8_t bytecode;
		uint64_t hash;

		assert(reader.ReadLE(bytecode));
		assert(reader.ReadLE(hash));
		assert(bytecode == 0);

		auto iter = propMap.find(hash);
		if (iter != propMap.end()) {
			iter->second.Exec(reader, writeTo);
		}
		else {
			char hashString[9];
			snprintf(hashString, 9, "%I64X", hash);
			std::string msg = "Unknown Property Hash ";
			//msg.append(hashString, 8);
			msg.append(std::to_string(hash));
			LogWarning(msg);

			// Skip the property
			assert(reader.ReadLE(bytecode));
			assert(bytecode == 0 || bytecode == 1); 
			uint32_t len;
			assert(reader.ReadLE(len));
			assert(reader.GoRight(len));

			// DEBUGGING: Put the hash into the file
			writeTo.push_back('"');
			writeTo.append(std::to_string(hash));
			writeTo.append("\"\n");
		}
	}
	writeTo.append("}\n");
}

void deserial::ds_idList(BinaryReader& reader, std::string& writeTo, dsfunc_t* callback)
{
	writeTo.append("{\n");

	assert(*(reader.GetBuffer() - 5) == 0);
	
	#define HASH_NUM 0x1437944E8D38F7D9UL

	uint8_t bytecode;
	uint16_t shortlength;
	uint32_t length;
	uint64_t numHash;

	// Read the list length
	// Length isn't guaranteed to be serialized i.e. when partially modifying an inherited list
	if (*reader.GetNext() == '\0') {
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 0);
		assert(reader.ReadLE(numHash));
		assert(numHash == HASH_NUM);
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 1);
		assert(reader.ReadLE(length));
		assert(length == 2);
		assert(reader.ReadLE(shortlength));

		writeTo.append("num = ");
		writeTo.append(std::to_string(shortlength));
		writeTo.append(";\n");
	}


	// FORMAT of elements:
	// 4 Bytes:
	// - Always 1
	// - 2 Byte index
	// - 0 and 1 --> Denotes stem/leaf
	while (reader.GetRemaining() > 0) {
		
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 1);
		assert(reader.ReadLE(shortlength));

		// At this point we're basically replicating
		// a deserializer's exec function

		std::string propName = "item[";
		propName.append(std::to_string(shortlength));
		propName.push_back(']');
		writeTo.append(propName);
		writeTo.append(" = ");
		propertyStack.emplace_back(propName);

		assert(reader.ReadLE(bytecode));
		assert(bytecode == 0 || bytecode == 1);
		assert(reader.ReadLE(length));

		BinaryReader propReader(reader.GetNext(), length);
		assert(reader.GoRight(length));

		callback(propReader, writeTo);

		propertyStack.pop_back();
	}
	writeTo.append("}\n");
}

void deserial::ds_staticList(BinaryReader& reader, std::string& writeTo, deserializer basetype)
{
	std::string_view debugging(reader.GetBuffer(), reader.GetLength());

	// Apparently this can happen....
	if (*(reader.GetBuffer() - 5) == 1) {
		assert(reader.GetLength() == 0);
		writeTo.append("{\n}\n");
		return;
	}

	// Static lists are serialized the exact same way as idLists. They just don't have a length property
	// and elements use the property's original name
	// Todo: Possibly find a way to consolidate code across list/staticlist/deserializer functions? But probably not going to happen
	assert(*(reader.GetBuffer() - 5) == 0);

	uint8_t bytecode;
	uint16_t index;
	uint32_t length;

	writeTo.append("{\n");
	while (reader.GetRemaining() > 0) {
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 1);
		assert(reader.ReadLE(index));

		// At this point we're basically replicating
		// a deserializer's exec function

		std::string propName = basetype.name;
		propName.push_back('[');
		propName.append(std::to_string(index));
		propName.push_back(']');
		writeTo.append(propName);
		writeTo.append(" = ");
		propertyStack.emplace_back(propName);

		assert(reader.ReadLE(bytecode));
		assert(bytecode == 0 || bytecode == 1);
		assert(reader.ReadLE(length));

		BinaryReader propReader(reader.GetNext(), length);
		assert(reader.GoRight(length));

		basetype.callback(propReader, writeTo);

		propertyStack.pop_back();
	}
	writeTo.append("}\n");
}

void deserial::ds_idListMap(BinaryReader& reader, std::string& writeTo, dsfunc_t* keyfunc, dsfunc_t* valuefunc)
{
	//LogWarning("idListMap");
	assert(*(reader.GetBuffer() - 5) == 0);
	writeTo.append("{\n");

	uint8_t bytecode;
	uint16_t shortcode;
	uint32_t length;

	int currentElement = 0;
	while (reader.GetRemaining() > 0) {


		// TODO: Monitor to ensure this is always 3
		assert(reader.ReadLE(shortcode));
		assert(shortcode == 3);

		/*
		* Deserialize the pair's Key
		*/
		std::string propName = "key[";
		propName.append(std::to_string(currentElement));
		propName.push_back(']');
		propertyStack.emplace_back(propName);

		assert(reader.ReadLE(bytecode));
		assert(bytecode == 0 || bytecode == 1);
		assert(reader.ReadLE(length));

		BinaryReader propReader(reader.GetNext(), length);
		assert(reader.GoRight(length));
		keyfunc(propReader, writeTo);
		propertyStack.pop_back();

		// Workaround to remove semicolon and newline normally placed after value is deserialized
		writeTo.pop_back();
		writeTo.pop_back();

		/*
		* Deserialize the pair's Value
		*/

		writeTo.append(" = ");

		propName = "value[";
		propName.append(std::to_string(currentElement));
		propName.push_back(']');
		propertyStack.emplace_back(propName);


		assert(reader.ReadLE(bytecode));
		assert(bytecode == 0 || bytecode == 1);
		assert(reader.ReadLE(length));

		propReader = BinaryReader(reader.GetNext(), length);
		assert(reader.GoRight(length));
		valuefunc(propReader, writeTo);
		propertyStack.pop_back();

		currentElement++;
	}
	writeTo.append("}\n");
}

dsfunc_m(deserial::ds_idStr)
{
	assert(*(reader.GetBuffer() - 5) == 1);

	uint8_t terminal;
	uint32_t stringLength;
	assert(reader.ReadLE(stringLength));

	if (stringLength == 0) {
		writeTo.append("\"\";\n");
	}
	else {
		const char* bytes = nullptr;
		assert(reader.ReadBytes(bytes, stringLength));

		/*
		* ANY Deserialized file can have multi-line strings
		*/
		bool iscomplexstring = false;
		for (const char* c = bytes, *max = bytes + stringLength; c < max; c++) {
			if (*c == '\n' || *c == '"') {
				iscomplexstring = true;
				break;
			}
		}

		if (iscomplexstring) {
			writeTo.append("<%");
			writeTo.append(bytes, stringLength);
			writeTo.append("%>;\n");
		}
		else {
			writeTo.push_back('"');
			writeTo.append(bytes, stringLength);
			writeTo.append("\";\n");
		}

		assert(reader.ReadLE(terminal));
		assert(terminal == 0);
	}

	assert(reader.GetRemaining() == 0);

}

dsfunc_m(deserial::ds_attachParent_t)
{

	// Attach Parent Format is [Leaf = 1][uint16_t shortCode][cstring]

	assert(*(reader.GetBuffer() - 5) == 1);

	uint16_t parentType;
	assert(reader.ReadLE(parentType));
	
	writeTo.push_back('"');
	switch (parentType) 
	{
		case 0:
		writeTo.append("none:");
		break;

		case 1:
		writeTo.append("joint:");
		break;

		case 2:
		writeTo.append("tag:");
		break;

		case 3:
		writeTo.append("slot:");
		break;

		default:
		assert(0);
		break;
	}

	uint8_t nullbyte;
	uint32_t stringLength;
	const char* rawString;

	assert(reader.ReadLE(stringLength));
	assert(reader.ReadBytes(rawString, stringLength));
	if (stringLength > 0) {
		assert(reader.ReadLE(nullbyte));
		assert(nullbyte == 0);
	}
	writeTo.append(rawString, stringLength);
	writeTo.append("\";\n");
	assert(reader.GetRemaining() == 0);
}

dsfunc_m(deserial::ds_idRenderModelWeakHandle)
{
	// TODO: Monitor this. There is absolutely no way of knowing how these are supposed to be serialized
	// because their block lengths are 0 in every logic decl
	assert(*(reader.GetBuffer() - 5) == 1);
	assert(reader.GetLength() == 0);
	writeTo.append("\"\";\n");
}

dsfunc_m(deserial::ds_idLogicProperties)
{
	assert(*(reader.GetBuffer() - 5) == 0); // Stem node
	writeTo.append("{\n");

	uint8_t bytecode;
	uint64_t hash;

	/*
	* The format is basically: 
	* - Property hashes are 4 byte logicProperty_t ID values, with upper word = 0
	* - Properties are logicProperty_t structs
	* - No length variables given
	*/
	while (reader.GetRemaining() > 0) {
		assert(reader.ReadLE(bytecode));
		assert(bytecode == 0);
		reader.ReadLE(hash);

		std::string hashString = "\"";
		hashString.append(std::to_string(hash));
		hashString.push_back('"');

		deserializer ds = {&ds_logicProperty_t, hashString.c_str()};
		ds.Exec(reader, writeTo);
	}

	writeTo.append("}\n");
}

/*
* This structure is undefined in the idlib, and is presumably a polymorphic decl variable
* We assume the following:
* - The property hash matches the camel-cased folder path
*/
dsfunc_m(ds_idEventArgDeclPtr)
{
	if (*(reader.GetBuffer() - 5) == 1) {
		assert(reader.GetLength() == 0);
		writeTo.append("NULL;\n");
		return;
	}

	// Also not farm hashes
	const dspropmap_t propMap = {
		{15128935135463038980, {&deserial::ds_pointerdecl, "soundstate"}},
		{17887799704904324637, {&deserial::ds_pointerdecl, "rumble"}},
		{17887800778963345949, {&deserial::ds_pointerdecl, "string"}},
		{17887784525484514587, {&deserial::ds_pointerdecl, "damage"}},
		{15128935076300951565, {&deserial::ds_pointerdecl, "soundevent"}},
		{8150212324453652044, {&deserial::ds_pointerdecl, "gorewounds"}},
	};
	deserial::ds_structbase(reader, writeTo, propMap);
}

dsfunc_m(deserial::ds_idEventArg)
{
	// These are not farmhashes - these are the class name hashes
	const dspropmap_t propMap = {
		{1182887132, {&ds_eEncounterSpawnType_t, "eEncounterSpawnType_t"}},
		{11024699549390617459, {&ds_bool, "bool"}},
		{10770807278281925633, {&ds_long_long, "int"}}, // It's actually 8 bytes..
		{4511345809429878981, {&ds_idStr, "string"}},
		{6144605143588414986, {&ds_idStr, "entity"}},
		{11643015150811461308, {&ds_float, "float"}},
		{2111299206, {&ds_idCombatStates_t, "idCombatStates_t"}},
		{18446744073207127831, {&ds_encounterGroupRole_t, "encounterGroupRole_t"}},
		{614444004, {&ds_encounterLogicOperator_t, "encounterLogicOperator_t"}},
		{18446744072262373936, {&ds_eEncounterEventFlags_t, "eEncounterEventFlags_t"}},
		{18446744072526653912, {&ds_idEmpoweredAIType_t, "idEmpoweredAIType_t"}},
		{1323721246, {&ds_fxCondition_t, "fxCondition_t"}},
		{402298019, {&ds_socialEmotion_t, "socialEmotion_t"}},
		{18446744073561678798, {&ds_damageCategoryMask_t, "damageCategoryMask_t"}},
		{853223886056435927, {&ds_idEventArgDeclPtr, "decl"}},
	};
	ds_structbase(reader, writeTo, propMap);
}

dsfunc_m(deserial::ds_bool)
{
	assert(*(reader.GetBuffer() - 5) == 1);
	assert(reader.GetLength() == 1);

	uint8_t val;
	reader.ReadLE(val);

	writeTo.append(val > 0 ? "true;\n" : "false;\n");
}

template<typename T>
__forceinline void ds_num(BinaryReader& reader, std::string& writeTo)
{
	std::string_view data(reader.GetBuffer(), reader.GetLength());

	assert(*(reader.GetBuffer() - 5) == 1); // Stem and leaf
	assert(reader.GetLength() == sizeof(T));

	T val;
	assert(reader.ReadLE(val));

	writeTo.append(std::to_string(val)); // Should be promoted to integer when it's a character
	writeTo.append(";\n");
}

dsfunc_m(deserial::ds_char)
{
	ds_num<int8_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_unsigned_char)
{
	ds_num<uint8_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_wchar_t)
{
	ds_num<uint16_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_short)
{
	ds_num<int16_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_unsigned_short)
{
	ds_num<uint16_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_int)
{
	ds_num<int32_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_unsigned_int)
{
	ds_num<uint32_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_long)
{
	ds_num<int32_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_long_long)
{
	ds_num<int64_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_unsigned_long)
{
	ds_num<uint32_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_unsigned_long_long)
{
	ds_num<uint64_t>(reader, writeTo);
}

dsfunc_m(deserial::ds_float)
{
	ds_num<float>(reader, writeTo);
}

dsfunc_m(deserial::ds_double)
{
	ds_num<double>(reader, writeTo);
}
