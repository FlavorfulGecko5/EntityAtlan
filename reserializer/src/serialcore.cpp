#include "entityslayer/EntityNode.h"
#include "io/BinaryWriter.h"
#include "serialcore.h"
#include "hash/HashLib.h"
#include "generated/reserialgenerated.h"

#define rsfunc_m(NAME) void NAME(const EntNode& property, BinaryWriter& writer)

thread_local std::vector<std::string_view> propertyStack;
thread_local reserialTypeInfo lastAccessedTypeInfo;
thread_local int warningCount = 0;

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

void LogWarning(std::string_view msg) {
	std::string propString;
	propString.reserve(200);
	for (std::string_view s : propertyStack) {
		propString.append(s);
		propString.push_back('/');
	}

	if (!propString.empty())
		propString.pop_back();

	printf("WARNING: %.*s %.*s\n", (int)propString.length(), propString.data(), (int)msg.length(), msg.data());
	warningCount++;
}

uint64_t DeclToFarmhash(const char* buffer, const char* max) {
	const char* delimiter = buffer;
	while (delimiter < max) {
		if (*delimiter == '/')
			break;
		delimiter++;
	}

	if (*delimiter != '/') {
		LogWarning("Could not find decltype delimiter");
		return 0;
	}

	uint64_t farmhash = HashLib::DeclHash(
		std::string_view(buffer, delimiter - buffer),
		std::string_view(delimiter + 1, max - delimiter - 1)
	);
	return farmhash;
}

void reserializer::Exec(const EntNode& property, BinaryWriter& writer) const
{
	propertyStack.emplace_back(property.getName());

	writer << static_cast<uint8_t>(0) << farmhash;

	if (arrayLength > 0) {
		reserial::rs_staticList(property, writer, *this);
	}
	else {
		callback(property, writer);
	}

	propertyStack.pop_back();
	
}

void reserial::rs_start_entitydef(const EntNode& root, BinaryWriter& writer)
{
	#define HASH_EDIT 0xC2D0B77C0D10391CUL
	writer << static_cast<uint8_t>(0);
	writer.pushSizeStack();

	// Inherit Hash
	const EntNode& inheritnode = root["inherit"];
	if(&inheritnode != EntNode::SEARCH_404)
	{
		std::string_view data = inheritnode.getValueUQ();
		writer << DeclToFarmhash(data.data(), data.data() + data.length());
	}
	else {
		writer << static_cast<uint64_t>(0);
	}

	// expandInheritance bool
	{
		const EntNode& expandinheritance_node = root["expandinheritance"];
		bool expandinheritance = true;
		expandinheritance_node.ValueBool(expandinheritance);
		writer << expandinheritance;
	}

	// Block #1: Editor Vars
	{
		writer << static_cast<uint8_t>(1) << static_cast<uint32_t>(0);
		
		EntNode& editorvarsnode = root["editorVars"];
		if (&editorvarsnode != EntNode::SEARCH_404) {
			writer.pushSizeStack();
			const reserializer editorVars = {&rs_idEntityDefEditorVars, HASH_EDIT};
			editorVars.Exec(editorvarsnode, writer);
			writer.popSizeStack();
		}
		else {
			writer << static_cast<uint32_t>(0);
		}
	}

	// Block #2: System Vars
	{
		writer << static_cast<uint8_t>(1) << static_cast<uint32_t>(0);

		const EntNode& systemvars_node = root["systemVars"];
		
		if (&systemvars_node != EntNode::SEARCH_404) {
			writer.pushSizeStack();
			const reserializer systemvars = {&rs_idDeclEntityDef__gameSystemVariables_t, HASH_EDIT};
			systemvars.Exec(systemvars_node, writer);
			writer.popSizeStack();
		}
		else {
			writer << static_cast<uint32_t>(0);
		}
	}

	// TODO: PROPERLY TERMINATE THE ENTITY
	if (lastAccessedTypeInfo.callback == nullptr) {
		LogWarning("[FATAL]: Cannot reserialize due to missing entity class");
		writer.popSizeStack();
		return;
	}

	// Block #3: Edit Block
	{
		writer << static_cast<uint8_t>(0);
		writer.pushSizeStack();
		writer << static_cast<uint8_t>(1) << static_cast<uint32_t>(0);

		const EntNode& edit_node = root["edit"];
		if (&edit_node != EntNode::SEARCH_404) {
			writer.pushSizeStack();
			const reserializer entityfunc = {lastAccessedTypeInfo.callback, HASH_EDIT};
			entityfunc.Exec(edit_node, writer);
			writer.popSizeStack();
		}
		else {
			writer << static_cast<uint32_t>(0);
		}
		writer.popSizeStack();
	}

	lastAccessedTypeInfo = {nullptr, 0};


	// Block #4: Unserialized Edit Block...we're not filling this in
	{
		writer << static_cast<uint8_t>(0) << static_cast<uint32_t>(0);
	}

	writer.popSizeStack();
}

