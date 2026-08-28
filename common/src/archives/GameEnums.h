#pragma once

enum gamebit_t : unsigned char {
	game_none     = 0,
	game_darkages = 1 << 0, // Doom The Dark Ages
	game_eternal  = 1 << 1, // Doom Eternal
	game_all      = 0xFF
};