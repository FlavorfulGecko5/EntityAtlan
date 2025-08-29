#include "reflector.h"
#include "entityslayer/EntityParser.h"
#include "hash/HashLib.h"
#include <fstream>
#include <set>
#include <unordered_map>
#include <cassert>

const char* desHeaderStart =
R"(#include <string>
class BinaryReader;

typedef void dsfunc_t(BinaryReader&, std::string&);

namespace deserial {
)";

const char* resHeaderStart =
R"(
class BinaryWriter;
class EntNode;

typedef void rsfunc_t(const EntNode&, BinaryWriter&);

namespace reserial {
)";

const char* desCppStart =
R"(#include "deserialgenerated.h"
#include "deserialcore.h"

#define dsfunc_m(NAME) void NAME(BinaryReader& reader, std::string& writeTo)

)";

const char* resCppStart =
R"(#include "reserialgenerated.h"
#include "serialcore.h"

#define rsfunc_m(NAME) void NAME(const EntNode& property, BinaryWriter& writer)

)";

class StringTee 
{
    private:
    std::string left;
    std::string right;

    public:
    StringTee(size_t leftsize, size_t rightsize) 
    {
        left.reserve(leftsize);
        right.reserve(rightsize);
    }

    void add(const std::string_view leftdata, const std::string_view rightdata)
    {
        left.append(leftdata);
        right.append(rightdata);
    }

    void both(const std::string_view data) 
    {
        left.append(data);
        right.append(data);
    }

    void push(const char leftdata, const char rightdata) {
        left.push_back(leftdata);
        right.push_back(rightdata);
    }

    void output(const std::string leftfile, const std::string rightfile)
    {
        std::ofstream output(leftfile, std::ios_base::binary);
        output << left;
        output.close();

        output.open(rightfile, std::ios_base::binary);
        output << right;
        output.close();
    }
};

class idlibReflector {
    public:
    // 
    const std::unordered_map<std::string, void(idlibReflector::*)(EntNode&)> SpecialTemplates = {
        {"idList", &idlibReflector::GenerateidList},
        {"idListBase", &idlibReflector::GenerateidListBase},
        {"idStaticList", &idlibReflector::GenerateidStaticList},
        {"idListMap", &idlibReflector::GenerateidListMap },
        {"idTypeInfoPtr", &idlibReflector::GenerateidTypeInfoPtr},
        {"idTypeInfoObjectPtr", &idlibReflector::GenerateidTypeInfoObjectPtr},
        {"idManagedClassPtr", &idlibReflector::GenerateidManagedClassPtr},
        {"idLogicEntityPtr", &idlibReflector::GenerateidLogicEntityPtr},
        {"idLogicList", &idlibReflector::GenerateidLogicList},
        {"idRenderModelWeakHandleT", &idlibReflector::GenerateidRenderModelWeakHandleT}
    };

    // LEFT: Deserial; RIGHT: Reserial
    StringTee headertee = StringTee(1000000, 1000000);

    // LEFT: Deserial; RIGHT: Reserial
    StringTee srctee = StringTee(8000000, 8000000);

    std::unordered_map<std::string, EntNode*> typelib;
    //std::string desheader = desHeaderStart;
    //std::string descpp = desCppStart;

    idlibReflector() {
        headertee.add(desHeaderStart, resHeaderStart);
        srctee.add(desCppStart, resCppStart);
    }

    void GenerateEnums(EntNode& enums) {

        EntNode** enumArray = enums.getChildBuffer();
        int enumCount = enums.getChildCount();

        for (EntNode** iter = enumArray, **iterMax = enumArray + enumCount; iter < iterMax; iter++) {
            EntNode& current = **iter;

            if(&current["INCLUDE"] == EntNode::SEARCH_404)
                continue;

            headertee.add("\tdsfunc_t ds_", "\trsfunc_t rs_");
            headertee.both(current.getName());
            headertee.both(";\n");

            srctee.add("dsfunc_m(deserial::ds_", "rsfunc_m(reserial::rs_");
            srctee.both(current.getName());
            srctee.both(") {\n");
            srctee.add("\tconst dsenummap_t valueMap = {\n", "\tconst rsenumset_t valueSet = {\n");

            EntNode& values = current["values"];
            EntNode** valueArray = values.getChildBuffer();
            int valueCount = values.getChildCount();

            for (EntNode** valIter = valueArray, **valMax = valueArray + valueCount; valIter < valMax; valIter++) {
                EntNode& v = **valIter;
                std::string_view vName = v.getName();
                uint64_t hash = HashLib::FarmHash64(vName.data(), vName.length());

                srctee.both("\t\t{");
                srctee.both(std::to_string(hash)); 
                srctee.add("UL, \"", "UL},\n"); // Reserializer is only a hash set. Deserializer requires a bit more data
                srctee.add(v.getName(), "");
                srctee.add("\"},\n", "");
            }

            srctee.add("\t};\n\tds_enumbase(reader, writeTo, valueMap);\n", "\t};\n\trs_enumbase(property, writer, valueSet);\n");
            srctee.both("}\n");
        }
    }

