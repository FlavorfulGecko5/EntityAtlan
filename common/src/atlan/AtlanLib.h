#pragma once

/*
* Useful Typedefs
*/

typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        i8;
typedef short              i16;
typedef int                i32;
typedef long long          i64;
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;


/*
* Commonly used buffer functions
*/
extern "C" {
	size_t __cdecl strlen(char const* _Str);
	int    __cdecl strcmp(char const* _Str1, char const* _Str2);
	int    __cdecl memcmp(void const* _Buf1, void const* _Buf2, size_t _Size);
	void*  __cdecl memcpy(void* _Dst, void const* _Src, size_t _Size);
	void*  __cdecl memset(void* _Dst, int _Val, size_t _Size);
}

/*
* Atlan Assert Functions
*/

[[noreturn]]
void __atlan_assert_failed(const char* op, const char* file, const int line, const char* extra);

#ifdef _DEBUG

	// Debug: Log and crash on failure. Release: Return 0
	#define check(OP, ...)         if(!(OP)) { __atlan_assert_failed(#OP, __FILE__, __LINE__, "" #__VA_ARGS__); return 0;}

	// Debug: Log and crash on failure. Release: Return nothing
	#define checkv(OP, ...)        if(!(OP)) { __atlan_assert_failed(#OP, __FILE__, __LINE__, "" #__VA_ARGS__); return;}

	// Debug: Log and crash on failure. Release: This check is compiled out
	#define check_debug(OP, ...)   if(!(OP)) { __atlan_assert_failed(#OP, __FILE__, __LINE__, "" #__VA_ARGS__);}

#else

	// Debug: Log and crash on failure. Release: Return 0
	#define check(OP, ...)  if(!(OP)) {return 0;}

	// Debug: Log and crash on failure. Release: Return nothing
	#define checkv(OP, ...) if(!(OP)) {return;}

	// Debug: Log and crash on failure. Release: This check is compiled out
	#define check_debug(OP, ...)

#endif

// Debug AND Release: Log and crash on failure
#define check_always(OP, ...) if(!(OP)) { __atlan_assert_failed(#OP, __FILE__, __LINE__, "" #__VA_ARGS__);}


/*
* Atlan Buffers
*/

// Simple char buffer that owns it's data
struct charbuffer_t {
	char* data = nullptr;
	size_t length = 0;
	size_t capacity = 0;

	~charbuffer_t();
	
	// Ensures buffer meets the minimum capacity
	// If re-allocation must occur any stored data is not
	// guaranteed to be preserved
	void EnsureCapacity(const size_t minimumCapacity);

	// Swaps contents of this buffer with another
	void Swap(charbuffer_t& other);
};

struct _iobuf;
typedef _iobuf FILE;
typedef long long fileposition_t;

struct FileReader {

	private:
	FILE* fptr = nullptr;
	int numErrors = 0;

	public:
	~FileReader();

	void close();
	bool open(const char* filepath);
	bool open(const wchar_t* filepath);
	bool isopen() const;
	bool noerrors() const;

	bool read(char* buffer, size_t length);

	bool seek(fileposition_t position);
	bool seekend(fileposition_t position);
	fileposition_t getposition() const;
	
	i64 getlength();

	static bool ReadFile(const wchar_t* in_filepath, charbuffer_t& out_buffer);
	static bool ReadFile(const char*    in_filepath, charbuffer_t& out_buffer);

	private:
	void ErrorDetected();
};