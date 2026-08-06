#include <filesystem>
#include <fstream>
#include "io/BinaryReader.h"
#include "hash/HashLib.h"
#include "entityslayer/EntityParser.h"
#include "atlan/AtlanLogger.h"

#define DOSLENGTH 38
#define DOSOFFSET 0x4E

typedef std::filesystem::path fspath;

struct alphahash_t {
	char hash[16];
};

alphahash_t hash_to_alpha(uint64_t hash) {
	alphahash_t wrapper;
	char* buffer = wrapper.hash;
	char* const buffermax = buffer + sizeof(wrapper.hash);

	while (buffer < buffermax) {
		*buffer =  (char)(hash & 0xF)  + '0';

		if(*buffer > '9')
			*buffer = *buffer - '0' - 10 + 'a';
		buffer++;
		hash >>= 4;
	}

	return wrapper;
}

alphahash_t GetConfigHash(const fspath& configpath) {
	charbuffer_t buffer;
	FileReader::ReadFile(configpath.c_str(), buffer);
	uint64_t farmhash = HashLib::FarmHash64(buffer.data, buffer.length);
	return hash_to_alpha(farmhash);
}

bool parse_hexstring(char* buffer, const char* data, size_t len) {
	if(!len || len % 2) 
		return false;

	const char* datamax = data + len;

	while (data < datamax) {

		uint8_t halves[2] = {(uint8_t)*data, (uint8_t)*(data + 1)};
		data += 2;

		for (int i = 0; i < 2; i++) {
			if (halves[i] >= '0' && halves[i] <= '9') {
				halves[i] -= '0';
			}
			else if (halves[i] >= 'a' && halves[i] <= 'f') {
				halves[i] = halves[i] - 'a' + 10;
			}
			else if (halves[i] >= 'A' && halves[i] <= 'F') {
				halves[i] = halves[i] - 'A' + 10;
			}
			else {
				return false;
			}
		}

		*buffer++ = (halves[0] << 4) | halves[1];
	}
	return true;
}


enum shouldpatchflag {
	SPF_FATAL_ERROR   = 1 << 0,  // Should abort mod loading
	SPF_PATCH         = 1 << 2,  // We should patch this file
	SPF_DONT_PATCH    = 1 << 3,  // Don't need to patch this file 
};

shouldpatchflag Should_Run_Patcher(const fspath exepath, alphahash_t configHash) 
{
	if(!std::filesystem::exists(exepath))
		return SPF_DONT_PATCH;

	fspath backuppath = exepath;
	backuppath += ".backup";

	char dosstring[DOSLENGTH];

	std::ifstream exereader(exepath, std::ios_base::binary);
	exereader.seekg(DOSOFFSET, std::ios_base::beg);
	exereader.read(dosstring, DOSLENGTH);
	exereader.close();

	atlog("\nChecking %ls", exepath.filename().c_str());

	if (memcmp(dosstring, "This program cannot be run in DOS mode", DOSLENGTH) == 0) {
		
		atlog("File is unpatched. Creating backup");
		std::filesystem::copy(exepath, backuppath, std::filesystem::copy_options::overwrite_existing);
		return SPF_PATCH;
	}
	

	if (memcmp(dosstring, "ATLANMOD", 8) == 0) {

		if (memcmp(dosstring + 8, configHash.hash, sizeof(configHash.hash)) == 0) {
			atlog("File has latest patches");
			return SPF_DONT_PATCH;
		}
		else {

			atlog("Different set of patches applied. Re-patching");
			
			if (exists(backuppath)) {
				atlog("Restoring executable from backup");
				std::filesystem::copy(backuppath, exepath, std::filesystem::copy_options::overwrite_existing);
			}
			else {
				atlog("ERROR: Modded executable exists while backup is missing. Will not attempt patching. Please verify your game files.");
				return SPF_FATAL_ERROR;
			}
			return SPF_PATCH;
		}

	}

	atlog("ERROR: Corrupt game executable detected. Will not attempt patching. Please verify your game files.");
	return SPF_FATAL_ERROR;
}

struct gamepatch_t {
	std::string name;
	char* bytes = nullptr; // Vanilla binary sequence followed by patched binary sequence
	size_t numbytes = 0;   // Twice the length of the vanilla and patched binary sequences

