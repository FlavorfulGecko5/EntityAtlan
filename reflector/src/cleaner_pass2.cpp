#include "cleaner.h"
#include "entityslayer/EntityParser.h"
#include <unordered_map>
#include <string>
#include <cassert>
#include <set>

/* Note: forced inclusions, exclusions, and aliases will likely be controlled in more places besides the following lists*/

// Forcibly Exclude these types because we're manually implementing their functions.
// Must use cleaned type name
const std::set<std::string> ForcedExclusions = {
	"bool",
	"char",
	"unsigned_char",
	"wchar_t",
	"short",
	"unsigned_short",
	"int",
	"unsigned_int",
	"long",
	"long_long",
	"unsigned_long",
	"unsigned_long_long",
	"float",
	"double",
	"idStr",
	"idLogicProperties",
	"attachParent_t",
	"idRenderModelWeakHandle",
	"idEventArg",

	// TODO: These are for eventcalls. Must get these hashes
	"idHandle_T_short___invalidEvent_t___INVALID_EVENT_HANDLE_T",
};

// For any structs that weren't included for whatever reason, but need to be
const std::set<std::string> ForcedInclusions = {
	"idEntityDefEditorVars",
	"idDeclEntityDef__gameSystemVariables_t",
	"idDeclLogicClass",
	"idDeclLogicLibrary",
	"idDeclLogicFX",
	"idDeclLogicEntity",
	"idDeclLogicUIWidget",
	"encounterLogicOperator_t"
};

// Instead of generating unique reflection functions, key structs' body functions
// will only include a call to the value's reflection function
const std::unordered_map<std::string, const char*> AliasStructs = {
	{"idAtomicString", "idStr"},
	{"idGameStatId", "idStr"},
	{"aliasHandle_t", "idStr"}, // This idHandle child is serialized as a string, but not all are guaranteed to be like this (encounter event handles seem to be a typeinfo hash)
	
	{"secondsToGameTime_t", "idTypesafeTime_T_long_long___gameTimeUnique_t___999960_T"},
	{"milliToGameTime_t", "idTypesafeTime_T_long_long___gameTimeUnique_t___999960_T"},

	// For some reason, these are all serialized in 4 bytes, even if the underlying value is 8 bytes
	{"idTypesafeTime_T_long_long___gameTimeUnique_t___999960_T", "int"},
	{"idTypesafeTime_T_long_long___microsecondUnique_t___1000000_T", "int"},
	{"idTypesafeTime_T_int___millisecondUnique_t___1000_T", "int"},
	{"idTypesafeTime_T_float___secondUnique_t___1_T", "float"},

	// Some are aliased to their literal type, but scene director-related nodes 
	// have no aliasing
	{"idTypesafeNumber_T_float___RadiansUnique_t_T", "float"},
	{"idTypesafeNumber_T_float___DegreesUnique_t_T", "float"},
	{"idTypesafeNumber_T_float___RadiusUnique_t_T", "float"},
	{"idTypesafeNumber_T_float___SphereUnique_t_T", "float"},
	{"idTypesafeNumber_T_float___LightUnitsUnique_t_T", "float"},

	// TODO: Monitor. Same issue as idRenderModelWeakHandle - block is always empty 
	{"idFxHandle", "idRenderModelWeakHandle"}

};

// These types are used in idTypeInfoObjectPtrs. We must forcibly include
// them and all their descendant types
// (Auto-including every TypeInfoObjectPtr class hierarchy would massively bloat
// the reflection code. Trying to avoid this by only including what is actually encountered for now)
const std::set<std::string> PolymorphicInclusions = {
	"idLogicVariableModel",
	"idTriggerBodyData",
	"idTransportComponent",
	"idEntityModifier_AI_Buff",
	"idPhysicsEditorConstraintDef",
	"idCollision_Animated",
	"idAICondition",
	"idLogicGraphAssetClassMain",
	"idLogicNodeModel",
	"idLogicGraphAssetState",
	"idLogicGraphAssetFunction",
	"idLogicGUIItem",
	"idQuestUIData",
	"idAISnippet",
	"idLogicDebugGeometry",
	"idLogicGraphAssetEvent",
	"idUICommand",
};

