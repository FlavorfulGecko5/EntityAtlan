#define _CRT_SECURE_NO_WARNINGS

#include "AtlanLogger.h"
#include <stdarg.h>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <errno.h>
#include <ctime>
#include <cstring>


/*
* Atlan Logging Library
*/

struct {
	FILE* cout = stdout;
	FILE* LogFile = nullptr;
} AtlanStreams;

bool AtlanLogger_Init(const char* path)
{
	if(AtlanStreams.LogFile)
		return true;

	bool replaceExisting = true;

	struct stat logstat;
	if (stat(path, &logstat) == 0) {
		replaceExisting = logstat.st_size > 100000;
	}
	else {
		if (errno == ENOENT) { 
			replaceExisting = true;
		} 
		else { 
			return false; 
		}
	}

	AtlanStreams.LogFile = fopen(path, replaceExisting ? "wb" : "ab");
	if(AtlanStreams.LogFile == nullptr)
		return false;

	if(replaceExisting) {
		atlog_file("Log file size threshold exceeded. Starting new log file");
	}

	char buffer[256];
	time_t timestamp;
	time(&timestamp);
	ctime_s(buffer, 256, &timestamp); // Newline is included at end of ctime string
	atlog_file("\n\n----------\n%.*s", (int)strlen(buffer) - 1, buffer);

	return true;
}

void AtlanLogger_Shutdown()
{
	if (AtlanStreams.LogFile) {
		fclose(AtlanStreams.LogFile);
		AtlanStreams.LogFile = nullptr;
	}
}

void __atlog_internal_heapbuffer(int LogFlags, const char* msg, va_list args, int RequiredLength) {

	const int buffer_max = RequiredLength + 16;
	char* buffer = new char[buffer_max];

	int stringlength = vsnprintf(buffer, buffer_max, msg, args);
	buffer[stringlength++] = '\n';
	buffer[stringlength] = '\0';

	if (LogFlags & LogCout) {
		fwrite(buffer, 1, stringlength, AtlanStreams.cout);
	}
	if (LogFlags & LogFile && AtlanStreams.LogFile) {
		fwrite(buffer, 1, stringlength, AtlanStreams.LogFile);
	}
	delete[] buffer;
}

void __atlog_internal(int LogFlags, const char* msg, va_list args) 
{
	const int BUFFER_MAX = 1024;
	const int PRINT_MAX = BUFFER_MAX - 1; // Leave space for a newline
	char buffer[BUFFER_MAX];

	int stringlength = vsnprintf(buffer, PRINT_MAX, msg, args);

	// Encoding Error or empty message
	if (stringlength < 1)
		return;

	// Returned length does not include the null char
	// If >= PRINT_MAX there was not enough room for the whole string
	if (stringlength >= PRINT_MAX) {
		__atlog_internal_heapbuffer(LogFlags, msg, args, stringlength);
		return;
		//stringlength = PRINT_MAX - 1;
	}

	// At this point stringLength is the index of the null char
	buffer[stringlength++] = '\n';
	buffer[stringlength] = '\0';

	if (LogFlags & LogCout) {
		fwrite(buffer, 1, stringlength, AtlanStreams.cout);
	}
	if (LogFlags & LogFile && AtlanStreams.LogFile) {
		fwrite(buffer, 1, stringlength, AtlanStreams.LogFile);
	}
}

#ifndef __ATLOG_DEBUG
void atlog(const char* msg, ...)
{
	va_list args;
	va_start(args, msg);
	__atlog_internal(LogAll, msg, args);
	va_end(args);
}

void atlog_file(const char* msg, ...) {
	va_list args;
	va_start(args, msg);
	__atlog_internal(LogFile, msg, args);
	va_end(args);
}
#else // We do NOT want this setting to be accidentally left on
#error "__ATLOG_DEBUG is set. Set to 0 after validation is complete."
#endif

void atlog_raw(const char* msg, long long msg_length, bool add_newline) {
	if (msg_length < 1) {
		msg_length = strlen(msg);

		if (msg_length < 1) {
			return;
		}
	}

	fwrite(msg, 1, msg_length, AtlanStreams.cout);
	fwrite(msg, 1, msg_length, AtlanStreams.LogFile);
	if (add_newline) {
		fputc('\n', AtlanStreams.cout);
		fputc('\n', AtlanStreams.LogFile);
	}
}