	~gamepatch_t() {
		delete[] bytes;
	}
};

struct gamemd5_t {
	std::string name;
	HashLib::md5_t md5;
};

struct PatcherConfig_t {
	gamepatch_t* patchlist = nullptr;
	int numpatches = 0;

	gamemd5_t* checksums = nullptr;
	int numchecksums = 0;


	bool reverse = false;
	bool write = false;
	bool SkipExeHashCheck = false;

	~PatcherConfig_t() {
		delete[] patchlist;
		delete[] checksums;
	}
};

struct patchref {
	const char* nameptr    = nullptr;
	const uint8_t* vanilla = nullptr;
	const uint8_t* patched = nullptr;
	int length = 0;
	int applied = 0;
	uint8_t vanillafirst = 0;
	uint8_t patchedfirst = 0;
};

bool ReadPatcherConfig(PatcherConfig_t& cfg, const fspath& exepath, const fspath& configpath) {

	try {
		EntityParser parser(configpath.string(), ParsingMode::PERMISSIVE);
		const EntNode& root = *parser.getRoot();

		// Read Toggles
		if (!root["reverse"].ValueBool(cfg.reverse)) {
			atlog("ERROR: Failed to parse 'reverse' property");
			return false;
		}
		if (cfg.reverse) {
			atlog("Reverse Mode Activated");
		}
		if (!root["write"].ValueBool(cfg.write)) {
			atlog("ERROR: Failed to parse 'write' property");
			return false;
		}
		if (!cfg.write) {
			atlog("Executable Writing Disabled");
		}
		cfg.SkipExeHashCheck = &root["UNSAFE_MODE"] != EntNode::SEARCH_404;
		if(cfg.SkipExeHashCheck)
			atlog("Unsafe Mode Enabled");


		// Read Patches
		EntNode& patches = root["patches"];
		cfg.patchlist = new gamepatch_t[patches.getChildCount()];

		const std::string EXENAME = exepath.filename().string();

		for(const EntNode& p : patches) {

			if(p["games"].getValue().find(EXENAME) == -1)
				continue;

			gamepatch_t& g = cfg.patchlist[cfg.numpatches++];

			g.name = p["name"].getValueUQ();

			std::string_view hexstring = p["vanilla"].getValueUQ();
			g.numbytes = hexstring.length(); // 2 characters per byte, so string length == total number of bytes
			g.bytes = new char[g.numbytes];  // for vanilla + patched byte sequences

			bool result = parse_hexstring(g.bytes, hexstring.data(), hexstring.length());
			if (!result) {
				atlog("ERROR: Failed to parse vanilla hex string for patch %s", g.name.c_str());
				return false;
			}

			hexstring = p["patch"].getValueUQ();
			if (hexstring.size() != g.numbytes) {
				atlog("ERROR: Patch %s has different sized vanilla and patched codes", g.name.c_str());
				return false;
			}

			result = parse_hexstring(g.bytes + g.numbytes / 2, hexstring.data(), hexstring.length());
			if (!result) {
				atlog("ERROR: Failed to parse patch hex string for patch %s", g.name.c_str());
				return false;
			}
		}

		EntNode& checksums = root["checksums"];
		cfg.checksums = new gamemd5_t[checksums.getChildCount()];
		for (const EntNode& c : checksums) {
			gamemd5_t& md5 = cfg.checksums[cfg.numchecksums++];
			md5.name = c.getNameUQ();

			std::string_view hashstring = c.getValueUQ();
			if (sizeof(HashLib::md5_t) != hashstring.length() / 2) {
				atlog("ERROR: Config checksum size mismatch in %s", md5.name.c_str());
				return false;
			}

			bool result = parse_hexstring((char*)md5.md5.bytes, hashstring.data(), hashstring.length());
			if (!result) {
				atlog("ERROR: Failed to parse checksum %s", md5.name.c_str());
				return false;
			}
		}

		return true;
	}
	catch (...) {
		atlog("ERROR: Failed to read AtlanPatcher.txt");
		return false;
	}
}

