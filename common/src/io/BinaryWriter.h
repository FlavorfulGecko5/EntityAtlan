#pragma once
#include <string>
#include <vector>

class BinaryWriter
{
	private:
	char* buffer = nullptr;
	char* next = nullptr;
	char* end = nullptr;
	float defaultSizeMultiplier = 2.0f;
	std::vector<size_t> sizeStack;
	
	//std::ofstream file;
	//char num[16]; // For converting numerical types to binary data
	//std::vector<std::streampos> sizeStack;

	/*
	* ACCESSORS
	*/

	public:

	size_t GetMaxCapacity() const {
		return end - buffer;
	}

	size_t GetPosition() const {
		return next - buffer;
	}

	size_t GetFilledSize() const {
		return next - buffer;
	}

	const char* GetBuffer() const {
		return buffer;
	}

	/*
	* RESIZING
	*/

	private:

	void GrowBuffer(size_t targetCapacity) {
		if (targetCapacity <= GetFilledSize())
			return;

		char* newBuffer = new char[targetCapacity];
		char* newNext = newBuffer + GetPosition();
		char* newEnd = newBuffer + targetCapacity;

		if (buffer != nullptr)
		{
			memcpy(newBuffer, buffer, GetFilledSize());
			delete[] buffer;
		}
		buffer = newBuffer;
		next = newNext;
		end = newEnd;
	}

	void GrowBuffer(size_t targetCapacity, float multiplier) {
		size_t newCapacity = static_cast<size_t>(GetMaxCapacity() * multiplier);
		if(newCapacity < targetCapacity)
			newCapacity = targetCapacity;
		GrowBuffer(newCapacity);
	}

	/*
	* CONSTRUCTION / DESTRUCTION
	*/

	public:

	~BinaryWriter() {
		delete[] buffer;
	}

	BinaryWriter(size_t initialCapacity, float p_resizeMultiplier) : defaultSizeMultiplier(p_resizeMultiplier) {
		GrowBuffer(initialCapacity);
	}

	BinaryWriter(size_t initialCapacity) {
		GrowBuffer(initialCapacity);
	}

	BinaryWriter(const BinaryWriter& b) = delete;
	void operator=(const BinaryWriter& b) = delete;

	/* Returns and releases ownership of the buffer */
	char* Finalize() {
		char* final = buffer;
		buffer = nullptr;
		next = nullptr;
		end = nullptr;
		return final;
	}

	/*
	* NAVIGATION
	*/

	public:
	
	//void Goto(const size_t newPos) {
	//	if (buffer + newPos >= end)
	//		GrowBuffer(newPos + 1, defaultSizeMultiplier);
	//	next = buffer + newPos;
	//}

	/*
	* WRITING
	*/

	void WriteBytes(const char* bytes, const size_t numBytes) {
		if (next + numBytes > end) {
			GrowBuffer(GetMaxCapacity() + numBytes, defaultSizeMultiplier);
		}

		memcpy(next, bytes, numBytes);
		next += numBytes;
	}

	template<typename T>
	BinaryWriter& operator<<(T value) 
	{
		if (next + sizeof(T) > end) {
			GrowBuffer(GetMaxCapacity() + sizeof(T), defaultSizeMultiplier);
		}

		*reinterpret_cast<T*>(next) = value;
		next += sizeof(T);

		return *this;
	}

	/*
	* SIZE STACK
	*/

	void pushSizeStack() {
		if (next + sizeof(uint32_t) > end) {
			GrowBuffer(GetMaxCapacity() + sizeof(uint32_t), defaultSizeMultiplier);
		}

		sizeStack.push_back(GetPosition());
		next += sizeof(uint32_t);
	}

	void popSizeStack() {
		if(sizeStack.empty())
			return;

		char* sizePos = buffer + sizeStack.back();

		sizeStack.pop_back();

		uint32_t sizeValue = static_cast<uint32_t>(next - sizePos - sizeof(uint32_t));
		*reinterpret_cast<uint32_t*>(sizePos) = sizeValue;
	}

	bool SaveTo(const std::string& path);
};