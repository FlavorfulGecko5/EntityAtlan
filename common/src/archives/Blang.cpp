#include "Blang.h"
#include "atlan/AtlanLib.h"
#include "crypt/aescbc/aescbc.h"
#include "hash/sha256.h"
#include "io/BinaryReader.h"
#include "io/BinaryWriter.h"
#include "atlan/AtlanLogger.h"


bool idcl::blang_decrypt(char* data, size_t datalength, const char* filename, charbuffer_t& out_buffer) {
	
	char KEYDERIVE[] = "swapTeam\n";
	char SALT[12]; // First 12 bytes of file
	char   IV[16]; // After salt
	char HMAC[32]; // At end of encrypted file, a checksum
	char SHA_KEY[32]; // First 16 bytes are the AES key

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

	{
		char EXPECTED_HMAC[32];
		memcpy(HMAC, data + datalength - sizeof(HMAC), sizeof(HMAC));
		hmac_sha256(SHA_KEY, sizeof(SHA_KEY), data, datalength - sizeof(HMAC), EXPECTED_HMAC);
		if(memcmp(HMAC, EXPECTED_HMAC, sizeof(HMAC)))
			atlog("WARNING: Blang Decode - HMAC mismatch");
	}

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

bool idcl::blang_encrypt(charbuffer_t& output, const char* filename)
{
	char KEYDERIVE[] = "swapTeam\n";
	char SHA_KEY[32];
	{
		SHA256_CTX sha;
		sha256_init(&sha);
		sha256_update(&sha, (BYTE*)output.data, 12); // Salt
		sha256_update(&sha, (BYTE*)KEYDERIVE, 10); // null char intentionally included
		sha256_update(&sha, (BYTE*)filename, strlen(filename));
		sha256_update(&sha, (BYTE*)nullptr, 0);
		sha256_final(&sha, (BYTE*)SHA_KEY);
	}

	AES_ctx aes;
	AES_init_ctx_iv(&aes, (u8*)SHA_KEY, (u8*)output.data + 12); // IV offset
	AES_CBC_encrypt_buffer(&aes, (u8*)output.data + 28, output.length - 32); // +28, -32 so we don't encrypt salt/iv/hmac
	hmac_sha256(SHA_KEY, sizeof(SHA_KEY), output.data, output.length - sizeof(SHA_KEY), output.data + output.length - 32);


	// TODO: Can't actually run this because it will decrypt our final blang text
	#ifdef _DEBUG
	//charbuffer_t testbuf;
	//check(idcl::blang_decrypt(output.data, output.length, filename, testbuf));
	//std::string csv;
	//check(idcl::blang_totxt(testbuf.data, testbuf.length, csv));
	#endif

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
			if (c == '\n')      json.append("\\n");
			else if (c == '\r') json.append("\\r");
			else if (c == '\\') json.append("\\\\");
			else if (c == '"')  json.append("\\\"");
			else if (c == '\t') json.append("\\t");
			else                json.push_back(c);
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

bool idcl::blang_totxt(const char* data, size_t datalength, std::string& txt)
{
	txt.reserve(datalength);

	u32 numstrings;

	BinaryReader r(data, datalength);
	r.GoRight(8);
	check(r.ReadBig(numstrings));

	u32 len;
	const char* bytes;

	for (u32 i = 0; i < numstrings; i++) {
		r >> len >> len; // Skip hash
		check(r.ReadBytes(bytes, len));

		txt.append(bytes, len);
		txt.append(" \"");

		r >> len;
		check(r.ReadBytes(bytes, len));

		for (u32 k = 0; k < len; k++) {
			char c = bytes[k];
			if (c == '\n')      txt.append("\\n");
			else if(c == '\r')  txt.append("\\r");
			else if (c == '\\') txt.append("\\\\");
			else if (c == '"')  txt.append("\\\"");
			else if (c == '\t') txt.append("\\t");
			else                txt.push_back(c);
		}
		txt.append("\"\n");

		// Ignore feature set
		r >> len;
		r.GoRight(len);
	}
	return true;
}

#include <string_view>
#include <unordered_map>

uint32_t blang_stringhash(const char* data, size_t len) {
	uint32_t fnvPrime = 0x01000193;
	uint32_t hash = 0x811C9DC5;

	for (const char* cptr = data, *cmax = data + len; cptr < cmax; cptr++) {
		char c = *cptr;
		if (c >= 'A' && c <= 'Z')
			c += 32;
		hash ^= c;
		hash *= fnvPrime;
	}

	// Hash is stored in big-endian
	return _byteswap_ulong(hash);
}

struct blangentry {
	std::string_view id;
	std::string_view text;
	std::string_view feature;
};

typedef std::unordered_map<u32, blangentry> blangmap_t;

bool blang_buildmap(blangmap_t& map, const char* blang_data, const size_t blang_length) {
	BinaryReader r(blang_data, blang_length);
	
	const char* bytes = nullptr;
	u32 numstrings, hash, strlength;

	r >> numstrings >> numstrings;
	r.ReadBig(numstrings);
	check(numstrings < 25000);
	map.reserve(numstrings + 1000);

	for (u32 i = 0; i < numstrings; i++) {
		r >> hash >> strlength;
		
		r.ReadBytes(bytes, strlength);
		check_debug(hash == blang_stringhash(bytes, strlength));

		blangentry& entry = map[hash];
		entry.id = std::string_view(bytes, strlength);

		r >> strlength;
		r.ReadBytes(bytes, strlength);
		entry.text = std::string_view(bytes, strlength);

		r >> strlength;
		r.ReadBytes(bytes, strlength);
		entry.feature = std::string_view(bytes, strlength);
	}

	return true;
}

#define isLetter(VAL) (((unsigned int)(VAL | 32) - 97) < 26U)
#define isNum(VAL) (((unsigned)VAL - '0') < 10u)

void blang_csvlog(const char* msg, const char* idstart, size_t idlength) {
	atlog("CSV ERROR: %s: %.*s", msg, (int)idlength, idstart);
}

#define langlog(MSG) blang_csvlog(MSG, idstart, idend - idstart)

bool blang_addcsv(blangmap_t& stringmap, char* csv, size_t csvlen) {

	char* cptr = csv, *cmax = csv + csvlen;

	const char* idstart = nullptr, *idend = nullptr;
	char* textstart = nullptr, *textend = nullptr;
	bool hasID = false;

	while (cptr < cmax) {

		// Skip any leading/trailing whitespace
		char c = *cptr;
		if (c <= ' ') {
			cptr++;
			continue;
		}

		// Read Identifier
		if (c != '#') {
			atlog("CSV ERROR: String names must begin with a '#'");
			return false;
		}
		idstart = cptr++;

		while (1) {
			if (cptr >= cmax) {
				atlog("CSV ERROR: Last string identifier has no text");
				return false;
			}

			c = *cptr;

			// Some vanilla strings have some unusual characters in their names
			//if (isLetter(c) || isNum(c) || c == '_' || c == '-' || c == '+' || c == '(' || c == ')' || c == '#') {
			//	cptr++;
			//	continue;
			//}
			if (c > ' ' && c != ',' && c != '"') {
				cptr++;
				continue;
			}

			idend = cptr;
			break;
		}

		// Skip anything in-between the identifier and the text
		// At this point cptr points to the first character after the id
		while (1) {
			if (cptr >= cmax) {
				langlog("String identifier without text");
				return false;
			}

			c = *cptr++;
			if (c == ',' || c == ' ' || c == '\t') {
				continue;
			}

			if (c == '"') {
				textstart = cptr;
				textend = cptr;
				break;
			}

			langlog("Unknown characters between ID and Text");
			return false;
		}


		// Read the text
		while (1) {
			if (cptr >= cmax) {
				langlog("Unterminated string text");
				return false;
			}

			c = *cptr++;

			if (c == '"') {
				break;
			}

			// Strategy: Eliminate need for any cloning of the text data
			// by replacing escape sequences with their characters directly in the source
			if (c == '\\') {
				if (cptr >= cmax) {
					langlog("Unterminated string text (escape sequence)");
					return false;
				}

				c = *cptr++;
				switch (c) {
					case 'n':  c = '\n'; break;
					case 'r':  c = '\r'; break;
					case '\\': c = '\\'; break;
					case '"':  c = '"'; break;
					case 't':  c = '\t'; break;

					default:
					langlog("Unknown escape sequence");
					return false;
				}
			}

			// Escape sequences will cause the textend to trail
			// behind cptr
			*textend++ = c;
			continue;

			// Do we want to error check? Ehh...may be convenient for multiline stings
			//if(c == '\n')
		}

		// Finally, Add string's data to the map
		blangentry& ENTRY = stringmap[blang_stringhash(idstart, idend - idstart)];
		std::string_view ORIGINAL = ENTRY.text;

		ENTRY.id = std::string_view(idstart, idend - idstart);
		ENTRY.text = std::string_view(textstart, textend - textstart);

		#ifdef _DEBUG
		//atlog("'%.*s' = '%.*s'", (int)(idend - idstart), idstart, (int)(textend - textstart), textstart);

		// For quickly assessing that we get identical results when injecting a copy
		// of the vanilla blang
		//check_debug(ORIGINAL == ENTRY.text);
		#endif
	}

	return true;
}

bool idcl::blang_modify(blangmodargs args, charbuffer_t& output)
{
	/* Decrypt vanilla blang */
	charbuffer_t decrypted;
	bool result = blang_decrypt(args.blang, args.blanglength, args.blangname, decrypted);
	if(!result)
		return false;

	/* Construct map of vanilla blang strings */
	blangmap_t stringmap;
	result = blang_buildmap(stringmap, decrypted.data, decrypted.length);
	if(!result)
		return false;


	/* Add modded strings to map */
	for (size_t i = 0; i < args.numcsvs; i++) {
		result = blang_addcsv(stringmap, args.csvs[i], args.csvlengths[i]);
		if(!result)
			return false;
	}

	/* 
	* Convert modded stringmap to blang file
	*/

	BinaryWriter writer;
	writer.EnsureMaxCapacity(decrypted.length * 1.2);

	// Write an empty salt and IV
	for(int i = 0; i < 7; i++)
		writer << (u32)0;

	writer << *(u64*)(decrypted.data);  // Copy first 8 bytes of vanilla file
	writer.WriteBig((u32)stringmap.size());
	for (const auto& pair : stringmap) {
		writer << (u32)pair.first;

		const blangentry& entry = pair.second;
		writer << (u32)entry.id.length();
		writer.WriteBytes(entry.id.data(), entry.id.length());

		writer << (u32)entry.text.length();
		writer.WriteBytes(entry.text.data(), entry.text.length());

		writer << (u32)entry.feature.length();
		writer.WriteBytes(entry.feature.data(), entry.feature.length());
	}
	// Add EOF padding according to patterns observed in the vanilla blang
	// THIS ACTUALLY MATTERS!!! AND THE BYTE VALUE MUST BE 3!!!
	size_t remcalc = writer.GetFilledSize() - 28;
	if (remcalc % 16 == 0) {
		for(int i = 0; i < 16; i++)
			writer <<(u8)3;
	}
	else {
		u64 rem = 16 - remcalc % 16;
		for(int i = 0; i < rem; i++)
			writer << (u8)3;
	}
	for(int i = 0; i < 4; i++) // Reserve space for the HMAC
		writer << (u64)0;

	/* Transfer ownership from writer to buffer */
	delete[] output.data;
	output.capacity = writer.GetFilledSize();
	output.length = writer.GetFilledSize();
	output.data = writer.Finalize();

	/* Encrypt Modified Blang */
	return blang_encrypt(output, args.blangname);
}
