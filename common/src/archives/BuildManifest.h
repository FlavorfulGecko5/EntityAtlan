#pragma once
#include "atlan/AtlanLib.h"

namespace idcl {

	struct buildmanifest {

		u8 IV[0xC];
		u8 TAG[0x10];
		u8 SIGNATURE[0x40];

		char* json = nullptr;
		int64_t json_length = 0;
		int64_t json_max = 0;

		~buildmanifest() {
			delete[] json;
		}

		// Extrabytes to allocate to the buffer
		bool read(const wchar_t* filepath, size_t extrabytes);
		bool decrypt();
		bool read_and_decrypt(const wchar_t* filepath);

		void encrypt();
		void write(const wchar_t* writeto);
		void writejson(const wchar_t* writeto);
		bool modify(const wchar_t* readfrom, const wchar_t* writeto, const char* newdata, const size_t newlength, bool writeUnencrypted);
	};
}