    void WritePointerFunc(std::string_view typeName) {
        auto iter = typelib.find(std::string(typeName));
        assert(iter != typelib.end());

        EntNode& pointerfunc = (*iter->second)["pointerfunc"];
        if (&pointerfunc == EntNode::SEARCH_404) {
            srctee.both("pointerbase");
        }
        else {
            srctee.both(pointerfunc.getValue());
        }
    }

    void PopulateStructMap(EntNode& typeNode) {
        EntNode& values = typeNode["values"];
        EntNode** valueArray = values.getChildBuffer();
        int valueCount = values.getChildCount();

        for (EntNode** valIter = valueArray, **valMax = valueArray + valueCount; valIter < valMax; valIter++) {
            EntNode& v = **valIter;

            if (&v["INCLUDE"] == EntNode::SEARCH_404)
                continue;

            uint64_t hash = HashLib::FarmHash64(v.getValue().data(), v.getValue().length());

            srctee.both("\t\t{");
            srctee.both(std::to_string(hash));
            srctee.add(", {&ds_", ", {&rs_");

            /* If a pointer, map to the appropriate pointer function */
            EntNode& pointers = v["pointers"];
            if (&pointers == EntNode::SEARCH_404) {
                srctee.both(v.getName());
            } 
            else {
                // Double pointer variables in idLists technically aren't flagged for inclusion
                // and will be handled differently.
                std::string_view ptrCount = pointers.getValue();
                assert(ptrCount.length() == 1 && ptrCount[0] == '1');

                WritePointerFunc(v.getName());
            }

            // Deserializer: Add the value string; Reserializer: Repeat the farmhash
            srctee.add(", \"", ", "); 
            srctee.add(v.getValue(), std::to_string(hash)); 
            srctee.add("\"", "");

            if (&v["array"] != EntNode::SEARCH_404) {
                srctee.both(", ");
                srctee.both(v["array"].getValue());
            }


            srctee.both("}},\n");
        }

        EntNode& parentName = typeNode["parentName"];
        if (&parentName != EntNode::SEARCH_404) {
            auto iter = typelib.find(std::string(parentName.getValue()));
            assert(iter != typelib.end());
            EntNode* parentType = iter->second;

            PopulateStructMap(*parentType);
        }
    }

    // bodyFunction is used for special template types that we need to generate non-standard
    // reflection code for (like idLists)
    void GenerateStruct(EntNode& structs, void(idlibReflector::*bodyFunction)(EntNode&) = nullptr) {

        EntNode** structArray = structs.getChildBuffer();
        int structCount = structs.getChildCount();

        for (EntNode** structIter = structArray, **structMax = structArray + structCount; structIter < structMax; structIter++)
        {
            EntNode& current = **structIter;

            if(&current["INCLUDE"] == EntNode::SEARCH_404)
                continue;

            headertee.add("\tdsfunc_t ds_", "\trsfunc_t rs_");
            headertee.both(current.getName());
            headertee.both(";\n");
            
            srctee.add("dsfunc_m(deserial::ds_", "rsfunc_m(reserial::rs_");
            srctee.both(current.getName());
            srctee.both(") {\n");

            if (bodyFunction == nullptr) {
                EntNode& alias = current["alias"];

                if (&alias == EntNode::SEARCH_404) {
                    srctee.add("\tconst dspropmap_t propMap = {\n", "\tconst rspropmap_t propMap = {\n");
                    PopulateStructMap(current);
                    srctee.add("\t};\n\tds_structbase(reader, writeTo, propMap);\n", "\t};\n\trs_structbase(property, writer, propMap);\n");
                }
                else {
                    srctee.add("\tds_", "\trs_");
                    srctee.both(alias.getValue());
                    srctee.add("(reader, writeTo);\n", "(property, writer);\n"); 
                }

            }
            else {
                (this->*bodyFunction)(current);
            }

            srctee.both("}\n");
        }
        ///* Generate Reflection Code */

        //if (structData.exclude) {
        //    descpp.append("\t#ifdef _DEBUG\n\tassert(0);\n\t#endif\n");
        //}
    }
    