// Manual Pointer Functions
const std::unordered_map<std::string, std::string> PointerFunctionMap = {
	{"idStaticModel", "pointerdecl"},
	{"idMD6Anim", "pointerdecl"},
	{"idColorLUT", "pointerdecl"},
	{"idCVar", "idStr"},
	{"idDeclInfo", "pointerdeclinfo"},
};



struct TypeMap {
	std::string_view alias = "";
	EntNode* node = nullptr;
	int referenceCount = 0;
	bool IsEntity = false;
	bool IsDecl = false;
	bool forceInlude = false;
	bool forceExclude = false; // Highest priority

	TypeMap() {}
	TypeMap(EntNode* n) : node(n) {}
};

struct inheritanceFlags {
	bool IsEntity = false;
	bool IsDecl = false;
};

class idlibCleaner2 
{
	private:
	EntityParser parser = EntityParser("../input/idlibcleaned_pass1.txt", ParsingMode::PERMISSIVE);
	std::unordered_map<std::string, TypeMap> typelib;

	public:

	void AddTypeMap(EntNode& typelist);
	void CountReferences(EntNode& typelist);
	void CheckInheritance(std::string_view type);
	int IncludeDescendantsOf(std::string_view parentName);
	void RecurseTemplates();
	bool RecurseTemplates_idList(EntNode& listnode, bool force);
	void ApplyManualSettings();


	void Build();
	void Output();
};

void idlibCleaner2::AddTypeMap(EntNode& typelist) {
	for (int i = 0, max = typelist.getChildCount(); i < max; i++) {
		EntNode* n = typelist.ChildAt(i);
		typelib.emplace(n->getName(), n);
	}
}

void idlibCleaner2::CheckInheritance(std::string_view type) {
	auto& pair = typelib[std::string(type)];
	
	EntNode* parent = &(*pair.node)["parentName"];
	while (parent != EntNode::SEARCH_404) {
		std::string_view parentString = parent->getValue();
		if (parentString == "idEngineEntity") {
			pair.IsEntity = true;
			return;
		}
		else if (parentString == "idDecl") {
			pair.IsDecl = true;
			return;
		}
		else if (parentString == "idStr") {
			pair.alias = "idStr";
			return;
		}
		else if (parentString == "idAtomicString") {
			pair.alias = "idAtomicString";
			return;
		}
			
		
		auto parentPair = typelib.find(std::string(parentString));

		/*
		* A very small number of structs have a parent type that's
		* not defined in the idlib - we fix these here
		*/
		if (parentPair == typelib.end()) {
			
			parser.EditText("UNDEFINED_PARENTtrue", parent, 16, false);
			//printf("UNKNOWN PARENT: %s\n", std::string(parentString).c_str());
			return;
		}
		else {
			parent = &(*parentPair->second.node)["parentName"];
		}
	}
}

// Used in template recursion
int idlibCleaner2::IncludeDescendantsOf(std::string_view targetParent) {
	int newInclusions = 0;

	for (auto& iter : typelib) {
		
		if (targetParent == iter.first) {
			if (iter.second.referenceCount == 0 && !iter.second.forceInlude) {
				iter.second.forceInlude = true;
				newInclusions++;
			}
			continue;
		}

		EntNode* parent = &(*iter.second.node)["parentName"];
		while (parent != EntNode::SEARCH_404) {
			std::string_view parentString = parent->getValue();
			if (parentString == targetParent) {
				if (iter.second.referenceCount == 0 && !iter.second.forceInlude) {
					iter.second.forceInlude = true;
					newInclusions++;
				}
				//printf("%.*s\n", (int)iter.first.length(), iter.first.data());
				break;
			}
		
			auto parentPair = typelib.find(std::string(parentString));
			/*
			* A very small number of structs have a parent type that's
			* not defined in the idlib - we fix these here
			*/
			assert(parentPair != typelib.end());
			parent = &(*parentPair->second.node)["parentName"];
		}
	}

	return newInclusions;
}

