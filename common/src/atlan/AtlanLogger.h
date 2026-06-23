#pragma once

/*
* Atlan Logging Library
*/

enum AtlanLogFlags {
	LogCout = 1 << 0,
	LogFile = 1 << 1,

	LogAll  = LogCout | LogFile
};

// MSVC has no way of performing compile-time format string validation
// on user-defined functions like it does with printf
// Hence we need to do this if we want to validate our logging functions
// Users can either define this here for global error checking, or in
// individual source files

//#define __ATLOG_DEBUG

#ifdef __ATLOG_DEBUG
#include <cstdio>
#define atlog(...) printf(__VA_ARGS__)
#define atlog_file(...) printf(__VA_ARGS__)
#else
void atlog(const char* msg, ...);
void atlog_file(const char* msg, ...);
#endif

void atlog_raw(const char* msg, long long msg_length = 0, bool add_newline = true);

bool AtlanLogger_Init(const char* path);
void AtlanLogger_Shutdown();