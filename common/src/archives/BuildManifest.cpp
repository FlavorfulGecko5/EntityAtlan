#include "crypt/aesgcm/aesgcm.h"
#include "atlan/AtlanLib.h"
#include "BuildManifest.h"

bool idcl::buildmanifest::read(const wchar_t* filepath, const size_t extrabytes) {
	FileReader reader;
	if (!reader.open(filepath))
		return false;

	const i64 filelength = reader.getlength();
	if (filelength < 0x5C) // Minimum length: IV + Tag + Signature
		return false;

	reader.read((char*)IV, sizeof(IV));
	json_length = filelength - 0x5C;

	int64_t bufferMin = json_length + extrabytes;

	if (bufferMin > json_max) {
		delete[] json;
		json = new char[bufferMin];
		json_max = bufferMin;
	}

	reader.read(json, json_length);
	reader.read((char*)TAG, sizeof(TAG));
	reader.read((char*)SIGNATURE, sizeof(SIGNATURE));
	reader.close();

	return true;
}

bool idcl::buildmanifest::decrypt() {

	const u8 KEY[] = "\x8B\x03\x1F\x6A\x24\xC5\xC4\xF3\x95\x01\x30\xC5\x7E\xF6\x60\xE9";
	const char* ADDITIONAL = "build-manifest";

	gcm_initialize();
	aes_gcm_decrypt(
		(u8*)json,
		(u8*)json, json_length,
		KEY, 0x10,
		IV, sizeof(IV),
		TAG, sizeof(TAG),
		(u8*)ADDITIONAL, strlen(ADDITIONAL)
	);

	// No whitespace should exist in the json
	return memcmp(json, R"({"hash":"sha1")", 14) == 0;
}

bool idcl::buildmanifest::read_and_decrypt(const wchar_t* filepath) {

	if(!read(filepath, 0))
		return false;

	return decrypt();
}

void idcl::buildmanifest::encrypt() {

	const u8 KEY[] = "\x8B\x03\x1F\x6A\x24\xC5\xC4\xF3\x95\x01\x30\xC5\x7E\xF6\x60\xE9";
	const char* ADDITIONAL = "build-manifest";

	aes_gcm_encrypt(
		(u8*)json,
		(u8*)json, json_length,
		KEY, 0x10,
		IV, sizeof(IV),
		TAG, sizeof(TAG),
		(u8*)ADDITIONAL, strlen(ADDITIONAL)
	);
	memset(SIGNATURE, 0xFF, sizeof(SIGNATURE));
}

#include <fstream>

void idcl::buildmanifest::write(const wchar_t* writeto) {
	std::ofstream outwriter(writeto, std::ios_base::binary);
	
	outwriter.write((char*)IV, sizeof(IV));
	outwriter.write(json, json_length);
	outwriter.write((char*)TAG, sizeof(TAG));
	outwriter.write((char*)SIGNATURE, sizeof(SIGNATURE));
	outwriter.close();

}

void idcl::buildmanifest::writejson(const wchar_t* writeto) {
	std::ofstream outwriter(writeto, std::ios_base::binary);
	outwriter.write(json, json_length);
	outwriter.close();
}


bool idcl::buildmanifest::modify(const wchar_t* filepath, const wchar_t* writeto, const char* NEWDATA, const size_t NEWLENGTH, bool writeUnencrypted) {


	if(!read(filepath, NEWLENGTH + 1000))
		return false;
	if(!decrypt())
		return false;


	// Edit the Manifest
	// After this loop ptr will be at the last popped brace
	char* ptr = json + json_length;
	int numbraces = 0;
	while (numbraces < 2) {
		ptr--;
		if (*ptr == '}')
			numbraces++;
	}
	memcpy(ptr, NEWDATA, NEWLENGTH);
	ptr += NEWLENGTH;
	json_length = ptr - json;

	if(writeUnencrypted) {
		writejson(writeto);
	}
	else {
		encrypt();
		write(writeto);
	}

	return true;
}