#include "AtlanOodle.h"
#include "AtlanLogger.h"
#include "entityslayer/Oodle.h"

typedef std::filesystem::path fspath;

bool Oodle::AtlanOodleInit(const std::filesystem::path& gamedirectory)
{
	using namespace std::filesystem;

	fspath oo2core_chosenpath;
	const fspath oo2corepath_debug = "oo2core_9_win64.dll";
	const fspath oo2corepath_alt   = gamedirectory / "oo2core_8_win64.dll";
	const fspath oo2corepath       = gamedirectory / "oo2core_9_win64.dll";

	/*
	* Search for a useable Oodle Core DLL to load
	* Download one if it doesn't exist
	* (Doom Eternal ships with core8, so we can avoid downloading core9 entirely)
	*/
	if (exists(oo2corepath_alt)) {
		oo2core_chosenpath = oo2corepath_alt;
	}
	else if (exists(oo2corepath_debug)) {
		oo2core_chosenpath = oo2corepath_debug;
	}
	else {
		if (!exists(oo2corepath)) {
			// Because nobody ever needed a simple STL function to convert a string to a wide string....
			#define OODLE_URL    "https://github.com/WorkingRobot/OodleUE/raw/refs/heads/main/Engine/Source/Programs/Shared/EpicGames.Oodle/Sdk/2.9.10/win/redist/oo2core_9_win64.dll"
			#define OODLE_URL_W L"https://github.com/WorkingRobot/OodleUE/raw/refs/heads/main/Engine/Source/Programs/Shared/EpicGames.Oodle/Sdk/2.9.10/win/redist/oo2core_9_win64.dll"
			atlog << "Downloading " << oo2corepath << " from " << OODLE_URL << "\n";

			bool success = Oodle::Download(OODLE_URL_W, oo2corepath.wstring().c_str());
			if (!success) {
				atlog << "FATAL ERROR: Failed to download " << oo2corepath << "\n";
				return false;
			}
			atlog << "Download Complete (Oodle is a file decompression library)\n";
		}
		oo2core_chosenpath = oo2corepath;
	}

	if (!Oodle::init(oo2core_chosenpath.string().c_str())) {
		atlog << "FATAL ERROR: Failed to initialize " << oo2core_chosenpath << "\n";
		return false;
	}

	return true;
}