bool Run_Executable_Patcher(const fspath& exepath, const fspath& configpath)
{
	//atlog("\nRunning Atlan Executable Patcher\n---");

	PatcherConfig_t cfg;
	if (!ReadPatcherConfig(cfg, exepath, configpath)) {
		return false;
	}

	std::vector<patchref> reflist;
	for (int i = 0; i < cfg.numpatches; i++) {
		const gamepatch_t& p = cfg.patchlist[i];
		patchref ref;
		ref.nameptr = p.name.c_str();

		ref.length = (int)p.numbytes / 2;
		if (cfg.reverse) {
			ref.patched = (uint8_t*)p.bytes;
			ref.vanilla = ref.patched + ref.length;
		}
		else {
			ref.vanilla = (uint8_t*)p.bytes; 
			ref.patched = ref.vanilla + ref.length;
		}
		ref.vanillafirst = *ref.vanilla;
		ref.patchedfirst = *ref.patched;

		reflist.push_back(ref);
	}

	      patchref* const patches    = reflist.data();
	const patchref* const patchesmax = patches + reflist.size();

	//atlog("Beginning scanning");

	charbuffer_t exebytes;
	if (!FileReader::ReadFile(exepath.c_str(), exebytes)) {
		atlog("Error reading executable into memory");
		return false;
	}

	#if 0 // Restoring from backup should make this codepath impossible
	if (memcmp(exebytes.data + DOSOFFSET, "ATLANMOD", 8) == 0) {
		atlog("Executable is previously patched. Skipping checksum validation");
		cfg.SkipExeHashCheck = true;
	}
	#endif

	if (!cfg.SkipExeHashCheck) {
		bool FoundMD5 = false;
		const HashLib::md5_t PrePatchMD5 = HashLib::md5(exebytes.data, exebytes.length);
		for (int i = 0; i < cfg.numchecksums; i++) {
			if (memcmp(PrePatchMD5.bytes, cfg.checksums[i].md5.bytes, sizeof(HashLib::md5_t)) == 0) {
				atlog("Matching Checksum '%s'", cfg.checksums[i].name.c_str());
				FoundMD5 = true;
				break;
			}
		}
		if (!FoundMD5) {
			atlog("ERROR: Executable has an unknown MD5 Checksum");
			return false;
		}
	}

	atlog("Attempting %d patches", cfg.numpatches);
	
	for(uint8_t* exe = (uint8_t*)exebytes.data, *const exemax = exe + exebytes.length; exe < exemax; exe++) {
		for (patchref* p = patches; p < patchesmax; p++) {

			if ( (size_t)(exemax - exe) < p->length) {
				continue;
			}
			
			if (*exe == p->vanillafirst) {
				
				if (memcmp(exe, p->vanilla, p->length) == 0) {

					if (p->applied) {
						atlog("ERROR: vanilla form of patch '%s' found multiple times.", p->nameptr);
						return false;
					}

					memcpy(exe, p->patched, p->length);

					atlog("Applied patch '%s'", p->nameptr);

					p->applied++;
					
					// Subtract 1 to account for rest of loop operations
					exe += p->length - 1;
					break;
				}
			}

			if (*exe == p->patchedfirst) {
				
				if (memcmp(exe, p->patched, p->length) == 0) {
					
					if (p->applied) {
						atlog("ERROR: patch signature for '%s' found multiple times", p->nameptr);
						return false;
					}

					atlog("Patch '%s' already applied", p->nameptr);

					p->applied++;

					exe += p->length - 1;
					break;
				}
			}
		}
	}


	int failedpatches = 0;
	for (size_t i = 0; i < reflist.size(); i++) {
		if (!patches[i].applied) {
			failedpatches++;
			atlog("ERROR: Failed to apply patch: %s", patches[i].nameptr);
		}
	}
	if (failedpatches) {
		return false;
	}

	HashLib::md5_t postpatchhash = HashLib::md5(exebytes.data, exebytes.length);
	#ifndef _DEBUG
	atlog("Post Patch Hash: %llx %llx", *(u64*)postpatchhash.bytes, *(u64*)(postpatchhash.bytes + 8) );
	#else
	for (int i = 0; i < 16; i++) {
		printf("%02hhx", postpatchhash.bytes[i]);
	}
	printf("\n");
	#endif

	alphahash_t hashwrapper = GetConfigHash(configpath);

	// Edit the DOS stub
	memcpy(exebytes.data + DOSOFFSET, "ATLANMOD", 8);
	memcpy(exebytes.data + DOSOFFSET + 8, hashwrapper.hash, sizeof(hashwrapper.hash));

	// Write out the file
	if (cfg.write) {
		std::ofstream outwriter(exepath, std::ios_base::binary);
		outwriter.write(exebytes.data, exebytes.length);
		if (!outwriter.good()) {
			atlog("Error saving patched exe to file");
			return false;
		}
		outwriter.close();
	}

	return true;
}

