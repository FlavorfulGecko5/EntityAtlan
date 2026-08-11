#include "AtlanModConfig.h"
#include "entityslayer/EntityParser.h"
#include "atlan/AtlanLogger.h"

void AtlanModConfig::GetNormalizedName(const std::string& in_zipname, std::string& out_assettype, std::string& out_assetname) const
{
    out_assetname = in_zipname;

    // Normalize Path Separators
    char* c = out_assetname.data();
    while (*c) {
        if('\\' == *c || '@' == *c)
            *c = '/';
        c++;
    }

    // Check if an alias exists
    if (alias.size()) {
        const auto& pair = alias.find(out_assetname);
        if (pair != alias.end()) {
            out_assetname = pair->second;
        }
    }

    // Identify the asset type
    c = out_assetname.data();
    while (*c) {
        if ('/' == *c) {
            size_t slashindex = c - out_assetname.data();
            out_assettype = out_assetname.substr(0, slashindex);
            out_assetname.erase(0, slashindex + 1);
            break;
        }
        c++;
    }

    //atlog_deprecated << "TYPE: '" << out_assettype << "' NAME: '" << out_assetname << "'\n";
}

bool AtlanModConfig::TryRead(const std::string& filepath)
{
    try {
        EntityParser parser(filepath, ParsingMode::PERMISSIVE);
        return TryRead_Internal(*parser.getRoot());
    }
    catch (...) {
        atlog("ERROR: Failed to parse " CFG_NAME);
        return false;
    }
    
}

bool AtlanModConfig::TryRead(const char* data, const size_t length) {
    
    try {
        EntityParser parser(ParsingMode::PERMISSIVE, std::string_view(data, length), false);
        return TryRead_Internal(*parser.getRoot());
    }
    catch (...) {
        atlog("ERROR: Failed to parse " CFG_NAME);
        return false;
    }
}

typedef EntNode enode;

#define CFG_REQUIREDVERSION "requiredVersion"
#define CFG_LOADPRIORITY "loadPriority"
#define CFG_ALIASES "aliasing"

bool AtlanModConfig::TryRead_Internal(EntNode& root) {

    /*
    * Read config properties
    */
    EntNode& modInfo = root["modinfo"];
    bool foundReqVersion = modInfo[CFG_REQUIREDVERSION].ValueInt(requiredVersion, INT_MIN, INT_MAX);
    bool foundLoadPriority = modInfo[CFG_LOADPRIORITY].ValueInt(loadPriority, INT_MIN, INT_MAX);

    if (!foundReqVersion)
    {
        atlog("WARNING: " CFG_REQUIREDVERSION  " not found. Using default value");
    }
    if (!foundLoadPriority)
    {
        atlog("WARNING: " CFG_LOADPRIORITY  " not found. Using default value");
    }

    /*
    * Read optional filepath aliases
    */
    EntNode& aliasNode = root[CFG_ALIASES];
    for (int i = 0, max = aliasNode.getChildCount(); i < max; i++) {
        EntNode& currentAlias = *aliasNode.ChildAt(i);
        if (currentAlias.IsComment())
            continue;

        if (currentAlias.getValueUQ().length() == 0) {
            atlog("WARNING: Alias with empty value. Skipping");
            continue;
        }

        // For simplicity, we'll pre-normalize the directory separators in the aliases
        std::string normalizedname(currentAlias.getNameUQ());
        std::string normalizedvalue(currentAlias.getValueUQ());

        char* c = normalizedname.data();
        while (*c) {
            if('\\' == *c || '@' == *c)
                *c = '/';
            c++;
        }
        c = normalizedvalue.data();
        while (*c) {
            if ('\\' == *c || '@' == *c)
                *c = '/';
            c++;
        }

        alias.emplace(normalizedname, normalizedvalue);
    }
    if (alias.size() > 0)
        atlog("Found %llu alias definitions", alias.size());

    //for (auto& pair : cfg.alias) {
    //	atlog_deprecated << "\n" << pair.first << "-" << pair.second;
    //}

    EntNode& mapspecnode = root["mapspec"];
    if (mapspecnode.getChildCount() > 0) {
        listmapspec = new std::string[mapspecnode.getChildCount()];
        for (const EntNode& m : mapspecnode) {
            if(m.IsComment())
                continue;
            listmapspec[nummapspec++] = m.getName();
        }
    }

    EntNode& resourcenode = root["common_mapresources"];
    if (resourcenode.getChildCount() > 0) {
        listresources = new std::string[resourcenode.getChildCount()];

        for (const EntNode& r : resourcenode) {
            if(r.IsComment())
                continue;

            listresources[numresources++] = r.getNameUQ();
        }
    }

    return true;
}
