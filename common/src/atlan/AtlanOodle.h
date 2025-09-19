#pragma once
#include <filesystem>

namespace Oodle
{
	// Performs all necessary operations for initializing Oodle for an Atlan application
	// Will download Oodle if no valid dll can be found. Results are logged appropriately
	// Returns true if successful. A return value of false indicates the program should be terminated
	bool AtlanOodleInit(const std::filesystem::path& gamedirectory);
}
