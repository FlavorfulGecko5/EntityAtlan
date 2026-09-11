#include "ModReader.h"
#include <set>

int IsNotOnlineSafe(const ModFile& modfile) {
	
	switch (modfile.typeenum) {
		case rt_image: 
		case rt_audio: 
		case rt_cswf: 
		return 0;

		case rt_compfile:
		if(modfile.assetPath._Starts_with("maps/game/pvp/"))
			return 1;
		break;
	}

	// Todo: Flesh out, add more elaborate decl type checking
	return 1;
}

// Format: Blang path, csv, Blang path, csv, etc.
extern const char* ONLINE_SAFETY_STRINGS[26];

extern unsigned char ONLINE_SAFETY_SWF[];
extern const size_t ONLINE_SAFETY_SWF_SIZE;

bool BuildOnlineSafetyMod(ModDef& mod) {
	mod.IsUnzipped = true;
	mod.ActiveZip = false;
	mod.modName = "GENERATED_ONLINE_SAFETY";

	mod.modFiles.reserve(9); // Online Safety SWF + String Files
	mod.modFiles.emplace_back();
	ModFile& swf = mod.modFiles.back();
	swf.typestring = "cswf";
	swf.typeenum = rt_cswf;
	swf.ownsData = false;
	swf.parentMod = &mod;
	swf.dataBuffer = (char*)ONLINE_SAFETY_SWF;
	swf.dataLength = ONLINE_SAFETY_SWF_SIZE;
	swf.realPath = "GENERATED";
	swf.assetPath = "swf/hud/menus/battle_arena/play_online_screen.swf";
	swf.resourceVersion = 9;

	#ifndef _DEBUG
	for (size_t i = 0; i < sizeof(ONLINE_SAFETY_STRINGS) / sizeof(ONLINE_SAFETY_STRINGS[0]); i+= 2) {

		const char* ASSETPATH = ONLINE_SAFETY_STRINGS[i];
		const char* CSVDATA = ONLINE_SAFETY_STRINGS[i + 1];

		mod.modFiles.emplace_back();
		
		ModFile& csv = mod.modFiles.back();
		csv.typestring = "binaryFile";
		csv.typeenum = rt_binaryFile;
		csv.ownsData = true;
		csv.parentMod = &mod;
		csv.realPath = "GENERATED";
		csv.assetPath = ASSETPATH;
		csv.resourceVersion = 1;

		csv.dataLength = strlen(CSVDATA);
		csv.dataBuffer = new char[csv.dataLength];
		memcpy(csv.dataBuffer, CSVDATA, csv.dataLength);
	}
	#endif


	return true;
}