void reserial::rs_pointerbase(const EntNode& property, BinaryWriter& writer)
{
	reserial::rs_unsigned_long_long(property, writer);
}

void reserial::rs_pointerdeclinfo(const EntNode& property, BinaryWriter& writer)
{
	reserial::rs_unsigned_long_long(property, writer);
}

void reserial::rs_pointerdecl(const EntNode& property, BinaryWriter& writer)
{
	writer << static_cast<uint8_t>(1) << static_cast<uint32_t>(8);

	std::string_view data = property.getValueUQ();
	if (data.empty() || data == "NULL") {
		writer << static_cast<uint64_t>(0);
		return;
	}

	const char* buffer = data.data();
	size_t len = data.length();
	const char* max = buffer + len;

	if (*buffer >= '0' && *buffer <= '9') {
		uint64_t farmhash;
		ParseWholeNumber(buffer, static_cast<int>(len), farmhash);
		writer << farmhash;
		return;
	}

	const char* delimiter = buffer;
	while (delimiter < max) {
		if(*delimiter == '/')
			break;
		delimiter++;
	}

	if (*delimiter != '/') {
		LogWarning("Could not find decltype delimiter");
		writer << static_cast<uint64_t>(0);
		return;
	}

	uint64_t farmhash = HashLib::DeclHash(
		std::string_view(buffer, delimiter - buffer), 
		std::string_view(delimiter + 1, max - delimiter - 1)
	);

	writer << farmhash;
}

void reserial::rs_idTypeInfoPtr(const EntNode& property, BinaryWriter& writer)
{
	writer << static_cast<uint8_t>(1) << static_cast<uint32_t>(4);

	std::string_view classname = property.getValueUQ();
	uint64_t farmhash = HashLib::FarmHash64(classname.data(), classname.length());

	const auto& iter = typeInfoPtrMap.find(farmhash);

	if (iter == typeInfoPtrMap.end()) {
		std::string msg = "Unknown class name ";
		msg.append(classname);
		LogWarning(msg);

		writer << static_cast<uint32_t>(0);
		lastAccessedTypeInfo = {nullptr, 0};
	}
	else {
		writer << iter->second.typeinfohash;
		lastAccessedTypeInfo = iter->second;
	}
}

void reserial::rs_idTypeInfoObjectPtr(const EntNode& property, BinaryWriter& writer)
{
	writer << static_cast<uint8_t>(0);
	writer.pushSizeStack();

	// Due to the way we're calling reserializer exec functions,
	// the property node will have it's name pushed multiple times in a row
	// Pop the first instance so the warning messages don't duplicate it
	propertyStack.pop_back();

	/*
	* Serialize the className, which is compactly stored as the object's value string
	*/

	const reserializer className = {&rs_idTypeInfoPtr, 0x18986161CE41CA86UL, 0};
	className.Exec(property, writer);


	/*
	* Serialize the actual objects
	*/
	if (lastAccessedTypeInfo.callback != nullptr) {
		const reserializer object {lastAccessedTypeInfo.callback, 0x0D83405E5171CB03UL, 0};
		object.Exec(property, writer);
	}

	// Re-push the name we popped at the beginning of the function
	propertyStack.push_back(property.getName());
	writer.popSizeStack();
}