    void GenerateidList(EntNode& typenode) {
        // Get the parent type
        EntNode& parentName = typenode["parentName"];
        auto iter = typelib.find(std::string(parentName.getValue()));
        assert(iter != typelib.end());
        EntNode& parentnode = *iter->second;

        // The first value in idListBase is what the list stores
        EntNode& listType = *parentnode["values"].ChildAt(0);
        assert(&listType != EntNode::SEARCH_404);
        
        bool usePointerFunc;
        {
            std::string_view pointerCount = listType["pointers"].getValue();
            assert(pointerCount.length() == 1 && (pointerCount[0] == '1' || pointerCount[0] == '2'));
            
            usePointerFunc = pointerCount[0] == '2';
        }

        srctee.add("\tds_idList(reader, writeTo, &ds_", "\trs_idList(property, writer, &rs_");
        
        if (usePointerFunc) {
            WritePointerFunc(listType.getName());
        }
        else {
            srctee.both(listType.getName());
        }
        srctee.both(");\n");

        //printf("%.*s\n", (int)listType.getName().length(), listType.getName().data());   
    }

    void GenerateidListBase(EntNode& typenode) {
        // idListBase should never actually be included, only idList
        assert(0);
    }

    void GenerateidLogicList(EntNode& typenode) {
        /* idLogicLists are children of idLists */
        EntNode& parentName = typenode["parentName"];
        auto iter = typelib.find(std::string(parentName.getValue()));
        assert(iter != typelib.end());
        EntNode& parentNode = *iter->second;
        GenerateidList(parentNode);
    }

    void GenerateidStaticList(EntNode& typenode) {
        // The first value in idListBase is what the list stores
        EntNode& listType = *typenode["values"].ChildAt(0);
        assert(listType.getValue() == "staticList");

        bool usePointerFunc;
        {
            // Since it's a static array, there may not be a pointer
            std::string_view pointerCount = listType["pointers"].getValue();
            assert(pointerCount.empty() || pointerCount[0] == '1');
            usePointerFunc = !pointerCount.empty();
        }

        srctee.add("\tds_idList(reader, writeTo, &ds_", "\trs_idList(property, writer, &rs_");

        if (usePointerFunc) {
            WritePointerFunc(listType.getName());
        }
        else {
            srctee.both(listType.getName());
        }
        srctee.both(");\n");
    }

    void GenerateidListMap(EntNode& typenode) {
        // We need to get the functions for the key and value types
        EntNode& keyval = *typenode["values"].ChildAt(0);
        EntNode& valueval = *typenode["values"].ChildAt(1);

        srctee.add("\tds_idListMap(reader, writeTo, &ds_", "\trs_idListMap(property, writer, &rs_");

        // Get key function
        {
            auto iter = typelib.find(std::string(keyval.getName()));
            assert(iter != typelib.end());

            // Get the idListBase
            iter = typelib.find(std::string((*iter->second)["parentName"].getValue()));
            assert(iter != typelib.end());

            EntNode& keylist = *(*iter->second)["values"].ChildAt(0);
            assert(keylist.getValue() == "list");
            assert(&keylist["pointers"] != EntNode::SEARCH_404);
            if (keylist["pointers"].getValue()[0] == '2')
                WritePointerFunc(keylist.getName());
            else srctee.both(keylist.getName());
        }

        

        // Get value function
        srctee.add(", &ds_", ", &rs_");
        {
            auto iter = typelib.find(std::string(valueval.getName()));
            assert(iter != typelib.end());

            // Get the idListBase
            iter = typelib.find(std::string((*iter->second)["parentName"].getValue()));
            assert(iter != typelib.end());

            EntNode& valuelist = *(*iter->second)["values"].ChildAt(0);
            assert(valuelist.getValue() == "list");
            assert(&valuelist["pointers"] != EntNode::SEARCH_404);
            if (valuelist["pointers"].getValue()[0] == '2')
                WritePointerFunc(valuelist.getName());
            else srctee.both(valuelist.getName());
        }

        srctee.both(");\n");
    }

    void GenerateidTypeInfoPtr(EntNode& typenode) {
        srctee.add("\tds_idTypeInfoPtr(reader, writeTo);\n", "\trs_idTypeInfoPtr(property, writer);\n");
    }

