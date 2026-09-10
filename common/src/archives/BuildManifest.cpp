#include "crypt/aesgcm/aesgcm.h"
#include "atlan/AtlanLib.h"
#include "atlan/AtlanLogger.h"
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

	// IMPORTANT: FF'ing the signature bytes is how we determine
	// that the build-manifest is modded
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


bool idcl::buildmanifest::modify_deprecated(const wchar_t* filepath, const wchar_t* writeto, const char* NEWDATA, const size_t NEWLENGTH, bool writeUnencrypted) {


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

bool idcl::buildmanifest::ismodded(const wchar_t* filepath)
{
	uint64_t signature[8];
	signature[0] = 0;

	FileReader reader;
	reader.open(filepath);
	reader.seekend(-64);
	reader.read((char*)signature, 64);
	reader.close();

	// A modded manifest will have it's signature FF'd
	for (int i = 0; i < 8; i++) {
		if (signature[i] != -1)
			return false;
	}
	return true;
}

#include <filesystem>
#include <string>

typedef std::filesystem::path fspath;

// Builds a simplified build-manifest json containing only verified files
bool idcl::buildmanifest::buildsimplemanifest(const wchar_t* binpath, bool RewriteBin)
{
	atlog("Creating simplified build manifest");
	buildmanifest manifest;
	if(!manifest.read_and_decrypt(binpath))
		return false;

	const char* JSON_START = R"({"hash":"sha1","files":{)";

	const fspath outpath = fspath(binpath).parent_path() / "build-manifest-simple.bin";
	std::ofstream writer(outpath, std::ios_base::binary);
	writer << JSON_START;

	// We take advantage of the fact the vanilla json has no whitespace
	// to simplify our culling process
	const char* const ptrmax = manifest.json + manifest.json_length;
	char* ptr = manifest.json + strlen(JSON_START);
	while(ptr < ptrmax)
	{
		check(*ptr++ == '"');
		const char* namestart = ptr;
		while (*ptr != '"') {
			ptr++;
		}

		std::string filename(namestart, ptr - namestart);

		while(*ptr != '}')
			ptr++;

		const char* blockend = ++ptr;

		// StreamDB files comprise 80% of the build-manifest, despite never being verified.
		// PackageMapSpec and meta.resources will need to have their entries customized, so we remove them too
		if (filename.find(".streamdb") == -1 && filename != "packagemapspec.json" && filename != "meta.resources") {
			writer << '"';
			writer.write(namestart, blockend - namestart);
			writer << ",";
		}

		if(*ptr == ',')
			ptr++;
		else {
			check(*ptr++ == '}');
			check(*ptr++ == '}');
			check(ptr == ptrmax);

			// Erase the last trailing comma (stupid json)
			writer.seekp(-1, std::ios_base::end);
			writer << "}}";
		}
	}
	
	writer.close();

	if (RewriteBin) {
		manifest.encrypt();
		manifest.write(binpath);
	}

	return true;
}

void buildmanifest_addinfo(const fspath& basedir, const fspath& relativepath, std::string& data) {

	//std::error_code ec;
	size_t fileSize = std::filesystem::file_size(basedir / relativepath);
	const int chunksize = 65536;
	size_t numchunks = fileSize / chunksize + (fileSize % chunksize ? 1 : 0);

	data.reserve(data.length() + numchunks * 42 + 250);

	data.append(",\"");
	data.append(relativepath.string());
	data.append("\":{\"fileSize\":");
	data.append(std::to_string(fileSize));
	data.append(",\"chunkSize\":65536,\"hashes\":[");

	for (size_t i = 0; i < numchunks; i++) {
		data.append("\"ffffffffffffffffffffffffffffffffffffffff\",");
	}

	data.pop_back(); // Trailing comma
	data.append("]}");
}

bool idcl::buildmanifest::modfromcache(modargs_t args)
{
	const fspath basedir = fspath(args.gamedir) / "base";
	const fspath binpath = basedir / "build-manifest.bin";
	const fspath backuppath = basedir / "build-manifest.bin.backup";
	const fspath simplepath = basedir / "build-manifest-simple.bin";

	gcm_initialize();

	// In case something gets messed up, allow regeneration of simplified manifest
	// from the backup file
	if (!std::filesystem::exists(simplepath)) {
		if (!buildsimplemanifest(backuppath.c_str(), false)) {
			atlog("ERROR: Failed to create simple build manifest");
			return false;
		}
	}

	// packagemapspec.json is a verified file
	// meta.resources is not, but there's some strange behavior that causes information
	// about it to not get logged with fs_debug 1. So for safety we're adding it anyways
	std::string newdata;
	buildmanifest_addinfo(basedir, L"packagemapspec.json", newdata);
	buildmanifest_addinfo(basedir, L"modarchives/common_mod.resources", newdata);
	buildmanifest_addinfo(basedir, L"meta.resources", newdata);
	newdata.append("}}");

	idcl::buildmanifest man;

	/* Load the simplified json into the buffer */
	FileReader reader;
	reader.open(simplepath.c_str());
	man.json_length = reader.getlength();
	man.json_max = man.json_length + newdata.length() + 1000;
	man.json = new char[man.json_max];
	reader.read(man.json, man.json_length);
	reader.close();

	/* Chop the 2 ending braces 
		After this ptr will be located at the last-popped brace */
	char* ptr = man.json + man.json_length;
	int numbraces = 0;
	while (numbraces < 2) {
		ptr--;
		if (*ptr == '}')
			numbraces++;
	}

	/* Copy new entries into the buffer */
	memcpy(ptr, newdata.data(), newdata.length());
	ptr += newdata.length();
	man.json_length = ptr - man.json;

	man.encrypt();
	man.write(binpath.c_str());
	return true;
}