void reserial::rs_enumbase(const EntNode& property, BinaryWriter& writer, const rsenumset_t& enumset)
{
	if(property.getFlags() & EntNode::NF_Braces)
		LogWarning("Enum property is an object node");

	writer << static_cast<uint8_t>(1); // Leaf node
	writer.pushSizeStack();

	std::string_view data = property.getValueUQ();
	const char* buffer = data.data();
	const char* max = buffer + data.length();

	while (buffer < max) {
		if (*buffer == ' ') { // Skip leading whitespace
			buffer++;
			continue;
		}

		const char* first = buffer++;
		while (buffer < max) {
			if (*buffer == ' ') {
				break;
			}
			buffer++;
		}
		uint64_t farmhash = HashLib::FarmHash64(first, static_cast<size_t>(buffer - first));

		if (enumset.count(farmhash) == 0) {
			std::string msg = "Unknown Enum Value ";
			msg.append(std::string_view(first, buffer - first));
			LogWarning(msg);
		}

		writer << farmhash;

		buffer++; // Increment past the delimiter character
	}

	writer.popSizeStack();
}

void reserial::rs_structbase(const EntNode& property, BinaryWriter& writer, const rspropmap_t& propmap)
{
	if ((property.getFlags() & EntNode::NF_Braces) == 0) {
		LogWarning("Structure is not a stem node!");
	}

	writer << static_cast<uint8_t>(0); // Stem node
	writer.pushSizeStack();

	EntNode** buffer = property.getChildBuffer();
	EntNode** max = buffer + property.getChildCount();

	while (buffer < max) {
		const EntNode* e = *buffer;

		uint64_t farmhash = HashLib::FarmHash64(e->NamePtr(), e->NameLength());

		const auto& iter = propmap.find(farmhash);
		if (iter != propmap.end()) {
			iter->second.Exec(*e, writer);
		}
		else {
			std::string msg = "Unknown Property Name ";
			msg.append(property.getName());
			LogWarning(msg);
		}
		buffer++;
	}

	writer.popSizeStack();
}


void reserial::rs_idList(const EntNode& property, BinaryWriter& writer, rsfunc_t* callback)
{
	if ((property.getFlags() & EntNode::NF_Braces) == 0) {
		LogWarning("idList is not a stem node!");
	}

	writer << static_cast<uint8_t>(0);
	writer.pushSizeStack();

	EntNode** buffer = property.getChildBuffer();
	EntNode** max = buffer + property.getChildCount();

	// For simplicity, we assume num is always the first property in the list
	if (buffer < max && (*buffer)->getName() == "num") {
		const reserializer numprop = {&rs_unsigned_short, 0x1437944E8D38F7D9UL, 0}; // Farmhash is for "num"
		numprop.Exec(**buffer, writer);
		buffer++;
	}

	while (buffer < max) {
		const EntNode* e = *buffer;

		propertyStack.push_back(e->getName());

		// Get the index of this list element
		uint16_t index = -1;
		{
			const char* name = e->NamePtr();
			int namelength = e->NameLength();
			const char* last = name + namelength - 1;

			if (*last != ']') {
				LogWarning("idList property has no index!");
				buffer++;
				continue;
			}

			// We can be somewhat lax on the syntax checks here, since the
			// EntityParser will have picked up on any errors
			const char* first = last;
			while (first > name) // Assume at least one characer before the open bracket
			{
				if (*first == '[') {
					break;
				}
				first--;
			}
			ParseWholeNumber(first + 1, static_cast<int>(last - first - 1), index);

			// TODO: Should verify that the name string == item but this
			// isn't strictly necessary
		}

		writer << static_cast<uint8_t>(1) << index;
		callback(*e, writer);
		propertyStack.pop_back();
		buffer++;
	}

	writer.popSizeStack();
}

