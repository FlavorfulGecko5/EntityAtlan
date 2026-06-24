#define _CRT_SECURE_NO_WARNINGS


#include "AtlanLib.h"
#include "AtlanLogger.h"

#include <cstdio>
#include <cstdlib>

/*
* Atlan Assert Functions
*/

void __atlan_assert_failed(const char* op, const char* file, const int line, const char* extra)
{
	atlog("[%s][%d] %s %s", file, line, op, extra);
	AtlanLogger_Shutdown();
	abort();
}


/*
* Atlan Buffers
*/

charbuffer_t::~charbuffer_t() {
	delete[] data;
}

void charbuffer_t::EnsureCapacity(const size_t requiredCapacity) {
	if (capacity < requiredCapacity) {
		delete[] data;

		capacity = requiredCapacity;
		data = new char[requiredCapacity];
		length = 0;
	}
}



/*
* Atlan File Reader
*/

void FileReader::ErrorDetected() {
	numErrors++;

	fileposition_t pos = FileReader::getposition();
	check_debug(0, "FileReader::ErrorDetected");
}

bool FileReader::noerrors() const {
	return numErrors == 0;
}

void FileReader::close() {
	if (fptr) {
		fclose(fptr);
		fptr = nullptr;
	}
}

FileReader::~FileReader() {
	FileReader::close();
}

bool FileReader::open(const char* filepath) {
	FileReader::close();

	fptr = fopen(filepath, "rb");

	if (fptr) {
		numErrors = 0;
		return true;
	}

	ErrorDetected();
	return false;
}

bool FileReader::open(const wchar_t* filepath) {
	FileReader::close();

	fptr = _wfopen(filepath, L"rb");

	if (fptr) {
		numErrors = 0;
		return true;
	}

	ErrorDetected();
	return false;
}

bool FileReader::isopen() const {
	return fptr != nullptr;
}

bool FileReader::read(char* buffer, size_t length) {
	size_t bytesread = fread(buffer, 1, length, fptr);

	if (bytesread == length) {
		return true;
	}

	ErrorDetected();
	return false;
}

bool FileReader::seek(fileposition_t position) {
	int success = _fseeki64(fptr, position, SEEK_SET);

	if(success == 0)
		return true;

	ErrorDetected();
	return false;
}

fileposition_t FileReader::getposition() const {
	return _ftelli64(fptr);
}

i64 FileReader::getlength() {
	const fileposition_t lastposition = _ftelli64(fptr);
	_fseeki64(fptr, 0, SEEK_END);

	const fileposition_t filelength   = _ftelli64(fptr);
	_fseeki64(fptr, lastposition, SEEK_SET);

	return filelength;
}