    void GenerateidTypeInfoObjectPtr(EntNode& typenode) {
        srctee.add("\tds_idTypeInfoObjectPtr(reader, writeTo);\n", "\trs_idTypeInfoObjectPtr(property, writer);\n");
    }

    void GenerateidRenderModelWeakHandleT(EntNode& typenode) {
        srctee.add("\tds_idRenderModelWeakHandle(reader, writeTo);\n", "\trs_idRenderModelWeakHandle(property, writer);\n");
    }

    void GenerateidManagedClassPtr(EntNode& typenode) {
        srctee.add("\tds_idStr(reader, writeTo);\n", "\trs_idStr(property, writer);\n");
    }

    void GenerateidLogicEntityPtr(EntNode& typenode) {
        srctee.add("\tds_idStr(reader, writeTo);\n", "\trs_idStr(property, writer);\n");
    }


    void AddTypeMap(EntNode& typelist) {
        for (int i = 0, max = typelist.getChildCount(); i < max; i++) {
            EntNode* n = typelist.ChildAt(i);
            typelib.emplace(n->getName(), n);
        }
    }

    void GenerateHashMaps() {
        srctee.add("const std::unordered_map<uint32_t, deserialTypeInfo> deserial::typeInfoPtrMap = {\n", 
            "const std::unordered_map<uint64_t, reserialTypeInfo> reserial::typeInfoPtrMap = {\n"
        );

        // FIX: Placing the reflection name into the deserial type map string, should be using the original name
        for (const auto& pair : typelib) {
            EntNode& node = *pair.second;
            if(&node["INCLUDE"] == EntNode::SEARCH_404)
                continue;

            EntNode& hashNode = node["hash"];
            assert(&hashNode != EntNode::SEARCH_404);
            EntNode& originalnamenode = node["originalName"];
            assert(&originalnamenode != EntNode::SEARCH_404);

            std::string_view originalname = originalnamenode.getValueUQ();
            uint64_t namefarmhash = HashLib::FarmHash64(originalname.data(), originalname.length());

            srctee.both("\t{");
            srctee.add(hashNode.getValueUQ(), std::to_string(namefarmhash));
            srctee.add("U, { &ds_", "UL, { &rs_");
            srctee.both(pair.first);
            srctee.both(", ");
            srctee.add(originalnamenode.getValue(), hashNode.getValueUQ());
            //srctee.add(originalname, hashNode.getValueUQ());
            srctee.both("}},\n");
        }

        srctee.both("};\n");
    }

    void Generate(EntNode& root) {
        EntNode& enums = root["enums"];
        EntNode& structs = root["structs"];
        EntNode& templatesubs = root["templatesubs"];
        EntNode& templates = root["templates"];

        assert(&enums != EntNode::SEARCH_404);
        assert(&structs != EntNode::SEARCH_404);
        assert(&templatesubs != EntNode::SEARCH_404);
        assert(&templates != EntNode::SEARCH_404);

        /* Build Type Lib */
        typelib.reserve(25000);
        AddTypeMap(enums);
        AddTypeMap(structs);
        AddTypeMap(templatesubs);
        for (int i = 0, max = templates.getChildCount(); i < max; i++) {
            AddTypeMap(*templates.ChildAt(i));
        }

        GenerateHashMaps();
        GenerateEnums(enums);
        GenerateStruct(structs);
        GenerateStruct(templatesubs);
        for (int i = 0, max = templates.getChildCount(); i < max; i++) {

            EntNode* t = templates.ChildAt(i);
            auto iter = SpecialTemplates.find(std::string(t->getName()));
            if (iter == SpecialTemplates.end()) {
                GenerateStruct(*t);
            }
            else {
                GenerateStruct(*t, iter->second);
            }
            
        }
    }
};

void idlibReflection::Generate() {

    printf("Engaging idlib Reflector shields\n");
    EntityParser parser = EntityParser("idlibcleaned.txt", ParsingMode::PERMISSIVE);
    EntNode* root = parser.getRoot();

    
    idlibReflector reflector;
    reflector.Generate(*root);

    reflector.headertee.both("}");
    reflector.headertee.output("../deserializer/src/deserialgenerated.h", "../reserializer/src/reserialgenerated.h");
    reflector.srctee.output("../deserializer/src/deserialgenerated.cpp", "../reserializer/src/reserialgenerated.cpp");
}
