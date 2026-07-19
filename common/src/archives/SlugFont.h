#pragma once
#include "atlan/AtlanLib.h"

namespace idcl {
	bool make_slugfont(const wchar_t* filepath, charbuffer_t& output);
	bool make_slugfont(const char* slugdata, const size_t sluglength, charbuffer_t& output);
}