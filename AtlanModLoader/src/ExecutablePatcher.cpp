#include <filesystem>
#include <fstream>
#include "io/BinaryReader.h"
#include "hash/HashLib.h"
#include "entityslayer/EntityParser.h"
#include "atlan/AtlanLogger.h"

#define DOSLENGTH 38
#define DOSOFFSET 0x4E
#define CONFIGPATH "AtlanPatcher.txt"

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

bool parse_hexstring(std::vector<uint8_t>& buffer, const char* data, size_t len) {
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

		buffer.push_back( (halves[0] << 4) | halves[1]);
	}
	return true;
}

bool Should_Run_Patcher(const fspath exepath, alphahash_t configHash) 
{
	fspath backuppath = exepath;
	backuppath += ".backup";

	if(!std::filesystem::exists(exepath))
		return false;

	std::ifstream exereader(exepath, std::ios_base::binary);
	exereader.seekg(DOSOFFSET, std::ios_base::beg);
	char dosstring[DOSLENGTH];

	exereader.read(dosstring, DOSLENGTH);

	if (memcmp(dosstring, "This program cannot be run in DOS mode", DOSLENGTH) == 0) {
		
		atlog("%ls is unpatched. Creating backup", exepath.c_str());
		std::filesystem::copy(exepath, backuppath, std::filesystem::copy_options::overwrite_existing);
		return true;
	}
	

	if (memcmp(dosstring, "ATLANMOD", 8) == 0) {

		if (memcmp(dosstring + 8, configHash.hash, sizeof(configHash.hash)) == 0) {
			atlog("%ls has latest patches", exepath.c_str());
			return false;
		}
		else {

			atlog("%ls has a different set of patches applied. Re-patching", exepath.c_str());
			
			if (exists(backuppath)) {
				atlog("Restoring executable from backup");
				std::filesystem::copy(backuppath, exepath, std::filesystem::copy_options::overwrite_existing);
			}
			else {
				atlog("WARNING: Executable backup not found. Creating non-vanilla backup");
				std::filesystem::copy(exepath, backuppath, std::filesystem::copy_options::overwrite_existing);
			}
			return true;
		}

	}

	atlog("ERROR: Corrupt game executable detected. Will not attempt patching.");
	return false;
}

struct gamepatch {
	std::string name;
	std::vector<uint8_t> hexdata; // Vanilla binary sequence followed by patched binary sequence
};

struct PatcherConfig {
	std::vector<gamepatch> patchlist;
	bool REVERSE = false;
};

struct patchref {
	const uint8_t* vanilla = nullptr;
	const uint8_t* patched = nullptr;
	int length = 0;
	int applied = 0;
	uint8_t vanillafirst = 0;
	uint8_t patchedfirst = 0;
};