bool Patcher_DownloadConfig(const fspath& configpath);

// If false is returned, we should terminate mod loading
bool Executable_Patcher_Main(const fspath& gamedir)
{
	// For debugging purposes, check the working dir before the game dir
	fspath configpath = "AtlanPatcher.txt";
	if (!std::filesystem::exists(configpath)) {
		configpath = gamedir / "AtlanPatcher.txt";

		if (!std::filesystem::exists(configpath)) {
			atlog("WARNING: Missing AtlanPatcher.txt");
			if (!Patcher_DownloadConfig(configpath)) {
				return false;
			}
		}
	}

	alphahash_t confighash = GetConfigHash(configpath);

	fspath exes[] = {
		gamedir / "DOOMTheDarkAges.exe",
		gamedir / "DOOMEternalx64vk.exe",
        gamedir / "doomSandBox/DOOMSandBox64vk.exe"
	};

	for (int i = 0; i < sizeof(exes) / sizeof(exes[0]); i++) {

		switch (Should_Run_Patcher(exes[i], confighash)) {
			case SPF_FATAL_ERROR: return false;
			case SPF_DONT_PATCH:  continue;
			case SPF_PATCH:       break;
		}

		// Initial Run with the existing config
		if(Run_Executable_Patcher(exes[i], configpath))
			continue;

		atlog("Initial Patch attempt failed. Downloading new config and trying again");

		if(!Patcher_DownloadConfig(configpath))
			return false;
		
		confighash = GetConfigHash(configpath);

		if(!Run_Executable_Patcher(exes[i], configpath))
			return false;
	}

	return true; 
}

#define CONFIG_URL      "https://dcealopez.es/AtlanPatcher.txt"
#define CONFIG_MD5_URL  "https://dcealopez.es/AtlanPatcher.txt.md5"
#define CONFIG_MD5_PATH "AtlanPatcher.txt.md5"
#define WIN32_LEAN_AND_MEAN
#include <urlmon.h>


bool Patcher_DownloadConfig(const fspath& configpath) {
	static bool TriedDownloadingOnce = false;
	if (TriedDownloadingOnce) {
		return false;
	}
	TriedDownloadingOnce = true;

	// Need to do an existence check incase user deleted the config file
	// or it otherwise doesn't exist
	if (std::filesystem::exists(configpath)) {
		atlog("Checking for config file updates");
		HRESULT result = URLDownloadToFile(NULL, TEXT(CONFIG_MD5_URL), TEXT(CONFIG_MD5_PATH), 0, NULL);
		if (result != S_OK) {
			atlog("FATAL ERROR: Update check failed");
			return false;
		}

		charbuffer_t hostedMd5File;
		FileReader::ReadFile(CONFIG_MD5_PATH, hostedMd5File);
		std::filesystem::remove(CONFIG_MD5_PATH);
		if (hostedMd5File.length != sizeof(HashLib::md5_t) * 2) {
			atlog("ERROR: Incorrectly sized .md5 file");
			return false;
		}

		HashLib::md5_t hostedMd5;
		if (!parse_hexstring((char*)hostedMd5.bytes, hostedMd5File.data, hostedMd5File.length)) {
			atlog("ERROR: Failed to parse .md5 file");
			return false;
		}

		charbuffer_t configbytes;
		FileReader::ReadFile(configpath.c_str(), configbytes);
		HashLib::md5_t realMd5 = HashLib::md5(configbytes.data, configbytes.length);
		if (!memcmp(realMd5.bytes, hostedMd5.bytes, sizeof(HashLib::md5_t))) {
			atlog("Latest config already downloaded");
			return false;
		}
	}

	atlog("Downloading AtlanPatcher.txt from " CONFIG_URL);
	HRESULT result = URLDownloadToFile(NULL, TEXT(CONFIG_URL), configpath.c_str(), 0, NULL);
	if (result != S_OK) {
		atlog("FATAL ERROR: Failed to download patcher config");
		return false;
	}
	return true;
}