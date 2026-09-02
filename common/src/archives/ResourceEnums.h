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
	rt_audio           = 1 << 9,
	rt_slug_font       = 1 << 10,
	rt_file            = 1 << 11,
	rt_compfile        = 1 << 12,
	rt_binaryFile      = 1 << 13,
	rt_baseModel       = 1 << 14,
	rt_strandsHair     = 1 << 15
};

enum ResourceTypeComboFlags : uint32_t 
{
	rtc_logic_decl    = rt_logicClass | rt_logicEntity | rt_logicFX | rt_logicLibrary | rt_logicUIWidget,
	rtc_serialized    = rt_entityDef  | rt_mapentities | rtc_logic_decl,
	rtc_no_extension  = rt_entityDef  | rt_mapentities | rtc_logic_decl | rt_audio | rt_slug_font,
	rtc_last_number   = rt_audio,
	rtc_query_dbhash  = rt_mapentities | rt_compfile,
	rtc_assign_dbhash = rt_image | rt_baseModel,
	rtc_deferload     = rt_image | rt_baseModel | rt_strandsHair
};