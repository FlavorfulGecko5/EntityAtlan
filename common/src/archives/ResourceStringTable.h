#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <unordered_map>
#include "ResourceStructs.h"

// Used for building a Resource Archive's string chunk
class StringTable {
	private:
	std::string blob;
	std::vector<uint64_t> offsets;
	std::unordered_map<std::string, uint64_t> offsetmap; // Maps strings to their index in the table / offset list

	public:
	StringTable() {
		blob.reserve(100000);
		offsets.reserve(1000);
		offsetmap.reserve(1000);
	}


	/*
	* Returns the offset for a string
	* Adds the string to the table if it doesn't exist
	*/
	uint64_t indexof(const std::string_view s) {
		const auto iter = offsetmap.find(std::string(s));
		if (iter == offsetmap.end())
		{
			uint64_t index = offsets.size();

			offsets.push_back(blob.size());
			blob.append(s);
			blob.push_back('\0');
			offsetmap.emplace(s, index);
			return index;
		}
		else 
		{
			return iter->second;
		}
	}

	/*
	* Copies data over to a string chunk
	* Once this is called, you can still use this object for locating
	* offsets but you shouldn't modify it any further
	*/
	void finalize(StringChunk& s, uint32_t& finalSize) const {
		s.numStrings = offsets.size();
		
		s.offsets.EnsureCapacity(offsets.size());
		memcpy(s.offsets.buffer, offsets.data(), offsets.size() * sizeof(uint64_t));
		
		s.dataBlock.EnsureCapacity(blob.size());
		memcpy(s.dataBlock.buffer, blob.data(), blob.size());

		// String Count + Offset Chunk + String Blob
		finalSize = static_cast<uint32_t>(sizeof(uint64_t) + s.numStrings * sizeof(uint64_t) + blob.size());

		s.paddingCount = 8 - finalSize % 8;
		finalSize += static_cast<uint32_t>(s.paddingCount);
	}
};