void reserial::rs_staticList(const EntNode& property, BinaryWriter& writer, reserializer basetype)
{
	if ((property.getFlags() & EntNode::NF_Braces) == 0) {
		LogWarning("Static Array is not a stem node!");
	}

	if (property.getChildCount() == 0) {
		writer << static_cast<uint8_t>(1) << static_cast<uint32_t>(0);
		return;
	}

	writer << static_cast<uint8_t>(0);
	writer.pushSizeStack();

	EntNode** buffer = property.getChildBuffer();
	EntNode** max = buffer + property.getChildCount();
	while (buffer < max) {
		const EntNode* e = *buffer;

		propertyStack.push_back(e->getName());

		// Get the index of this list element
		uint16_t index = -1;
		{
			const char* name = e->NamePtr();
			int namelength = e->NameLength();
			const char* last = name + namelength - 1;

			if (*last != ']') {
				LogWarning("Static array property has no index!");
				buffer++;
				continue;
			}

			// We can be somewhat lax on the syntax checks here, since the
			// EntityParser will have picked up on any errors
			const char* first = last;
			while (first > name) // Assume at least one characer before the open bracket
			{
				if (*first == '[') {
					break;
				}
				first--;
			}
			ParseWholeNumber(first + 1, static_cast<int>(last - first - 1), index);

			if (index >= basetype.arrayLength) {
				LogWarning("Static array index out of bounds!");
				buffer++;
				continue;
			}

			// TODO: Should verify that the name string's farmhash matches what's in the reserializer
			// but it's not strictly necessary
		}

		writer << static_cast<uint8_t>(1) << index;
		basetype.callback(*e, writer);
		propertyStack.pop_back();
		buffer++;
	}

	writer.popSizeStack();
}

// TODO: Will need to monitor list maps when this all goes live.
// Unclear whether keys *have* to be correctly sorted or not
void reserial::rs_idListMap(const EntNode& property, BinaryWriter& writer, rsfunc_t* keyfunc, rsfunc_t* valuefunc)
{
	if ((property.getFlags() & EntNode::NF_Braces) == 0) {
		LogWarning("Static Array is not a stem node!");
	}

	writer << static_cast<uint8_t>(0);
	writer.pushSizeStack();

	EntNode** buffer = property.getChildBuffer();
	EntNode** max = buffer + property.getChildCount();
	while (buffer < max)
	{
		writer << static_cast<uint16_t>(3);
		const EntNode* e = *buffer;
		propertyStack.push_back(e->getName());

		// Serialize the key
		propertyStack.push_back("KEY");
		const EntNode keynode = e->ListMapHack();
		keyfunc(keynode, writer);
		propertyStack.pop_back();

		// Serialize the value
		valuefunc(*e, writer);
		propertyStack.pop_back();
		buffer++;
	}
	writer.popSizeStack();
}

void reserial::rs_idStr(const EntNode& property, BinaryWriter& writeTo)
{
	// Debug warnings
	{
		if (property.getFlags() & EntNode::NF_Braces)
			LogWarning("String property is an object");

		const char* vptr = property.ValuePtr();
		if (*vptr != '"' && *vptr != '<')
			LogWarning("String property not quoted");
	}

	std::string_view value = property.getValueUQ();
	writeTo << static_cast<uint8_t>(1);
	if (value.length() == 0) {
		writeTo << static_cast<uint64_t>(4); // Block length of 4 + string length of 0
	}
	else {
		writeTo << static_cast<uint32_t>(value.length() + 5); // Block Length = string length + string length field + null char
		writeTo << static_cast<uint32_t>(value.length()); // String length;
		writeTo.WriteBytes(value.data(), value.length());
		writeTo << static_cast<uint8_t>('\0'); // Null terminate if string isn't empty
	}	
}

