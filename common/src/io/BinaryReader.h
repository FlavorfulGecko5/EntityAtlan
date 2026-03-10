#include <string>

class BinaryReader
{
	private:
	const char* buffer = nullptr;
	const char* next = nullptr;
	const char* max = nullptr;

	public:

	BinaryReader() {}
	BinaryReader(const char* p_buffer, size_t length) : buffer(p_buffer), next(p_buffer), max(p_buffer + length) {}

	void SetBuffer(const char* p_buffer, size_t length) {
		buffer = p_buffer;
		next = p_buffer;
		max = p_buffer + length;
	}

	/*
	* Accessors
	*/

	bool InitSuccessful() const {
		return buffer != nullptr;
	}

	const char* GetBuffer() const {
		return buffer;
	}

	const char* GetNext() const {
		return next;
	}

	size_t GetLength() const {
		return max - buffer;
	};

	size_t GetPosition() const {
		return next - buffer;
	}

	size_t GetRemaining() const {
		return max - next;
	}

	bool ReachedEOF() const {
		return next == max;
	}

	void DebugLogState() const;


	/*
	* Navigation
	*/
	
	bool Goto(const size_t newPos)
	{
		if (buffer + newPos > max)
			return false;
		next = buffer + newPos;
		return true;
	}

	bool GoRight(const size_t shiftAmount)
	{
		if (next + shiftAmount > max) {
			//printf("%zu %zu %zu %zu\n", pos, shiftAmount, pos + shiftAmount, length);
			return false;
		}

		next += shiftAmount;
		return true;
	}


	/*
	* Reading Functions
	*/

	bool ReadBytes(const char*& writeTo, const size_t numBytes)
	{
		if (next + numBytes > max)
			return false;

		writeTo = next;
		next += numBytes;
		return true;
	}

	bool ReadCString(const char*& writeTo)
	{
		const char* iter = next;
		while (iter < max) {
			if (*iter++ == '\0') {
				writeTo = next;
				next+= (iter - next);
				return true;
			}
		}
		return false;
	}

	bool ReadLE(int8_t& readTo)
	{
		if(next + 1 > max)
			return false;
		readTo = *next;
		next++;
		return true;
	}

	bool ReadLE(uint8_t& readTo)
	{
		if(next + 1 > max)
			return false;
		readTo = *reinterpret_cast<const unsigned char*>(next);
		next++;
		return true;
	}

	bool ReadLE(uint16_t& readTo) {
		if(next + 2 > max)
			return false;
		
		readTo = *reinterpret_cast<const uint16_t*>(next);
		//readTo = _byteswap_ushort( *reinterpret_cast<uint16_t*>(buffer + pos));
		next += 2;
		return true;
	}

	bool ReadLE(int16_t& readTo) {
		uint16_t u;
		if (ReadLE(u)) {
			readTo = *reinterpret_cast<int16_t*>(&u);
			return true;
		}
		return false;
	}

	bool ReadLE(uint32_t& readTo)
	{
		if (next + 4 > max)
			return false;

		readTo = *reinterpret_cast<const uint32_t*>(next);
		//readTo = _byteswap_ulong(*reinterpret_cast<uint32_t*>(buffer + pos));
		next += 4;
		return true;
	}

	bool ReadLE(int32_t& readTo) {
		uint32_t u;
		if (ReadLE(u)) {
			readTo = *reinterpret_cast<int32_t*>(&u);
			return true;
		}
		return false;
		
	}

	bool ReadLE(uint64_t& readTo) {
		if (next + 8 > max)
			return false;

		readTo = *reinterpret_cast<const uint64_t*>(next);
		//readTo = _byteswap_uint64(*reinterpret_cast<uint64_t*>(buffer + pos));
		next += 8;
		return true;
	}

	bool ReadLE(int64_t& readTo) {
		uint64_t u;
		if (ReadLE(u)) {
			readTo = *reinterpret_cast<int64_t*>(&u);
			return true;
		}
		return false;
	}

	bool ReadLE(float& readTo) {
		uint32_t u;
		if (ReadLE(u)) {
			readTo = *reinterpret_cast<float*>(&u);
			return true;
		}
		return false;
	}

	bool ReadLE(double& readTo) {
		uint64_t u;
		if (ReadLE(u)) {
			readTo = *reinterpret_cast<double*>(&u);
			return true;
		}
		return false;
	}
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

	size_t len() const {return length;}
};