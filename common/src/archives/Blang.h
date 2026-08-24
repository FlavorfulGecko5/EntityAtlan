#pragma once
#include <string>

struct charbuffer_t;

namespace idcl {
	
	// WARNING: This function directly modifies the input encrypted data buffer
	bool blang_decrypt(char* data, size_t datalength, const char* filename, charbuffer_t& out_buffer);
	bool blang_tojson(const char* data, size_t datalength, std::string& out_json);
}