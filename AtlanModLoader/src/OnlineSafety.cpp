#include "ModReader.h"
#include <set>

const std::set<std::string> ONLINE_SAFE_DECLTYPES = {
	"advancedscreenviewshake", 
	"audiolog", "audiologstory", 
	"automap", "automapplayerprofile", "automapproperties", "automapsoundprofile",
	"campaign"
	"env", 
	"font", "fontfx", 
	"fx", 
	"gameitem", 
	"globalfonttable", 
	"gorebehavior",
	"gorecontainer",
	"gorewounds", 
	"handsbobcycle", 
	//"highlightlos", 
	//"highlights", 
	"hitconfirmationsoundsinfo", 
	"hud", 
	"hudelement",
	"lightrig", 
	"lodgroup", 
	"material2", 
	//"md6def", 
	"modelasset", 
	"particle", 
	"particlestage", 
	"renderlayerdefinition", 
	"renderparm",
	"renderparmmeta", 
	"renderprogflag", 
	"ribbon2", 
	"rumble", 
	"soundevent", "soundpack", "soundrtpc", "soundstate", "soundswitch",
	"speaker", 
	"staticimage", 
	"swfresources", 
	"uianchor", 
	"uicolor", 
	"weaponreticle", 
	"weaponreticleswfinfo", 
	//"/entitydef/light/", 
	//"/entitydef/fx",
	"impacteffect", 
	"uiweapon", 
	"globalinitialwarehouse", 
	"globalshell", 
	"warehouseitem", 
	"warehouseofflinecontainer", 
	"tooltip", 
	"livetile",
	"tutorialevent", 
};

bool IsOnlineSafe(const ModFile& modfile, const bool Strict) {

	const ResourceType rt = modfile.typeenum;

	if(rt & (rt_image | rt_audio | rt_cswf))
		return true;

	// Edge Case: Technically only pvp entities files should be unsafe
	// However, elevated gameplay mods would demand all entities files
	// in other mods be designated unsafe
	if(rt == rt_compfile) {
		return !(Strict || modfile.assetPath._Starts_with("maps/game/pvp"));
	}

	if(rt != rt_rs_streamfile)
		return false;

	// Some rs_streamfiles don't start with this
	// Seems to just be megatexture materials and vertexpaints
	if(!modfile.assetPath._Starts_with("generated/decls/"))
		return true;

	const char* startPtr = modfile.assetPath.data() + 16; // Length of "generated/decls/"
	const char* ptr = startPtr;
	while(*ptr) {
		if(*ptr == '/')
			break;
		ptr++;
	}
	std::string declType(startPtr, ptr - startPtr);

	return ONLINE_SAFE_DECLTYPES.count(declType);
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