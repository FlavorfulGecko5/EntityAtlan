#pragma once
#include <string>

struct charbuffer_t;

namespace idcl {
	
	// WARNING: This function directly modifies the input encrypted data buffer
	bool blang_decrypt(char* data, size_t datalength, const char* filename, charbuffer_t& out_buffer);
	bool blang_tojson(const char* data, size_t datalength, std::string& out_json);
	bool blang_totxt(const char* data, size_t datalength, std::string& out_txt);

	bool blang_encrypt(char* data, size_t datalength, const char* filename, charbuffer_t& out_buffer);

	// This overload assumes the buffer's data already has the format of 
	// [salt][iv][unencrypted blang][hmac]
	// The hmac will be calculated
	bool blang_encrypt(charbuffer_t& unencrypted, const char* filename);


	struct blangmodargs {

		// Raw, encrypted blang file we want to modify
		// This *will* get modified
		char* blang = nullptr;
		size_t blanglength = 0;

		// Name of the blang file
		const char* blangname = nullptr;

		// Pointers to all csvs we're injecting into this
		// blang and their lengths. CSV data *may* get modified
		int numcsvs = 0;
		char** csvs = nullptr;
		size_t* csvlengths = nullptr;
	};

	bool blang_modify(blangmodargs args, charbuffer_t& output);
}