#include "BinaryReader.h"

/*
* Binary Reader
*/

BinaryReader::BinaryReader() {}

BinaryReader::BinaryReader(const char* p_buffer, size_t length)
	: buffer(p_buffer), next(p_buffer), endptr(p_buffer + length) {}

BinaryReader::BinaryReader(const charbuffer_t& p_buffer)
	: BinaryReader(p_buffer.data, p_buffer.length) {}

void BinaryReader::SetBuffer(const char* p_buffer, size_t length) {
	buffer = p_buffer;
	next = p_buffer;
	endptr = p_buffer + length;
	num_failures = 0;
}

void BinaryReader::SetBuffer(const charbuffer_t& p_buffer)
{
	buffer = p_buffer.data;
	next = p_buffer.data;
	endptr = p_buffer.data + p_buffer.length;
	num_failures = 0;
}

BinaryReader BinaryReader::SubReader(size_t length)
{
	BinaryReader subreader;
	
	if (next + length > endptr) {
		BinaryReader::ErrorDetected();
		return subreader;
	}

	subreader.buffer = next;
	subreader.next = next;
	subreader.endptr = next + length;
	subreader.AbortOnError = AbortOnError;
	next += length;

	return subreader;
}

bool BinaryReader::Goto(const size_t newPos)
{
	if (buffer + newPos > endptr) {
		BinaryReader::ErrorDetected();
		return false;
	}
	next = buffer + newPos;
	return true;
}

bool BinaryReader::GoRight(const size_t shiftAmount)
{
	if (next + shiftAmount > endptr) {
		BinaryReader::ErrorDetected();
		return false;
	}

	next += shiftAmount;
	return true;
}

bool BinaryReader::ReadBytes(const char*& writeTo, const size_t numBytes)
{
	if (next + numBytes > endptr) {
		BinaryReader::ErrorDetected();
		return false;
	}

	writeTo = next;
	next += numBytes;
	return true;
}

bool BinaryReader::ReadCString(const char*& writeTo)
{
	const char* iter = next;
	while (iter < endptr) {
		if (*iter++ == '\0') {
			writeTo = next;
			next = iter;
			return true;
		}
	}
	BinaryReader::ErrorDetected();
	return false;
}

template<typename TYPE>
bool BinaryReader::__BinaryReader_ReadLE_Internal(TYPE& readTo) {
	if (next + sizeof(TYPE) > endptr) {
		BinaryReader::ErrorDetected();
		return false;
	}

	readTo = *reinterpret_cast<const TYPE*>(next);
	next += sizeof(TYPE);
	return true;
}

bool BinaryReader::ReadLE(int8_t& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }
bool BinaryReader::ReadLE(uint8_t& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }
bool BinaryReader::ReadLE(uint16_t& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }
bool BinaryReader::ReadLE(int16_t& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }
bool BinaryReader::ReadLE(uint32_t& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }
bool BinaryReader::ReadLE(int32_t& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }
bool BinaryReader::ReadLE(uint64_t& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }
bool BinaryReader::ReadLE(int64_t& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }
bool BinaryReader::ReadLE(float& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }
bool BinaryReader::ReadLE(double& readTo) { return __BinaryReader_ReadLE_Internal(readTo); }

template<typename TYPE>
BinaryReader& BinaryReader::__BinaryReader_RShift_Internal(TYPE& readto) {
	if (next + sizeof(TYPE) > endptr) {
		BinaryReader::ErrorDetected();
		return *this; 
	}

	readto = *reinterpret_cast<const TYPE*>(next);
	next += sizeof(TYPE);
	return *this;
}

BinaryReader& BinaryReader::operator>>(i8& readto) { return __BinaryReader_RShift_Internal(readto); }
BinaryReader& BinaryReader::operator>>(i16& readto) { return __BinaryReader_RShift_Internal(readto); }
BinaryReader& BinaryReader::operator>>(i32& readto) { return __BinaryReader_RShift_Internal(readto); }
BinaryReader& BinaryReader::operator>>(i64& readto) { return __BinaryReader_RShift_Internal(readto); }
BinaryReader& BinaryReader::operator>>(u8& readto) { return __BinaryReader_RShift_Internal(readto); }
BinaryReader& BinaryReader::operator>>(u16& readto) { return __BinaryReader_RShift_Internal(readto); }
BinaryReader& BinaryReader::operator>>(u32& readto) { return __BinaryReader_RShift_Internal(readto); }
BinaryReader& BinaryReader::operator>>(u64& readto) { return __BinaryReader_RShift_Internal(readto); }
BinaryReader& BinaryReader::operator>>(float& readto) { return __BinaryReader_RShift_Internal(readto); }
BinaryReader& BinaryReader::operator>>(double& readto) { return __BinaryReader_RShift_Internal(readto); }

template<typename TYPE>
bool BinaryReader::__BinaryReader_Check_Internal(TYPE expectedvalue)
{
	TYPE readto;
	if (next + sizeof(TYPE) > endptr) {
		BinaryReader::ErrorDetected();
		return false;
	}
	readto = *reinterpret_cast<const TYPE*>(next);
	next += sizeof(TYPE);
	
	if (readto != expectedvalue) {
		BinaryReader::ErrorDetected();
		return false;
	}
	return true;
}

bool BinaryReader::check8(u8 expectedvalue)   { return __BinaryReader_Check_Internal(expectedvalue);}
bool BinaryReader::check16(u16 expectedvalue) { return __BinaryReader_Check_Internal(expectedvalue); }
bool BinaryReader::check32(u32 expectedvalue) { return __BinaryReader_Check_Internal(expectedvalue); }
bool BinaryReader::check64(u64 expectedvalue) { return __BinaryReader_Check_Internal(expectedvalue); }


void BinaryReader::ErrorDetected() {
	size_t debug_position = BinaryReader::GetPosition();
	const char* debug_next = BinaryReader::GetNext();

	// Best to ensure we halt at the first error so we don't do stuff like read
	// list lengths from the wrong field
	num_failures++;
	next = endptr;

	check_debug(0, "BinaryReader::ErrorDetected");

	check_always(false == AbortOnError, "BinaryReader::ErrorDetected");
}

BinaryOpener::BinaryOpener(const std::string& path)
{
	charbuffer_t temp;
	FileReader::ReadFile(path.c_str(), temp);
	buffer = temp.data;
	length = temp.length;
	temp.data = nullptr; 
	temp.capacity = 0; 
	temp.length = 0;
}