#pragma once

typedef unsigned uint32_t;

enum ResourceType : uint32_t
{
	rt_rs_streamfile   = 1 << 0,
	rt_entityDef       = 1 << 1,
	rt_logicClass      = 1 << 2,
	rt_logicEntity     = 1 << 3,
	rt_logicFX         = 1 << 4,
	rt_logicLibrary    = 1 << 5,
	rt_logicUIWidget   = 1 << 6,
	rt_mapentities     = 1 << 7,
	rt_image           = 1 << 8,
};

enum ResourceTypeComboFlags : uint32_t 
{
	rtc_logic_decl    = rt_logicClass | rt_logicEntity | rt_logicFX | rt_logicLibrary | rt_logicUIWidget,
	rtc_serialized    = rt_entityDef  | rt_mapentities | rtc_logic_decl,
	rtc_no_extension  = rt_entityDef  | rt_mapentities | rtc_logic_decl,
	rtc_streamdb_hash = rt_mapentities | rt_image
};