#include "Blang.h"
#include "atlan/AtlanLib.h"
#include "crypt/aescbc/aescbc.h"
#include "hash/sha256.h"
#include "io/BinaryReader.h"
#include "io/BinaryWriter.h"


bool idcl::blang_decrypt(char* data, size_t datalength, const char* filename, charbuffer_t& out_buffer) {
	
	char KEYDERIVE[] = "swapTeam\n";
	char SALT[12];
	char HMAC[32]; // At end of encrypted file, a checksum
	char   IV[16];
	char SHA_KEY[32]; // First 16 bytes are the key

	memcpy(SALT, data, sizeof(SALT));
	memcpy(IV, data + sizeof(SALT), sizeof(IV));

	{
		SHA256_CTX sha;
		sha256_init(&sha);
		sha256_update(&sha, (BYTE*)SALT, 12);
		sha256_update(&sha, (BYTE*)KEYDERIVE, 10); // null char intentionally included
		sha256_update(&sha, (BYTE*)filename, strlen(filename));
		sha256_update(&sha, (BYTE*)nullptr, 0);
		sha256_final(&sha, (BYTE*)SHA_KEY);
	}

	// Todo: Verify HMAC at end of offset

	const char* blangoffset = data + sizeof(SALT) + sizeof(IV);
	const size_t blanglength = datalength - sizeof(SALT) - sizeof(IV) - sizeof(HMAC);

	AES_ctx aes;
	AES_init_ctx_iv(&aes, (u8*)SHA_KEY, (u8*)IV);
	AES_CBC_decrypt_buffer(&aes, (u8*)blangoffset, blanglength);

	out_buffer.EnsureCapacity(blanglength);
	out_buffer.length = blanglength;
	memcpy(out_buffer.data, blangoffset, blanglength);

	return true;
}

bool idcl::blang_tojson(const char* data, size_t datalength, std::string& json)
{
	json.reserve(datalength);

	json.append(R"({ "strings": [
)");

	BinaryReader r(data, datalength);
	r.GoRight(8);

	u32 numstrings;
	check(r.ReadBig(numstrings));

	u32 len;
	const char* bytes;

	for (u32 i = 0; i < numstrings; i++) {
		json.append(
R"(	{
		"name": ")");

		r >> len >> len; // Skip hash
		check(r.ReadBytes(bytes, len));

		json.append(bytes, len);
		json.append(
R"(",
		"text": ")");

		r >> len;
		check(r.ReadBytes(bytes, len));

		for (u32 i = 0; i < len; i++) {
			char c = bytes[i];
			if (c == '\n')     json.append("\\n");
			else if(c == '\\') json.append("\\\\");
			else if(c == '"')  json.append("\\\"");
			else               json.push_back(c);
		}

		//json.append(bytes, len);
		json.append(
R"("
	},
)");
	
		// Feature Set - ignore this
		r >> len;
		r.GoRight(len);
	}

	json.pop_back(); json.pop_back(); // Pop newline and trailing comma (stupid json)
	json.append("]}");
	return true;
}
