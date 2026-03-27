#include <chrono>
#include <cstdio>
#include "AtlanProfiling.h"

typedef std::chrono::steady_clock::time_point chronostamp_t;

size_t AtlanProfiling::CurrentTime()
{
	static_assert(sizeof(size_t) == sizeof(chronostamp_t));

	chronostamp_t chronotime = std::chrono::high_resolution_clock::now();

	return *reinterpret_cast<size_t*>(&chronotime);
}

void atlanstamp::log(bool update)
{
	chronostamp_t endtime = std::chrono::high_resolution_clock::now();
	chronostamp_t start   = *reinterpret_cast<chronostamp_t*>(&time);


	std::chrono::milliseconds duration = std::chrono::duration_cast<std::chrono::milliseconds>(endtime - start); 
	printf("[%s] %zu\n", id, duration.count()); 

	if (update) {
		time = *reinterpret_cast<size_t*>(&endtime);
	}
}
