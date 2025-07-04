#include <fstream>
#include "Oodle.h"
#include "EntityLogger.h"
#include "EntityNode.h"
/*
* This file should be used to define any fields or functions
* that can't be defined in the header files
*/

EntNode _404;

EntNode* EntNode::SEARCH_404 = &_404;

bool EntNode::IsComment() {
	if (childCount == 0 && nameLength > 1) {
		if (textPtr[0] == '/' && textPtr[1] == '/')
			return true;
		if (textPtr[0] == '/' && textPtr[1] == '*')
			return true;
	}
	return false;
}

bool EntNode::IsRoot() {
	return parent == nullptr && nodeFlags == NFC_RootNode;
}

bool EntNode::IsContainer() {
	return childCount > 0 || (nodeFlags & NF_Braces);
}


bool EntNode::ValueInt(int& writeTo, int clampMin, int clampMax) const {
	if (valLength == 0) return false;
	
	const char* firstChar = textPtr + nameLength;
	int base = 1, value = 0;

	for (const char* ptr = textPtr + nameLength + valLength - 1; ptr >= firstChar; ptr--) {

		char c = *ptr;
		if (c >= '0' && c <= '9') {
			value += (c - '0') * base;
			base *= 10;
		}
		else if (c == '-') {
			if (ptr == firstChar) {
				value = -value;
			}
			else {
				return false;
			}
			
		}
		else return false;
	}

	if(value < clampMin) value = clampMin;
	if(value > clampMax) value = clampMax;

	writeTo = value;
	return true;
}

bool EntNode::ValueBool(bool& writeTo) const {
	if(valLength == 0) return false;

	const char* ptr = textPtr + nameLength;

	if (valLength == 1) {
		if (*ptr == '0') {
			writeTo = false;
			return true;
		}
		if (*ptr == '1') {
			writeTo = true;
			return true;
		}
	}

	if (valLength == 4) {
		if (memcmp(ptr, "true", 4) == 0) {
			writeTo = true;
			return true;
		}
	}

	if (valLength == 5) {
		if (memcmp(ptr, "false", 5) == 0) {
			writeTo = false;
			return true;
		}
	}

	return false;
}


bool EntNode::findPositionalID(EntNode* n, int& id)
{
	// We're not doing any undo/redo here, and this function is slowing everything down
	return 0;
	if (this == n) return true;
	id++;

	for (int i = 0; i < childCount; i++)
		if (children[i]->findPositionalID(n, id)) return true;
	return false;
}

EntNode* EntNode::nodeFromPositionalID(int& decrementor)
{
	if (decrementor == 0) return this;
	for (int i = 0; i < childCount; i++)
	{
		EntNode* result = children[i]->nodeFromPositionalID(--decrementor);
		if (result != nullptr) return result;
	}
	return nullptr;
}

bool EntNode::searchText(const std::string& key, const bool caseSensitive, const bool exactLength)
{
	if(exactLength && key.length() != nameLength + valLength) return false;
	if (caseSensitive)
	{
		std::string_view s(textPtr, nameLength + valLength);
		return s.find(key) != std::string_view::npos;
	}

	int k = (int)key.length(), n = nameLength + valLength;
	int max = n - k + 1; // Don't search where key can't fit.
	int i = 0;

	char fixedKey0 = key[0];
	if(fixedKey0 > '`' && fixedKey0 < '{') fixedKey0 -= 32;

	FAILED:
	while (i < max)
	{
		char c1 = textPtr[i++];
		if(c1 > '`' && c1 < '{') c1-= 32;

		if (c1 == fixedKey0)
		{
			for (int j = 1; j < k; j++, i++)
			{
				c1 = textPtr[i]; if (c1 > '`' && c1 < '{') c1 -= 32;
				char c2 = key[j]; if (c2 > '`' && c2 < '{') c2 -= 32;
				// Do not increment i here: If we fail, we'll want to
				// check the failing character to see if it == key[0]
				if (c1 != c2)
					goto FAILED;
			}

			return true;
		}
	}
	return false;
}

/*
* For searching downwards, we must:
* - Check all children of the starting node
* - recursively check all nodes listed after the starting node
* - parents in parent chain do not need their values checked
*/

EntNode* EntNode::searchDownwards(const std::string& key, const bool caseSensitive, const bool exactLength, const EntNode* startAfter) 
{
	int startIndex = 0;
	if(startAfter != nullptr) // Ensures all children in the starting node are checked
		while(startIndex < childCount)
			if(children[startIndex++] == startAfter) break;

	for (int i = startIndex; i < childCount; i++)
	{
		EntNode* result = children[i]->searchDownwardsLocal(key, caseSensitive, exactLength);
		if(result != SEARCH_404) return result;
	}

	if(parent != nullptr)
		return parent->searchDownwards(key, caseSensitive, exactLength, this);

	// Wrap around by performing local search on root
	return searchDownwardsLocal(key, caseSensitive, exactLength);
}

