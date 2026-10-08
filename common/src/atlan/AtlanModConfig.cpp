#include "AtlanModConfig.h"
#include "entityslayer/EntityParser.h"
#include "atlan/AtlanLogger.h"
#include "atlan/AtlanLib.h"
#include <miniz/miniz.h>

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
        atlog("ERROR: Failed to parse config file");
        return false;
    }
    
}

bool AtlanModConfig::TryRead(const char* data, const size_t length) {
    
    try {
        EntityParser parser(ParsingMode::PERMISSIVE, std::string_view(data, length), false);
        return TryRead_Internal(*parser.getRoot());
    }
    catch (...) {
        atlog("ERROR: Failed to parse config file");
        return false;
    }
}

bool AtlanModConfig::TryRead_FromDir(const fspath& ModDir, mz_zip_archive* AddConfigTo)
{
    fspath ConfigPath = ModDir / CFG_NAME;
    const char* name = CFG_NAME;

    if (std::filesystem::exists(ConfigPath)) {
        gameid = game_darkages;
    }
    else {
        ConfigPath = ModDir / CFG_NAME_V2;
        name = CFG_NAME_V2;
        if (!std::filesystem::exists(ConfigPath)) {
            atlog("WARNING: Could not find mod config file");
            return true;
        }
    }

    if (AddConfigTo) {
        bool result = mz_zip_writer_add_file(AddConfigTo, name, ConfigPath.string().c_str(), "", 0, MZ_DEFAULT_COMPRESSION);
        if (!result) {
            atlog("ERROR: Failed to add config file to zip");
            return false;
        }
    }

    atlog("Found %s", name);
    return TryRead(ConfigPath.string());
}

bool AtlanModConfig::TryRead_FromZip(mz_zip_archive* zptr)
{
    const char* name = CFG_NAME;
    charbuffer_t raw;

    raw.data = (char*)mz_zip_reader_extract_file_to_heap(zptr, name, &raw.length, 0);
    if (raw.data) {
        gameid = game_darkages;
    }
    else {
        name = CFG_NAME_V2;
        raw.data = (char*)mz_zip_reader_extract_file_to_heap(zptr, name, &raw.length, 0);
        if (!raw.data) {
            atlog("WARNING: Could not find mod config file");
            return true;
        }
    }

    atlog("Found %s", name);
    return TryRead(raw.data, raw.length);
}

std::vector<fspath> AtlanModConfig::GetModFiles(const fspath& moddir)
{
    using namespace std::filesystem;
    std::vector<fspath> files;
    files.reserve(32);

    fspath modfile, substring;

    for (const directory_entry& DirEntry : recursive_directory_iterator(moddir)) {
        if(DirEntry.is_directory())
            continue;

        modfile = DirEntry.path();
        substring = modfile.extension();
        if(substring == L".zip" || substring == L".ZIP")
            continue;

        substring = modfile.filename();
        if(substring == CFG_NAME || substring == CFG_NAME_V2)
            continue;
        files.push_back(modfile);
    }
    return files;
}

typedef EntNode enode;

#define CFG_REQUIREDVERSION "requiredVersion"
#define CFG_LOADPRIORITY "loadPriority"
#define CFG_ALIASES "aliasing"

void AtlanModConfig__ReadListBuffer(AtlanModConfig::namedlist_t*& list, int& num, EntNode& node) {

    if(node.getChildCount() == 0)
        return;

    list = new AtlanModConfig::namedlist_t[node.getChildCount()];
    for (EntNode& anode : node) {
        if (anode.IsComment())
            continue;

        AtlanModConfig::namedlist_t& a = list[num++];
        a.listname = anode.getNameUQ();
        a.entries = new std::string[anode.getChildCount()];

        for (const EntNode& entry : anode) {
            if (entry.IsComment())
                continue;
            a.entries[a.numentries++] = entry.getNameUQ();
        }
    }
}

bool AtlanModConfig::TryRead_Internal(EntNode& root) {

    /*
    * Read config properties
    */
    EntNode& modInfo = root["modinfo"];
    bool foundReqVersion = modInfo[CFG_REQUIREDVERSION].ValueInt(requiredVersion, INT_MIN, INT_MAX);
    bool foundLoadPriority = modInfo[CFG_LOADPRIORITY].ValueInt(loadPriority, INT_MIN, INT_MAX);

    std::string gamestring = std::string(modInfo["game"].getValue());
    if (gamestring.length()) {
        for (char& c : gamestring) {
            if (c >= 'A' && c <= 'Z')
                c += 32;
        }

        if (gamestring.find("eternal") != -1) {
            gameid = game_eternal;
        }
        else if (gamestring.find("dark") != -1) {
            gameid = game_darkages;
        }
        else {
            atlog("WARNING: Unknown game specified in config file");
            gameid = game_all;
        }
    }

    if (!foundReqVersion)
    {
        atlog("WARNING: " CFG_REQUIREDVERSION  " not found. Using default value");
    }
    if (!foundLoadPriority)
    {
        atlog("WARNING: " CFG_LOADPRIORITY  " not found. Using default value");
    }
    elevated = &modInfo["elevated_gameplay"] != EntNode::SEARCH_404;
    installOnce = &root["install_once"] != EntNode::SEARCH_404;
    levelfolder = root["load_level"].getValueUQ();

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
            listmapspec[nummapspec++] = m.getNameUQ();
        }
    }

    EntNode& assetnode = root["assets"];
    AtlanModConfig__ReadListBuffer(listassets, numAssetlists, assetnode);

    EntNode& resnode = root["mapresources"];
    AtlanModConfig__ReadListBuffer(listedits, numEdits, resnode);

    EntNode& newMapNode = root["newmaps"];
    AtlanModConfig__ReadListBuffer(listnewmaps, numnewmaps, newMapNode);

    return true;
}
