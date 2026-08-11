#include <string>
#include "atlan/AtlanLib.h"

// Binary stream parser. Does not own the data it's parsing
class BinaryReader
{
	private:
	const char* buffer = nullptr;
	const char* next = nullptr;
	const char* endptr = nullptr;
	int num_failures = 0;
	
	public:
	bool AbortOnError = false;

	public:
	BinaryReader();
	BinaryReader(const char* p_buffer, size_t length);
	BinaryReader(const charbuffer_t& p_buffer);

	void SetBuffer(const char* p_buffer, size_t length);
	void SetBuffer(const charbuffer_t& p_buffer);


	BinaryReader SubReader(size_t length);

	/*
	* Accessors
	*/

	bool        InitSuccessful() const {return buffer != nullptr;}
	const char* GetBuffer()      const {return buffer;}
	const char* GetNext()        const {return next;}
	size_t      GetLength()      const {return endptr - buffer;};
	size_t      GetPosition()    const {return next - buffer;}
	size_t      GetRemaining()   const {return endptr - next;}
	bool        ReachedEOF()     const {return next == endptr && NoErrors();}
	int         FailureCount()   const {return num_failures;}
	bool        NoErrors()       const {return num_failures == 0;}

	//void DebugLogState() const;


	/*
	* Navigation
	*/

	bool Goto(const size_t newPos);
	bool GoRight(const size_t shiftAmount);

	/*
	* Reading Functions
	*/

	bool ReadBytes(const char*& writeTo, const size_t numBytes);
	bool ReadCString(const char*& writeTo);

	/*
	* Read Little-Endian
	*/

	bool ReadLE(int8_t& readTo);
	bool ReadLE(uint8_t& readTo);
	bool ReadLE(uint16_t& readTo);
	bool ReadLE(int16_t& readTo);
	bool ReadLE(uint32_t& readTo);
	bool ReadLE(int32_t& readTo);
	bool ReadLE(uint64_t& readTo);
	bool ReadLE(int64_t& readTo);
	bool ReadLE(float& readTo);
	bool ReadLE(double& readTo);

	/*
	* Read Big-Endian
	*/
	bool ReadBig(uint32_t& readto);
	bool ReadBig(uint64_t& readto);

	/*
	* Right Shift: Equivalent to ReadLE with no return value
	* indicating a successful read. Must check the number of
	* failures to determine if a read was successful
	*/

	BinaryReader& operator>>(i8& readto);
	BinaryReader& operator>>(i16& readto);
	BinaryReader& operator>>(i32& readto);
	BinaryReader& operator>>(i64& readto);
	BinaryReader& operator>>(u8& readto);
	BinaryReader& operator>>(u16& readto);
	BinaryReader& operator>>(u32& readto);
	BinaryReader& operator>>(u64& readto);
	BinaryReader& operator>>(float& readto);
	BinaryReader& operator>>(double& readto);

	/*
	* Read a little-endian number and verify it's value
	*/

	bool check8(u8  expectedvalue);
	bool check16(u16 expectedvalue);
	bool check32(u32 expectedvalue);
	bool check64(u64 expectedvalue);

	private:
	template<typename TYPE>
	bool __BinaryReader_ReadLE_Internal(TYPE& readTo);

	template<typename TYPE>
	BinaryReader& __BinaryReader_RShift_Internal(TYPE& readto);

	template<typename TYPE>
	bool __BinaryReader_Check_Internal(TYPE expectedvalue);

	void ErrorDetected();
};

class BinaryOpener {
	private:
	char* buffer = nullptr;
	size_t length = 0;

	public:
	BinaryOpener(const std::string& path);

	BinaryOpener(const BinaryOpener& b) = delete;
	void operator=(const BinaryOpener& b) = delete;

	~BinaryOpener() {
		delete[] buffer;
	}

	bool Okay() const {
		return buffer != nullptr;
	}

	BinaryReader ToReader() {
		return BinaryReader(buffer, length);
	}

	const char* data() const {return buffer;}

	char* GetEditable() {return buffer;}

	size_t len() const {return length;}
};