EntNode* EntNode::searchDownwardsLocal(const std::string& key, const bool caseSensitive, const bool exactLength)
{
	if(searchText(key, caseSensitive, exactLength)) return this;
	for (int i = 0; i < childCount; i++)
	{
		EntNode* result = children[i]->searchDownwardsLocal(key, caseSensitive, exactLength);
		if(result != SEARCH_404) return result;
	}
	return SEARCH_404;
}

/*
* Upward Searches require we:
* - Do not check the children of the starting node
* - Check children above the node
* - Parents have their text checked AFTER children are checked
*/

EntNode* EntNode::searchUpwards(const std::string& key, const bool caseSensitive, const bool exactLength)
{
	// Wrap around by performing local search on root
	if (parent == nullptr)
		return searchUpwardsLocal(key, caseSensitive, exactLength);

	// Check the parent's child nodes placed above this one
	int startIndex = parent->childCount - 1;
	while (startIndex > -1)
		if (parent->children[startIndex--] == this) break;

	for (int i = startIndex; i > -1; i--)
	{
		EntNode* result = parent->children[i]->searchUpwardsLocal(key, caseSensitive, exactLength);
		if (result != SEARCH_404) return result;
	}

	if (parent->searchText(key, caseSensitive, exactLength)) return parent;
	return parent->searchUpwards(key, caseSensitive, exactLength);
}

EntNode* EntNode::searchUpwardsLocal(const std::string& key, const bool caseSensitive, const bool exactLength)
{
	for (int i = childCount - 1; i > -1; i--)
	{
		EntNode* result = children[i]->searchUpwardsLocal(key, caseSensitive, exactLength);
		if (result != SEARCH_404) return result;
	} // Search children in reverse order, then the node's own text
	if (searchText(key, caseSensitive, exactLength)) return this;
	return SEARCH_404;
}

void EntNode::generateText(std::string& buffer, int wsIndex)
{
	buffer.append(wsIndex, '\t');
	buffer.append(textPtr, nameLength);

	if(nodeFlags & NF_Equals)
		buffer.append(" =");
	if(nodeFlags & NF_Colon)
		buffer.push_back(':');
	
	if (valLength > 0) {
		buffer.push_back(' ');
		buffer.append(textPtr + nameLength, valLength);
	}

	if(nodeFlags & NF_Semicolon)
		buffer.push_back(';');

	if(nodeFlags & NF_Braces)
		buffer.append(" {\n");
	else if(nodeFlags & NF_Brackets)
		buffer.append(" [\n");

	if(nodeFlags & NF_NoIndent)
		wsIndex--;
	
	for (int i = 0; i < childCount; i++) {
		children[i]->generateText(buffer, wsIndex + 1);
		buffer.push_back('\n');
	}
	if (nodeFlags & NF_Braces) {
		if(wsIndex > 0)
			buffer.append(wsIndex, '\t');
		buffer.push_back('}');
	}
	else if (nodeFlags & NF_Brackets) {
		buffer.append(wsIndex, '\t');
		buffer.push_back(']');
	}
	if(nodeFlags & NF_Comma)
		buffer.push_back(',');
}

size_t EntNode::writeToFile(const std::string filepath, const size_t sizeHint, const bool oodleCompress, const bool debug_logTime)
{
	auto timeStart = std::chrono::high_resolution_clock::now();
	// 25% of time spent writing to output buffer, 75% on generateText

	std::string raw;
	raw.reserve(sizeHint);
	generateText(raw);

	if(debug_logTime)
		EntityLogger::logTimeStamps("Generate Text Duration: ", timeStart);

	timeStart = std::chrono::high_resolution_clock::now();

	std::ofstream output(filepath, std::ios_base::binary);

	if (oodleCompress)
	{
		char* compressedData = new char[raw.length() + 65536];
		size_t compressedSize;
		if (Oodle::CompressBuffer(raw.data(), raw.length(), compressedData + 16, compressedSize)) {
			((size_t*)compressedData)[0] = raw.length();
			((size_t*)compressedData)[1] = compressedSize;
			output.write(compressedData, compressedSize + 16);
		}
		else {
			EntityLogger::logWarning("Failed to compress .entities file. Saving uncompressed version instead.");
			output << raw;
		}
		delete[] compressedData;
	}
	else output << raw;

	output.close();
	if (debug_logTime)
		EntityLogger::logTimeStamps("Writing Duration: ", timeStart);
	return raw.length();
}