void reserial::rs_attachParent_t(const EntNode& property, BinaryWriter& writer)
{
	writer << static_cast<uint8_t>(1);
	writer.pushSizeStack();

	std::string_view data = property.getValueUQ();
	const char* buffer = data.data();
	const char* max = buffer + data.length();
	const char* delimiter = buffer;

	while (delimiter < max) {
		if(*delimiter == ':')
			break;
		delimiter++;
	}

	if (*delimiter != ':') {
		LogWarning("Could not find colon delimiter for attachParent");
	}
	else {
		uint16_t parenttype = 0;
		std::string_view typeview(buffer, delimiter - buffer);

		if(typeview == "none")
			parenttype = 0;
		else if(typeview == "joint")
			parenttype = 1;
		else if(typeview == "tag")
			parenttype = 2;
		else if(typeview == "slot")
			parenttype = 3;
		else 
			LogWarning("Unknown attachParent Type");

		writer << parenttype;
		
		std::string_view stringview(delimiter + 1, max - delimiter - 1);

		// Modified from rs_idStr
		if (stringview.length() == 0) {
			writer << static_cast<uint32_t>(0); // String length of 0
		}
		else {
			writer << static_cast<uint32_t>(stringview.length()); // String length;
			writer.WriteBytes(stringview.data(), stringview.length());
			writer << static_cast<uint8_t>('\0'); // Null terminate if string isn't empty
		}
	}
	
	writer.popSizeStack();
}

void reserial::rs_idRenderModelWeakHandle(const EntNode& property, BinaryWriter& writer)
{
	// TODO: If we ever get a non-zero block size for one of these, rewrite this
	writer << static_cast<uint8_t>(1) << static_cast<uint32_t>(0);
}

void reserial::rs_idLogicProperties(const EntNode& property, BinaryWriter& writer)
{
	writer << static_cast<uint8_t>(0);
	writer.pushSizeStack();


	// See corresponding method in deserialcore for explanation of format
	EntNode** buffer = property.getChildBuffer();
	EntNode** max = buffer + property.getChildCount();
	while (buffer < max)
	{
		const EntNode* e = *buffer;
		propertyStack.push_back(e->getName());

		std::string_view idstring = e->getNameUQ();
		uint32_t id;
		ParseWholeNumber(idstring.data(), static_cast<int>(idstring.length()), id);
		writer << static_cast<uint8_t>(0) << id << static_cast<uint32_t>(0);
		reserial::rs_logicProperty_t(*e, writer);

		propertyStack.pop_back();
		buffer++;
	}
	writer.popSizeStack();
}

void rs_idEventArgDeclPtr(const EntNode& property, BinaryWriter& writer)
{
	if (property.getValue() == "NULL") {
		writer << static_cast<uint8_t>(1) << static_cast<uint32_t>(0);
		return;
	}

	// Have to manually compute the key hashes
	const rspropmap_t propmap = {
		{1848598508625020864,  {&reserial::rs_pointerdecl, 15128935135463038980}}, // soundstate
		{13243819652675730551, {&reserial::rs_pointerdecl, 17887799704904324637}}, // rumble
		{4511345809429878981,  {&reserial::rs_pointerdecl, 17887800778963345949}}, // string
		{3786530527054513753,  {&reserial::rs_pointerdecl, 17887784525484514587}}, // damage
		{3660094820350849066,  {&reserial::rs_pointerdecl, 15128935076300951565}}, // soundevent
		{188873101814212032,   {&reserial::rs_pointerdecl, 8150212324453652044}},  // gorewounds
	};

	reserial::rs_structbase(property, writer, propmap);
}