void idlibCleaner2::CountReferences(EntNode& typelist)
{
	for (int i = 0, max = typelist.getChildCount(); i < max; i++) {
		EntNode& type = *typelist.ChildAt(i);
		EntNode& valueList = type["values"];

		for (int k = 0; k < valueList.getChildCount(); k++) {
			EntNode& value = *valueList.ChildAt(k);

			if(&value["INCLUDE"] == EntNode::SEARCH_404)
				continue;

			// TODO: MONITOR TO ENSURE THIS IS ACCEPTABLE
			// Interestingly, this only makes a mere ~6000 line difference
			// To truly cull unnecessary stuff we would need to rewrite the
			// reference counter to only include things directly referenced by entity classes
			if(&value["pointers"] != EntNode::SEARCH_404)
				continue;

			// Need to check for types not defined in the idlib
			auto pair = typelib.find(std::string(value.getName()));
			if (pair != typelib.end()) {
				pair->second.referenceCount++;
			}
		}
	}
}

/*
* Plan B:
* - Build up an inheritance hierarchy of all idEngineEntity classes
* - Recursively scan through these classes and mark all non-pointer variable types for inclusion
*	- Don't need to scan through parents if we're inlining parent properties into child maps
* - May need to add special exemption for idList variables
*/

bool idlibCleaner2::RecurseTemplates_idList(EntNode& list, bool force) {
	// Skip idLists which are not included
	auto iter = typelib.find(std::string(list.getName()));
	assert(iter != typelib.end());
	if (iter->second.referenceCount == 0 && !iter->second.forceInlude && !force)
		return false;

	// Get the parent idListBase
	EntNode& basename = list["parentName"];
	iter = typelib.find(std::string(basename.getValue()));
	assert(iter != typelib.end());

	// Get the type stored by the list
	EntNode& baselist = *iter->second.node;
	EntNode& listtype = *baselist["values"].ChildAt(0);
	assert(listtype.getValue() == "list");
	assert(&listtype["array"] == EntNode::SEARCH_404);

	bool mustInclude;
	{
		std::string_view pointerCount = listtype["pointers"].getValue();
		assert(pointerCount.length() == 1);
		mustInclude = pointerCount[0] == '1';
	}

	if (mustInclude) {
		iter = typelib.find(std::string(listtype.getName()));
		assert(iter != typelib.end());

		if (!iter->second.forceInlude) {
			iter->second.forceInlude = true;
			return true;
		}
	}

	return false;
}