bool GetPatchList(std::vector<gamepatch>& patchlist, bool& REVERSE) {

	patchlist.reserve(10);

	try {
		EntityParser parser(CONFIGPATH, ParsingMode::PERMISSIVE);

		const EntNode& root = *parser.getRoot();

		if (!root["reverse"].ValueBool(REVERSE)) {
			atlog("ERROR: Failed to parse 'reverse' property");
			return false;
		}

		if (REVERSE) {
			atlog("Reverse Mode Activated");
		}
		

		const EntNode& patches = root["patches"];

		for (int i = 0; i < patches.getChildCount(); i++) {

			patchlist.emplace_back();
			gamepatch& g = patchlist.back();
			const EntNode& p = patches[i];

			g.name = p["name"].getValueUQ();
			//atlog("Reading Patch Definition '%s'", g.name.c_str());

			std::string_view hexstring = p["vanilla"].getValueUQ();
			bool result = parse_hexstring(g.hexdata, hexstring.data(), hexstring.length());
			if (!result) {
				atlog("Failed to parse vanilla hex string for patch %s", g.name.c_str());
				return false;
			}

			hexstring = p["patch"].getValueUQ();
			if (hexstring.size() != g.hexdata.size() * 2) {
				atlog("Patch %s has different sized vanilla and patched codes", g.name.c_str());
				return false;
			}

			result = parse_hexstring(g.hexdata, hexstring.data(), hexstring.length());
			if (!result) {
				atlog("Failed to parse patch hex string for patch %s", g.name.c_str());
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


// TODO: Refactor is not finished. Still need to adjust config parsing
// to account for multiple games
void Run_Executable_Patcher(const fspath& exepath)
{
	bool REVERSE = false;

	atlog("\n\nRunning Atlan Executable Patcher\n-----");

	std::vector<gamepatch> patchlist;
	if (!GetPatchList(patchlist, REVERSE)) {
		return;
	}

	std::vector<patchref> reflist;
	for (const gamepatch& p : patchlist) {
		patchref ref;

		ref.length = p.hexdata.size() / 2;
		if (REVERSE) {
			ref.patched = p.hexdata.data();
			ref.vanilla = ref.patched + ref.length;
		}
		else {
			ref.vanilla = p.hexdata.data(); 
			ref.patched = ref.vanilla + ref.length;
		}
		ref.vanillafirst = *ref.vanilla;
		ref.patchedfirst = *ref.patched;

		reflist.push_back(ref);

		//for (int i = 0; i < ref.length; i++) {
		//	atlog_deprecated << ref.patched[i] << " ";
		//}
		//atlog_deprecated << "\n";
	}

	patchref* patches = reflist.data();

	atlog("Beginning scanning");

	BinaryOpener exeopener(exepath.string());
	
	for(uint8_t* exe = (uint8_t*)exeopener.GetEditable(), *const exemax = exe + exeopener.len(); exe < exemax; exe++) {
		for (size_t i = 0; i < reflist.size(); i++) {

			if ( (size_t)(exemax - exe) < patches[i].length) {
				continue;
			}
			
			if (*exe == patches[i].vanillafirst) {
				
				if (memcmp(exe, patches[i].vanilla, patches[i].length) == 0) {

					if (patches[i].applied) {
						atlog("ERROR: vanilla form of patch '%s' found multiple times.", patchlist[i].name.c_str());
						return;
					}

					memcpy(exe, patches[i].patched, patches[i].length);

					atlog("Applied patch '%s'", patchlist[i].name.c_str());

					patches[i].applied++;
					
					// Subtract 1 to account for rest of loop operations
					exe       += patches[i].length - 1;
					break;
				}
			}

			if (*exe == patches[i].patchedfirst) {
				
				if (memcmp(exe, patches[i].patched, patches[i].length) == 0) {
					
					if (patches[i].applied) {
						atlog("ERROR: patch signature for '%s' found multiple times", patchlist[i].name.c_str());
						return;
					}

					atlog("Patch '%s' already applied", patchlist[i].name.c_str());

					patches[i].applied++;

					exe += patches[i].length - 1;
					break;
				}
			}
		}
	}


	int failedpatches = 0;
	for (size_t i = 0; i < reflist.size(); i++) {
		if (!patches[i].applied) {
			atlog("Failed to apply patch: %s", patchlist[i].name.c_str());
		}
	}
	if (failedpatches) {
		atlog("Cannot proceed because 1 or more patches have failed to apply");
		return;
	}


	BinaryOpener hashopen(CONFIGPATH);
	uint64_t farmhash = HashLib::FarmHash64(hashopen.data(), hashopen.len());
	alphahash_t hashwrapper = hash_to_alpha(farmhash);

	// Edit the DOS stub
	memcpy(exeopener.GetEditable() + DOSOFFSET, "ATLANMOD", 8);
	memcpy(exeopener.GetEditable() + DOSOFFSET + 8, hashwrapper.hash, sizeof(hashwrapper.hash));

	// Write out the file
	std::ofstream outwriter("../input/darkages/injectortest/test.bin", std::ios_base::binary);
	outwriter.write(exeopener.data(), exeopener.len()); // exe ptr is incremented...do not use here
	outwriter.close();
}

bool Executable_Patcher_Main(const fspath& gamedir)
{
	if (!std::filesystem::exists(CONFIGPATH)) {
		atlog("ERROR: Missing " CONFIGPATH);
		return false;
	}

	alphahash_t configHash;
	{
		BinaryOpener rawconfig(CONFIGPATH);
		uint64_t hash = HashLib::FarmHash64(rawconfig.data(), rawconfig.len());
		configHash = hash_to_alpha(hash);
	}

	const fspath exes[] = {
		gamedir / "DOOMTheDarkAges.exe",
		gamedir / "DOOMEternalx64vk.exe",
		gamedir / "doomSandBox/DOOMSandBox64vk.exe"
	};


	for (int i = 0; i < sizeof(exes) / sizeof(exes[0]); i++) {
		if (Should_Run_Patcher(exes[i], configHash) == false)
			continue;

		// TODO NOT FINISHED
	}


}