void reserial::rs_idEventArg(const EntNode& property, BinaryWriter& writer)
{
	// Also have to manually compute the key hashes
	const rspropmap_t propmap = {
		{202368896358392332,   {&rs_eEncounterSpawnType_t, 1182887132}},
		{11024699549390617459, {&rs_bool, 11024699549390617459}}, // The farmhash is actually used for bool?
		{10770807278281925633, {&rs_long_long, 10770807278281925633}},
		{4511345809429878981,  {&rs_idStr, 4511345809429878981}}, // string
		{6144605143588414986,  {&rs_idStr, 6144605143588414986}}, // entity
		{11643015150811461308, {&rs_float, 11643015150811461308}},
		{872288027194033085,   {&rs_idCombatStates_t, 2111299206}},
		{6794667074012475943,  {&rs_encounterGroupRole_t, 18446744073207127831}},
		{17678348661440869271, {&rs_encounterLogicOperator_t, 614444004}},
		{14612131600335597994, {&rs_eEncounterEventFlags_t, 18446744072262373936}},
		{4369030352838381010,  {&rs_idEmpoweredAIType_t, 18446744072526653912}},
		{16523631401782497127, {&rs_fxCondition_t, 1323721246}},
		{18357943648798624979, {&rs_socialEmotion_t, 402298019}},
		{4091884542181036492,  {&rs_damageCategoryMask_t, 18446744073561678798}},
		{853223886056435927,   {&rs_idEventArgDeclPtr, 853223886056435927}},
	};
	rs_structbase(property, writer, propmap);
}

void reserial::rs_bool(const EntNode& property, BinaryWriter& writer)
{
	bool val = false;

	if (property.getFlags() & EntNode::NF_Braces) {
		LogWarning("Boolean property is an object node");
	}
	
	if (!property.ValueBool(val)) {
		LogWarning("Can't parse value as Boolean.");
	}

	writer << static_cast<uint8_t>(1) << static_cast<uint32_t>(1) << static_cast<uint8_t>(val);
}

template<typename T>
__forceinline void rs_wholenumber(const EntNode& property, BinaryWriter& writeTo) {
	T val = 0;

	if (property.getFlags() & EntNode::NF_Braces) {
		LogWarning("Numerical property is an object node");
	}

	if (!ParseWholeNumber(property.ValuePtr(), property.ValueLength(), val)) {
		LogWarning("Can't parse value as number");
	}

	writeTo << static_cast<uint8_t>(1) << static_cast<uint32_t>(sizeof(T)) << val;
}

void reserial::rs_char(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<int8_t>(property, writeTo);
}

void reserial::rs_unsigned_char(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<uint8_t>(property, writeTo);
}

void reserial::rs_wchar_t(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<uint16_t>(property, writeTo);
}

void reserial::rs_short(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<int16_t>(property, writeTo);
}

void reserial::rs_unsigned_short(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<uint16_t>(property, writeTo);
}

void reserial::rs_int(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<int32_t>(property, writeTo);
}

void reserial::rs_unsigned_int(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<uint32_t>(property, writeTo);
}

void reserial::rs_long(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<int32_t>(property, writeTo);
}

void reserial::rs_long_long(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<int64_t>(property, writeTo);
}

void reserial::rs_unsigned_long(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<uint32_t>(property, writeTo);
}

void reserial::rs_unsigned_long_long(const EntNode& property, BinaryWriter& writeTo)
{
	rs_wholenumber<uint64_t>(property, writeTo);
}

void reserial::rs_float(const EntNode& property, BinaryWriter& writeTo)
{
	float val = 0.0f;
	const std::string data(property.getValue());

	if (property.getFlags() & EntNode::NF_Braces) {
		LogWarning("Float property is an object node");
	}

	try {
		val = std::stof(data);
	}
	catch (...) {
		LogWarning("Can't parse number");
	}

	writeTo << static_cast<uint8_t>(1) << static_cast<uint32_t>(sizeof(float)) << val;
}

void reserial::rs_double(const EntNode& property, BinaryWriter& writeTo)
{
	double val = 0.0;
	const std::string data(property.getValue());

	if (property.getFlags() & EntNode::NF_Braces) {
		LogWarning("Double property is an object node");
	}
	
	try {
		val = std::stod(data);
	}
	catch (...) {
		LogWarning("Can't parse number");
	}

	writeTo << static_cast<uint8_t>(1) << static_cast<uint32_t>(sizeof(double)) << val;
}