void idlibCleaner2::RecurseTemplates() {
	EntNode& templates = (*parser.getRoot())["templates"];
	printf("Executing RecurseTemplates\n");

	bool includedThisRun = false;
	
	// idListBase variables have no tags. 
	// We must forcibly iterate over the list variable's type and include it if missing
	// Have to fetch the idListBase from the child idList because it's only the latter
	// that are included
	EntNode& idLists = templates["idList"];
	assert(&idLists != EntNode::SEARCH_404);
	for (int i = 0, max = idLists.getChildCount(); i < max; i++) {
		EntNode& list = *idLists.ChildAt(i);

		if(RecurseTemplates_idList(list, false))
			includedThisRun = true;
	}

	EntNode& idLogicLists = templates["idLogicList"];
	assert(&idLogicLists != EntNode::SEARCH_404);
	for (int i = 0, max = idLogicLists.getChildCount(); i < max; i++) {
		EntNode& logiclist = *idLogicLists.ChildAt(i);

		// Skip idLists which are not included
		auto iter = typelib.find(std::string(logiclist.getName()));
		assert(iter != typelib.end());
		if (iter->second.referenceCount == 0 && !iter->second.forceInlude)
			continue;

		// Get the parent idListBase
		EntNode& basename = logiclist["parentName"];
		iter = typelib.find(std::string(basename.getValue()));
		assert(iter != typelib.end());
		EntNode& list = *iter->second.node;

		if (RecurseTemplates_idList(list, true)) {
			includedThisRun = true;
		}
			
	}


	EntNode& idStaticLists = templates["idStaticList"];
	assert(&idStaticLists != EntNode::SEARCH_404);
	for (int i = 0, max = idStaticLists.getChildCount(); i < max; i++) {
		EntNode& list = *idStaticLists.ChildAt(i);

		// Skip lists which are not included
		auto iter = typelib.find(std::string(list.getName()));
		assert(iter != typelib.end());
		if (iter->second.referenceCount == 0 && !iter->second.forceInlude)
			continue;

		EntNode& listtype = *list["values"].ChildAt(0);
		assert(listtype.getValue() == "staticList");

		bool mustInclude;
		{
			// Since it's a static array, there may not be a pointer
			std::string_view pointerCount = listtype["pointers"].getValue();
			assert(pointerCount.empty() || pointerCount[0] == '1');
			mustInclude = pointerCount.empty();
		}

		if (mustInclude) {
			iter = typelib.find(std::string(listtype.getName()));
			assert(iter != typelib.end());

			if (!iter->second.forceInlude) {
				iter->second.forceInlude = true;
				includedThisRun = true;
			}
		}
	}

	/*
	* idListMaps are another special case - neither the key nor value list has reflection tags
	*/
	EntNode& idListMaps = templates["idListMap"];
	assert(&idListMaps != EntNode::SEARCH_404);

	for (int i = 0, max = idListMaps.getChildCount(); i < max; i++) {
		EntNode& map = *idListMaps.ChildAt(i);

		// Skip list maps which are not included
		auto iter = typelib.find(std::string(map.getName()));
		assert(iter != typelib.end());
		if (iter->second.referenceCount == 0 && !iter->second.forceInlude)
			continue;

		EntNode& valuenode = map["values"];
		
		// Include the key value
		{
			EntNode& keyvar = *valuenode.ChildAt(0);
			iter = typelib.find(std::string(keyvar.getName()));
			assert(iter != typelib.end());
			if (!iter->second.forceInlude) {
				iter->second.forceInlude = true;
				includedThisRun = true;
			}
		}

		// Include the value var
		{
			EntNode& valuevar = *valuenode.ChildAt(1);
			iter = typelib.find(std::string(valuevar.getName()));
			assert(iter != typelib.end());
			if (!iter->second.forceInlude) {
				iter->second.forceInlude = true;
				includedThisRun = true;
			}
		}
	}

	// This will more than double the amount of included types - and increases runtime of this phase
	// To try and avoid doing this for as long as possible, I'll attempt to only include types which are actually encountered
	// Also, this probably only needs to run once - should monitor and see

	//printf("In looping hell\n");
	//EntNode& idTypeInfoObjectPtrs = templates["idTypeInfoObjectPtr"];
	//assert(&idTypeInfoObjectPtrs != EntNode::SEARCH_404);
	//for (int i = 0, max = idTypeInfoObjectPtrs.getChildCount(); i < max; i++) {
	//	EntNode& objptr = *idTypeInfoObjectPtrs.ChildAt(i);

	//	EntNode& ptrtype = *objptr["values"].ChildAt(0);
	//	assert(ptrtype.getValue() == "object");

	//	std::string_view typeString = ptrtype.getName();
	//	//printf("%.*s ", (int)typeString.length(), typeString.data());
	//	if (typeString == "idClass") {
	//		printf("skipping idTypeInfoObjectPtr for idClass\n");
	//		continue;
	//	}

	//	int newIncludes = IncludeDescendantsOf(typeString);
	//	if(newIncludes > 0)
	//		includedThisRun = true;
	//}

	if(includedThisRun)
		RecurseTemplates();

}

void idlibCleaner2::ApplyManualSettings()
{
	for (const std::string& s : ForcedExclusions) {
		auto iter = typelib.find(s);
		assert(iter != typelib.end());
		iter->second.forceExclude = true;
	}

	for (const std::string& s : ForcedInclusions) {
		auto iter = typelib.find(s);
		assert(iter != typelib.end());
		iter->second.forceInlude = true;
	}

	for (const auto& pair : AliasStructs) {
		auto iter = typelib.find(pair.first);
		assert(iter != typelib.end());
		iter->second.alias = pair.second;
	}

	for (const std::string& s : PolymorphicInclusions) {
		IncludeDescendantsOf(s);
	}

	for (const auto& pair : PointerFunctionMap) {
		auto iter = typelib.find(pair.first);
		assert(iter!= typelib.end());

		std::string functext = "pointerfunc = ";
		functext.append(pair.second);

		parser.EditTree(functext, iter->second.node, 0, 0, 0, 0);
	}
}

void idlibCleaner2::Build() {

	EntNode& root = *parser.getRoot();
	EntNode& enums = root["enums"];
	EntNode& structs = root["structs"];
	EntNode& templatesubs = root["templatesubs"];
	EntNode& templates = root["templates"];

	assert(&enums != EntNode::SEARCH_404);
	assert(&structs != EntNode::SEARCH_404);
	assert(&templatesubs != EntNode::SEARCH_404);
	assert(&templates != EntNode::SEARCH_404);



	printf("Building type map\n");
	typelib.reserve(25000);
	AddTypeMap(enums);
	AddTypeMap(structs);
	AddTypeMap(templatesubs);
	for (int i = 0, max = templates.getChildCount(); i < max; i++) {
		AddTypeMap(*templates.ChildAt(i));
	}

	printf("Inheritance Analysis\n");
	for (auto& pair : typelib) {
		CheckInheritance(pair.first);
	}

	printf("Counting references \n"); // Do NOT count enums here
	CountReferences(structs);
	CountReferences(templatesubs);
	for (int i = 0, max = templates.getChildCount(); i < max; i++) {
		CountReferences(*templates.ChildAt(i));
	}

	// This probably should be placed before RecurseTemplates
	printf("Applying Manual Settings\n");
	ApplyManualSettings();

	//printf("Iterating over special types\n");
	RecurseTemplates();

	// TODO: This is too overzealous - it's excluding entity classes
	// Seems better now after manually including entity classes - continue to monitor
	printf("Adding Include Tags\n");
	int includeCount = 0;

	for (auto& pair : typelib)
	{

		if (pair.second.IsDecl) {
			parser.EditTree("pointerfunc = pointerdecl", pair.second.node, 0, 0, false, false);
		}

		if (!pair.second.alias.empty()) {
			std::string aliasText = "alias = ";
			aliasText.append(pair.second.alias);
			parser.EditTree(aliasText, pair.second.node, 0, 0, false, false);
		}

		if (!pair.second.forceExclude && (pair.second.referenceCount > 0 || pair.second.IsEntity || pair.second.forceInlude)) {
			includeCount++;
			parser.EditTree("INCLUDE", pair.second.node, 0, 0, false, false);
		}
		
	}
	printf("%d types out of %zu should be included \n", includeCount, typelib.size());
}

void idlibCleaner2::Output() {
	parser.WriteToFile("../input/idlibcleaned.txt", false);
}

void idlibCleaning::Pass2()
{
	printf("Cleaning Phase 2\n");

	idlibCleaner2 cleaner;
	cleaner.Build();
	cleaner.Output();
}