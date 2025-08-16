//#pragma warning(disable : 4996) // Deprecation errors
#include <fstream>
#include "Oodle.h"
#include "EntityLogger.h"
#include "EntityParser.h"

enum TokenType : uint32_t
{
	TT_End          = 1 << 0,
	TT_Newline      = 1 << 1, // Permissive parsing mode only
	TT_BraceOpen    = 1 << 2,
	TT_BraceClose   = 1 << 3,
	TT_EqualSign    = 1 << 4,
	TT_Semicolon    = 1 << 5,
	TT_Comma        = 1 << 6,
	TT_Parenclose   = 1 << 7,
	TT_Comment      = 1 << 8,
	TT_Identifier   = 1 << 9,
	TT_Number       = 1 << 10,
	TT_Tuple        = 1 << 11,
	TT_String       = 1 << 12,
	TT_Keyword      = 1 << 13,
	TT_Colon        = 1 << 14,
	TT_BracketOpen  = 1 << 15,
	TT_BracketClose = 1 << 16,
	TT_IndyHex      = 1 << 17,

	/* Token Type Combos */
	TTC_PermissiveKey = TT_Identifier | TT_Number | TT_Tuple | TT_String | TT_Keyword,
	TTC_EntityValues = TT_Number | TT_String | TT_Keyword,

	TTC_Perm2Terminals = TT_End | TT_Newline | TT_BraceOpen | TT_BraceClose | TT_Comment | TT_Semicolon,
	TTC_Perm2UniqueTokens = TT_Comment | TT_Identifier | TT_Number | TT_Tuple | TT_String | TT_Keyword
};

const std::string EntityParser::FILTER_NOLAYERS = "\"No Layers\"";
const std::string EntityParser::FILTER_NOCOMPONENTS = "\"No Components\"";

EntityParser::EntityParser() : fileWasCompressed(false), PARSEMODE(ParsingMode::ENTITIES)
{
	// Cannot append a null character to a string? Hence const char* instead
	const char* rawText = "Version 7\nHierarchyVersion 1\0";
	ParseResult presult;
	initiateParse(rawText, &root, &root, presult);
}

EntityParser::EntityParser(const std::string& filepath, const ParsingMode mode, const bool debug_logParseTime)
	: PARSEMODE(mode)
{
	auto timeStart = std::chrono::high_resolution_clock::now();
	size_t rawLength = 0;
	char* rawBytes = nullptr;
	char* decompressedText = nullptr;
	std::string_view textView;

	{
		std::ifstream file(filepath, std::ios_base::binary); // Binary mode 50% faster than 'in' mode, keeps CR chars
		if (!file.is_open())
			throw std::runtime_error("Could not open file");

		// Tellg() does not guarantee the length of the file but this works in practice for binary mode
		file.seekg(0, std::ios_base::end);
		rawLength = static_cast<size_t>(file.tellg());
		rawBytes = new char[rawLength + 1]; // Leave room for the null char
		file.seekg(0, std::ios_base::beg);
		file.read(rawBytes, rawLength);
		file.close();
	}

	if (rawLength > 16 && (unsigned char)rawBytes[16] == 0x8C) // Oodle compression signature
	{
		fileWasCompressed = true;
		size_t decompressedSize = ((size_t*)rawBytes)[0];
		size_t compressedSize = ((size_t*)rawBytes)[1];
		decompressedText = new char[decompressedSize + 1]; // +1 for the null char

		if (!Oodle::DecompressBuffer(rawBytes + 16, compressedSize, decompressedText, decompressedSize))
			throw std::runtime_error("Could not decompress .entities file");
		decompressedText[decompressedSize] = '\0';
		textView = std::string_view(decompressedText, decompressedSize + 1);
	}
	else
	{
		fileWasCompressed = false;
		rawBytes[rawLength] = '\0';
		textView = std::string_view(rawBytes, rawLength + 1);
	}

	lastUncompressedSize = textView.length();

	if (debug_logParseTime)
	{
		EntityLogger::logTimeStamps("File Read/Decompress Duration: ", timeStart);
		timeStart = std::chrono::high_resolution_clock::now();
	}

	// Simpler char analysis algorithm that runs ~10 MS faster compared to old doubled runtime
	size_t counts[256] = { 0 };
	for (uint8_t c : textView) // Ensure it's unsigned if you don't want cursed stack corruption
		counts[c]++;

	// Distinguishes between the number of chars comprising actual identifiers/values versus syntax chars
	if (PARSEMODE == ParsingMode::JSON) {
		size_t charBufferSize = textView.length()
			- counts['\t'] - counts['\n'] - counts['\r'] - counts['}'] - counts['{']
			- counts[':'] - counts[','] - counts['['] - counts[']'] - counts[' ']
			+ 100000;
		textAlloc.setActiveBuffer(charBufferSize);

		// This should give us an exact count of how many nodes exist in the file
		size_t nodeCount = counts[','] + counts['{'] + counts['['] + 1000;
		nodeAlloc.setActiveBuffer(nodeCount);
		childAlloc.setActiveBuffer(nodeCount);
	}
	else {
		size_t charBufferSize = textView.length()
			- counts['\t'] - counts['\n'] - counts['\r'] - counts['}'] - counts['{'] - counts[';']
			- counts['=']
			- counts[' '] // This is an overestimate - string values will uncommonly contain spaces
			+ 100000;
		textAlloc.setActiveBuffer(charBufferSize);

		// For a well-formatted .entities file, we can get an exact count of how many nodes we must
		// allocate by subtracting the number of closing braces from the number of lines
		size_t numCloseBraces = counts['}'] > counts['\n'] ? counts['\n'] : counts['}']; // Prevents disastrous overflow
		size_t initialBufferSize = counts['\n'] - numCloseBraces + 1000;
		nodeAlloc.setActiveBuffer(initialBufferSize);
		childAlloc.setActiveBuffer(initialBufferSize);
	}

	if (debug_logParseTime)
	{
		EntityLogger::logTimeStamps("Node Buffer Init Duration: ", timeStart);
		timeStart = std::chrono::high_resolution_clock::now();
	}

	try {
		ParseResult presult;
		initiateParse(textView.data(), &root, &root, presult);
	} // How did I never notice this memory leak until now...?
	catch (std::runtime_error err) {
		if (fileWasCompressed) {
			std::ofstream decompressor(filepath, std::ios_base::binary);
			decompressor.write(textView.data(), textView.length() - 1);
			decompressor.close();
		}

		if(fileWasCompressed)
			delete[] decompressedText;
		delete[] rawBytes;
		throw err;
	}

	if (fileWasCompressed)
		delete[] decompressedText;
	delete[] rawBytes;
	if (debug_logParseTime)
		EntityLogger::logTimeStamps("Parsing Duration: ", timeStart);
}

/*
New strategy to prevent nodes with irregularly large numbers of children
from creating runaway allocations. This implementation is more generalized
and maintainable compared to the original solution meant to exclusively
handle the large root child counts of .entities files
*/
int OptimalMaxChildCount(int childCount) {
	if (childCount > 100) {
		// More non-root nodes can have more than 100 children than you might initially believe
		// Hence we should do multiplication instead of flat adding 1000 to every oversized childCount
		int addition = static_cast<int>(childCount * 0.1);
		if (addition > 1000)
			addition = 1000;
		return childCount + addition;
	}
	else return childCount;
}

ParseResult EntityParser::EditTree(const std::string& text, EntNode* parent, int insertionIndex, int removeCount, bool renumberLists, bool highlightNew)
{
	ParseResult outcome;

	// We must ensure the parse is successful before modifying the existing tree
	EntNode tempRoot(EntNode::NFC_RootNode);
	//text.push_back('\0');
	initiateParse(text.data(), &tempRoot, parent, outcome);
	if(!outcome.success) return outcome;

	// Give every node a comma - we'll ensure the (possibly new) last child has no
	// comma after merging the children
	if (PARSEMODE == ParsingMode::JSON) {
		if(parent->childCount > 0)
			parent->children[parent->childCount - 1]->nodeFlags |= EntNode::NF_Comma;

		if(tempRoot.childCount > 0)
			tempRoot.children[tempRoot.childCount - 1]->nodeFlags |= EntNode::NF_Comma;
	}

	// Populate these with nodes we might need to remove/add to the dataview
	std::vector<EntNode*> newNodes;

	// Build the reverse command
	reverseGroup.emplace_back();
	ParseCommand& reverse = reverseGroup.back();
	reverse.type = CommandType::EDIT_TREE;
	root.findPositionalID(parent, reverse.parentID);
	reverse.insertionIndex = insertionIndex;
	reverse.removalCount = tempRoot.childCount;
	for (int i = 0; i < removeCount; i++) {
		EntNode* n = parent->children[insertionIndex + i];
		n->generateText(reverse.text);
		reverse.text.push_back('\n');

		// Deallocate deleted nodes
		freeNode(n);
	}

	// Prep. the new nodes to be fully integrated into the tree
	for (int i = 0; i < tempRoot.childCount; i++)
	{
		EntNode* n = tempRoot.children[i];
		n->parent = parent;
		if(n->filtered) // Future-proofing
			newNodes.push_back(n);
	}
	
	/* 
	* Common Steps for Each Branch:
	* 1. Move everything into the new child buffer
	* 2. Deallocate the old child buffers
	* 3. Assign the new child buffer/child count to the parent
	*/
	int newNumChildren = parent->childCount + tempRoot.childCount - removeCount;

	if (newNumChildren > parent->maxChildren) {
		int newMaxChildren = OptimalMaxChildCount(newNumChildren);
		EntNode** newChildBuffer = childAlloc.reserveBlock(newMaxChildren);

		// Copy everything into the new child buffer
		int inc = 0;
		for (inc = 0; inc < insertionIndex; inc++)
			newChildBuffer[inc] = parent->children[inc];
		for (int i = 0; i < tempRoot.childCount; i++)
			newChildBuffer[inc++] = tempRoot.children[i];
		for (int i = insertionIndex + removeCount; i < parent->childCount; i++)
			newChildBuffer[inc++] = parent->children[i];

		// Deallocate old child buffer
		childAlloc.freeBlock(parent->children, parent->maxChildren);
		
		// Finally, attach new data to the parent
		parent->maxChildren = newMaxChildren;
		parent->children = newChildBuffer;
		
	}
	else {
		int difference = newNumChildren - parent->childCount;
		int min = insertionIndex + removeCount;
		if (difference > 0) // Must shift to right to expand room
			for (int i = parent->childCount - 1; i >= min; i--)
				parent->children[i + difference] = parent->children[i];
					
		if (difference < 0) // Must shift left to contract space
			for (int i = min, max = parent->childCount; i < max; i++)
				parent->children[i + difference] = parent->children[i];

		for (int inc = insertionIndex, i = 0, max = tempRoot.childCount; i < max; inc++, i++)
			parent->children[inc] = tempRoot.children[i];
	}

	// Common to both branches
	childAlloc.freeBlock(tempRoot.children, tempRoot.maxChildren);
	parent->childCount = newNumChildren;

	if (PARSEMODE == ParsingMode::JSON) {
		if(parent->childCount > 0)
			parent->children[parent->childCount - 1]->nodeFlags &= ~EntNode::NF_Comma;
	}

	// Must update model AFTER node is given it's new child data
	if (parent->isFiltered()) // Filtering checks are optimized so we only check parent + ancestor filter status once, right here
	{
	}

	if (renumberLists)
	{
		for (EntNode* n : newNodes)
			fixListNumberings(n, true, false);
		if(parent != &root) // Don't waste time reordering the root children, there shouldn't be a list there
			fixListNumberings(parent, false, false);
	}
	fileUpToDate = false;
	return outcome;
}

void EntityParser::EditText(const std::string& text, EntNode* node, int nameLength, bool highlight)
{
	// Construct reverse command
	reverseGroup.emplace_back();
	ParseCommand& reverse = reverseGroup.back();
	reverse.type = CommandType::EDIT_TEXT;
	reverse.text = std::string(node->textPtr, node->nameLength + node->valLength);
	reverse.insertionIndex = node->nameLength;
	root.findPositionalID(node, reverse.parentID);

	// Create new buffer
	char* newBuffer = textAlloc.reserveBlock(text.length());
	int i = 0;
	for (char c : text)
		newBuffer[i++] = c;

	// Free old data
	textAlloc.freeBlock(node->textPtr, node->nameLength + node->valLength);

	// Assign new data to node
	node->textPtr = newBuffer;
	node->nameLength = nameLength;
	node->valLength = (int)text.length() - nameLength;

	// Alert model
	if (node->isFiltered()) // Todo: add safeguards so node can't be the root
	{
	}

	fileUpToDate = false;
}

/* 
* Moves a node's child to a different index in it's buffer 
* Nodes inbetween the two indices are shifted up/down to fill the node's old slot
*/
void EntityParser::EditPosition(EntNode* parent, int childIndex, int insertionIndex, bool highlight)
{
	// Construct reverse command
	reverseGroup.emplace_back();
	ParseCommand& reverse = reverseGroup.back();
	reverse.type = CommandType::EDIT_POSITION;
	reverse.insertionIndex = childIndex; // Removal count is interpreted as the child index, and 
	reverse.removalCount = insertionIndex;
	root.findPositionalID(parent, reverse.parentID);

	// Assemble data
	EntNode** buffer = parent->children;
	EntNode* child = buffer[childIndex];

	// Shift nodes around
	if (childIndex < insertionIndex)  // Shift middle elements up
		for (int i = childIndex; i < insertionIndex; i++)
			buffer[i] = buffer[i + 1];

	else  // Shift middle elements down
		for (int i = childIndex; i > insertionIndex; i--)
			buffer[i] = buffer[i - 1];
	buffer[insertionIndex] = child;
	
	// Only alert model if node is filtered in 
	// We assume Root will never be the node we're moving (todo: add safeguards to ensure this)
	if (child->isFiltered())
	{
	}
	fileUpToDate = false;
}

void EntityParser::fixListNumberings(EntNode* parent, bool recursive, bool highlight)
{
	// Do not renumber the ids of Indiana Jones components
	if (parent->getName() == "components") {
		for (int i = 0; i < parent->childCount; i++)
		{
			EntNode* current = parent->children[i];
			if (current->childCount > 0 && recursive)
				fixListNumberings(current, true, highlight);
		}
		return;
	}

	int listItems = 0;
	for (int i = 0; i < parent->childCount; i++) 
	{
		EntNode* current = parent->children[i];
		if(current->childCount > 0 && recursive)
			fixListNumberings(current, true, highlight);

		std::string_view name = current->getName();
		if(!name._Starts_with("item["))
			continue;
		listItems++;

		std::string newName = "item[" + std::to_string(listItems - 1) + ']';
		if(name == newName) continue;

		int length = (int)newName.length();
		newName.append(current->getValue());

		EditText(newName, current, length, highlight);
	}

	EntNode& numNode = (*parent)["num"];
	if (&numNode == EntNode::SEARCH_404)
	{
		if (listItems == 0) return;
		EditTree("num = " + std::to_string(listItems) + ';', parent, 0, 0, false, highlight);
	}
	else {
		std::string newVal = std::to_string(listItems);
		if(numNode.getValue() == newVal) return;

		EditText("num" + newVal, &numNode, 3, highlight);
	}
}

void EntityParser::PushGroupCommand() {
	if (reverseGroup.size() == 0) return;

	if (redoIndex < history.size())
	{
		size_t index = redoIndex, max = history.size();
		while (index < max)
			if (history[index++].lastInGroup)
				commandCount--;
		history.resize(redoIndex);
	}

	if (commandCount == MAX_COMMAND_MEMORY)
	{
		// Assumes a max command history > 1
		// Find start of the second-to-last undo command,
		// and erase everything before it.
		size_t index = 1;
		while (!history[index].lastInGroup)
			index++;
		auto first = history.begin();
		history.erase(first, first + index);
		commandCount--;
	}

	reverseGroup[0].lastInGroup = true;
	for (ParseCommand& p : reverseGroup)
		history.push_back(p);
	redoIndex = (int)history.size();
	reverseGroup.clear();
	commandCount++;
}

void EntityParser::CancelGroupCommand()
{
	for (int i = (int)reverseGroup.size() - 1; i > -1; i--) // Should(?) underflow to -1
		ExecuteCommand(reverseGroup[i]);
	reverseGroup.clear();
}

void EntityParser::ClearHistory()
{
	history.clear();
	redoIndex = 0;
	commandCount = 0;
}

void EntityParser::ExecuteCommand(ParseCommand& cmd)
{
	int id = cmd.parentID;
	EntNode* node = root.nodeFromPositionalID(id);
	switch (cmd.type)
	{
		case CommandType::EDIT_TREE:
		EditTree(cmd.text, node, cmd.insertionIndex, cmd.removalCount, false, true);
		break;

		case CommandType::EDIT_TEXT:
		EditText(cmd.text, node, cmd.insertionIndex, true); // TODO: GENERALIZE PARM NAMES?
		break;

		case CommandType::EDIT_POSITION:
		EditPosition(node, cmd.removalCount, cmd.insertionIndex, true);
		break;
	}
}

bool EntityParser::Undo()
{
	// TODO: ADD SAFEGUARDS FOR UNDOING / REDOING WHILE UNPUSHED GROUP COMMAND EXISTS
	// Nothing to undo
	if (redoIndex == 0) return false;

	int index = redoIndex - 1;
	do ExecuteCommand(history[index]);
	while (!history[index--].lastInGroup);

	redoIndex = ++index;
	reverseGroup[0].lastInGroup = true;
	for (int i = (int)reverseGroup.size() - 1; i > -1; i--)
		history[index++] = reverseGroup[i];

	reverseGroup.clear();
	return true;
}

bool EntityParser::Redo()
{
	// Nothing to redo
	if (redoIndex == history.size()) return false;

	int index = redoIndex;
	do ExecuteCommand(history[index]);
	while (!history[index++].lastInGroup);

	reverseGroup[0].lastInGroup = true;
	for (ParseCommand& p : reverseGroup)
		history[redoIndex++] = p;

	reverseGroup.clear();
	return true;
}

EntNode* EntityParser::getRoot() { return &root; }

bool EntityParser::wasFileCompressed() { return fileWasCompressed; }

void EntityParser::logAllocatorInfo(bool includeBlockList, bool logToLogger, bool logToFile, const std::string filepath)
{
	std::string msg = "EntNode Allocator\n=====\n";
	msg.append(nodeAlloc.toString(includeBlockList));
	msg.append("\nText Buffer Allocator\n=====\n");
	msg.append(textAlloc.toString(includeBlockList));
	msg.append("\nChild Buffer Allocator\n=====\n");
	msg.append(childAlloc.toString(includeBlockList));
	msg.append("\nRoot Child Buffer: ");
	msg.append(std::to_string(root.childCount));
	msg.append(" / ");
	msg.append(std::to_string(root.maxChildren));
	msg.append(" Slots Filled");
	msg.push_back('\n');

	if (logToLogger)
		EntityLogger::log(msg);

	if(logToFile)
	{
		std::ofstream file(filepath, std::ios_base::binary);
		file.write(msg.data(), msg.length());
		file.close();
	}
}

std::runtime_error EntityParser::Error(std::string msg)
{
	for (const char* inc = ch - 1; inc >= firstChar; inc--) //ch will equal next char to be parsed - if == \n an extra newline would be added
		if (*inc == '\n') errorLine++;
	return std::runtime_error("Entities parsing failed (line " + std::to_string(errorLine) + "): " + msg);
}


void EntityParser::initiateParse(const char* cstring, EntNode* tempRoot, EntNode* parent,
	ParseResult& results)
{
	// Setup variables
	firstChar = cstring;
	errorLine = 1;
	ch = cstring;
	tempChildren.push_back(tempRoot);

	try 
	{
		if(PARSEMODE == ParsingMode::PERMISSIVE)
			parseContentsPermissive();
		else if (PARSEMODE == ParsingMode::JSON) {
			if (parent->nodeFlags & EntNode::NF_Braces)
				parseJsonObject();
			else if (parent->nodeFlags & EntNode::NF_Brackets)
				parseJsonArray();
			else parseJsonRoot();
		}
		else switch (parent->nodeFlags)
		{
			case EntNode::NFC_RootNode:
			parseContentsFile();
			break;

			case EntNode::NFC_ObjSimple:
			if (parent->parent == &root)
				parseContentsEntity();
			else parseContentsLayer();
			break;

			case EntNode::NFC_ObjEntitydef: case EntNode::NFC_ObjCommon:
			parseContentsDefinition();
			break;
		}
		assertLastType(TT_End);
		tempChildren.pop_back();
	}
	catch (std::runtime_error err)
	{
		// If this is the initial parse, don't waste time
		// deallocating everything
		if (tempRoot == parent) {
			throw err;
		}

		// Deallocate and clear everything in the temporary vector
		// Skip over the temporary root node, since we assume it
		// wasn't allocated normally
		for(size_t i = 1; i < tempChildren.size(); i++)
			freeNode(tempChildren[i]);
		tempChildren.clear();

		// Deallocate everything in the temporary root node,
		// then deallocate it's child buffer
		for (int i = 0; i < tempRoot->childCount; i++)
			freeNode(tempRoot->children[i]);
		childAlloc.freeBlock(tempRoot->children, tempRoot->maxChildren);

		results.errorLineNum = errorLine;
		results.errorMessage = err.what();
		results.success = false;
	}
	tempChildren.shrink_to_fit();
}

/* 
Permissive parse function with significantly less error checking for proper token types and arrangements.
It is intended to be as generous as possible, allowing grammars that wouldn't normally work with id's Parsers
to be given a free pass if they're not completely wrong.

This will eventually be used to parse the append menu file, and may see further use if/when EntitySlayer's is
expanded to work with .decl files
*/
void EntityParser::parseContentsPermissive()
{
	size_t childrenStart = tempChildren.size();
	
	LABEL_LOOP:
	Tokenize();

	LABEL_LOOP_SKIP_TOKENIZE:
	switch (lastTokenType)
	{
		default: // End, BraceClose, Assignment, Value_Number
		setNodeChildren(childrenStart);
		return;

		case TT_Comment:
		pushNode(EntNode::NFC_Comment, lastUniqueToken);
		goto LABEL_LOOP;

		case TT_Newline:
		case TT_Semicolon: // Stray semicolons are valid entities/decl syntax
		goto LABEL_LOOP;

		case TT_BraceOpen: // Most decl files begin with a nameless brace pair
		pushNode(EntNode::NFC_ObjSimple, "");
		parseContentsPermissive();
		assertLastType(TT_BraceClose);
		goto LABEL_LOOP;

		/*
		* Many Possibilities:
		* 1. [Identifier/String] [newline | Closing Brace | EOF | Comment]
		* [DONE] 2. [Identifier/String] { }    
		* [DONE] 3. [Identifier/String] = { }
		* [DONE] 4. [Identifier/String] = [Identifier/Value]
		* [DONE] 5. [Identifier/String] = [Idenitifer/Value];
		* [DONE] 6. [Identifier/String] [Identifier/Value]
		* [DONE] 7. [Identifier/String] [Identifier/Value] { }
		*/
		case TT_Identifier: case TT_String: case TT_Number:
		activeID = lastUniqueToken;
		Tokenize();

		if (lastTokenType == TT_BraceOpen) {  // Simple objects
			pushNode(EntNode::NFC_ObjSimple, activeID);
			parseContentsPermissive();
			assertLastType(TT_BraceClose);
			goto LABEL_LOOP;
		}

		// Permit braces to run onto newlines
		else if (lastTokenType == TT_Newline) {
			do {
				Tokenize();
			}
			while(lastTokenType == TT_Newline);

			if (lastTokenType == TT_BraceOpen) {
				pushNode(EntNode::NFC_ObjSimple, activeID);
				parseContentsPermissive();
				assertLastType(TT_BraceClose);
				goto LABEL_LOOP;
			}

			else {
				pushNode(EntNode::NFC_ValueLayer, activeID);
				goto LABEL_LOOP_SKIP_TOKENIZE;
			}
		}

		else if (lastTokenType == TT_EqualSign) {
			Tokenize();

			if (lastTokenType == TT_BraceOpen) { // Common Objects
				pushNode(EntNode::NFC_ObjCommon, activeID);
				parseContentsPermissive();
				assertLastType(TT_BraceClose);
				goto LABEL_LOOP;
			}
			
			else if (lastTokenType == TT_Newline) {
				do { 
					Tokenize();
				}
				while(lastTokenType == TT_Newline);

				if(lastTokenType != TT_BraceOpen)
					throw Error("Expected brace after = and newline");

				pushNode(EntNode::NFC_ObjCommon, activeID);
				parseContentsPermissive();
				assertLastType(TT_BraceClose);
				goto LABEL_LOOP;
			}

			else if (lastTokenType & TTC_PermissiveKey) { // Value assignments
				pushNodeBoth(EntNode::NFC_ValueDarkmetal);
				Tokenize();
				if (lastTokenType == TT_Semicolon) {
					tempChildren.back()->nodeFlags = EntNode::NFC_ValueCommon;
					goto LABEL_LOOP;
				}
				else goto LABEL_LOOP_SKIP_TOKENIZE;
			}

			else throw Error("Unexpected token after = sign");
		}

		else if (lastTokenType & TTC_PermissiveKey) { // Consecutive Identifiers or higher
			pushNodeBoth(EntNode::NFC_ValueFile);
			Tokenize();

			while (lastTokenType == TT_Newline) {
				Tokenize();
			}

			if (lastTokenType == TT_BraceOpen) {
				tempChildren.back()->nodeFlags = EntNode::NFC_ObjSimple; // Changed from ObjEntityDef to fix indentation
				parseContentsPermissive();
				assertLastType(TT_BraceClose);
				goto LABEL_LOOP;
			}
			else goto LABEL_LOOP_SKIP_TOKENIZE;
		}
		else if (lastTokenType != TT_Semicolon) { // EOF, Braceclose, comment
			pushNode(EntNode::NFC_ValueLayer, activeID);
			goto LABEL_LOOP_SKIP_TOKENIZE;
		}
		else throw Error("Unexpected token after identifier or string literal");
	}
}

void EntityParser::parseContentsFile() {
	size_t childrenStart = tempChildren.size();
	LABEL_LOOP:
	Tokenize();
	if (lastTokenType == TT_Comment)
	{
		pushNode(EntNode::NFC_Comment, lastUniqueToken);
		goto LABEL_LOOP;
	}
	if (lastTokenType != TT_Identifier)
	{
		setNodeChildren(childrenStart);
		return;
	}

	activeID = lastUniqueToken;
	TokenizeAdjustValue();
	switch (lastTokenType)
	{
		case TT_Number:
		case TT_String: case TT_Keyword:
		pushNodeBoth(EntNode::NFC_ValueFile);
		break;

		case TT_BraceOpen:
		pushNode(EntNode::NFC_ObjSimple, activeID);
		parseContentsEntity();
		assertLastType(TT_BraceClose);
		break;

		default:
		throw Error("Invalid token (File function)");
	}
	goto LABEL_LOOP;
}

void EntityParser::parseContentsEntity() {
	size_t childrenStart = tempChildren.size();
	LABEL_LOOP:
	Tokenize();
	if (lastTokenType == TT_Comment)
	{
		pushNode(EntNode::NFC_Comment, lastUniqueToken);
		goto LABEL_LOOP;
	}
	if (lastTokenType == TT_String) // Need this specifically for pvp_darkmetal
	{
		activeID = lastUniqueToken;
		assertIgnore(TT_EqualSign);
		assertIgnore(TT_String);
		pushNodeBoth(EntNode::NFC_ValueDarkmetal);
		goto LABEL_LOOP;
	}
	if(lastTokenType != TT_Identifier)
	{
		setNodeChildren(childrenStart);
		return;
	}
	activeID = lastUniqueToken;
	TokenizeAdjustValue();
	switch (lastTokenType)
	{
		case TT_Identifier:
		assertIgnore(TT_BraceOpen);
		pushNodeBoth(EntNode::NFC_ObjEntitydef);
		parseContentsDefinition();
		assertLastType(TT_BraceClose);
		break;

		case TT_BraceOpen:
		pushNode(EntNode::NFC_ObjSimple, activeID);
		parseContentsLayer();
		assertLastType(TT_BraceClose);
		break;

		case TT_EqualSign:
		TokenizeAdjustValue();
		if((lastTokenType & TTC_EntityValues) == 0)
			throw Error("Value expected (entity function)");
		pushNodeBoth(EntNode::NFC_ValueCommon);
		assertIgnore(TT_Semicolon);
		break;

		default:
		throw Error("Bad token type (Entity function");
	}
	goto LABEL_LOOP;
}

void EntityParser::parseContentsLayer() {
	size_t childrenStart = tempChildren.size();

	Tokenize();
	while (lastTokenType & (TT_Comment | TT_String)) {
		pushNode(EntNode::NFC_Comment, lastUniqueToken);
		Tokenize();
	}
	setNodeChildren(childrenStart);
}

void EntityParser::parseContentsDefinition()
{
	size_t childrenStart = tempChildren.size();
	LABEL_LOOP:
	Tokenize();
	if (lastTokenType == TT_Comment)
	{
		pushNode(EntNode::NFC_Comment, lastUniqueToken);
		goto LABEL_LOOP;
	}
	if(lastTokenType != TT_Identifier && lastTokenType != TT_String)
	{
		setNodeChildren(childrenStart);
		return;
	}
	assertIgnore(TT_EqualSign);
	activeID = lastUniqueToken;
	TokenizeAdjustValue();
	switch (lastTokenType)
	{
		case TT_BraceOpen:
		pushNode(EntNode::NFC_ObjCommon, activeID);
		parseContentsDefinition();
		assertLastType(TT_BraceClose);
		break;

		case TT_Number: case TT_IndyHex:
		case TT_String: case TT_Keyword:
		pushNodeBoth(EntNode::NFC_ValueCommon);
		assertIgnore(TT_Semicolon);
		break;

		default:
		throw Error("Bad token type (definition function)");
	}
	goto LABEL_LOOP;
}

void EntityParser::parseJsonRoot()
{
	size_t childrenStart = tempChildren.size();
	TokenizeAdjustJson();

	switch (lastTokenType)
	{
		case TT_Keyword:
		case TT_Number:
		case TT_String:
		pushNode(0, lastUniqueToken);
		break;

		case TT_BraceOpen:
		pushNode(EntNode::NF_Braces, "");
		parseJsonObject();
		assertLastType(TT_BraceClose);
		break;

		case TT_BracketOpen:
		pushNode(EntNode::NF_Brackets, "");
		parseJsonArray();
		assertLastType(TT_BracketClose);
		break;

		default:
		throw Error("Bad Token Type JSON Root Function");
	}
	setNodeChildren(childrenStart);
	Tokenize(); // Ensures the last type is the end
}

void EntityParser::parseJsonObject()
{
	size_t childrenStart = tempChildren.size();

	while (true) {
		Tokenize();

		// This conditional lets us parse the json whether or not there's
		// a trailing comma - good for editing
		if(lastTokenType != TT_String)
			goto LOOP_EXIT;
		activeID = lastUniqueToken;

		assertIgnore(TT_Colon);
		TokenizeAdjustJson();
		switch (lastTokenType)
		{
			case TT_Keyword:
			case TT_Number:
			case TT_String:
			pushNodeBoth(EntNode::NF_Colon | EntNode::NF_Comma);
			break;
			
			case TT_BraceOpen:
			pushNode(EntNode::NF_Colon | EntNode::NF_Braces | EntNode::NF_Comma, activeID);
			parseJsonObject();
			assertLastType(TT_BraceClose);
			break;

			case TT_BracketOpen:
			pushNode(EntNode::NF_Colon | EntNode::NF_Brackets | EntNode::NF_Comma, activeID);
			parseJsonArray();
			assertLastType(TT_BracketClose);
			break;

			default:
			throw Error("Invalid Value token JSON Object function");
		}
		Tokenize();
		if(lastTokenType != TT_Comma)
			goto LOOP_EXIT;
	}

	LOOP_EXIT:
	if(tempChildren.size() > childrenStart)
		tempChildren.back()->nodeFlags &= ~EntNode::NF_Comma;
	setNodeChildren(childrenStart);
}

void EntityParser::parseJsonArray()
{
	size_t childrenStart = tempChildren.size();

	while (true) {
		TokenizeAdjustJson();
		switch (lastTokenType)
		{
			case TT_Keyword:
			case TT_Number: 
			case TT_String:
			pushNode(EntNode::NF_Comma, lastUniqueToken);
			break;

			case TT_BraceOpen:
			pushNode(EntNode::NF_Braces | EntNode::NF_Comma, "");
			parseJsonObject();
			assertLastType(TT_BraceClose);
			break;

			case TT_BracketOpen:
			pushNode(EntNode::NF_Brackets | EntNode::NF_Comma, "");
			parseJsonArray();
			assertLastType(TT_BracketClose);
			break;
			
			default:
			goto LOOP_EXIT;
		}
		Tokenize();
		if(lastTokenType != TT_Comma)
			goto LOOP_EXIT;
	}
	LOOP_EXIT:
	if (tempChildren.size() > childrenStart)
		tempChildren.back()->nodeFlags &= ~EntNode::NF_Comma;
	setNodeChildren(childrenStart);
}

void EntityParser::freeNode(EntNode* node)
{
	// Free the allocated text block
	textAlloc.freeBlock(node->textPtr, node->nameLength + node->valLength);

	// Free the node's children and the pointer block listing them
	if (node->childCount > 0)
	{
		for (int i = 0; i < node->childCount; i++)
			freeNode(node->children[i]);
		childAlloc.freeBlock(node->children, node->maxChildren);
	}

	/*
	* We should ensure node values are reset to default when freeing them.
	* Otherwise we must operate under the assumption that some data is inaccurate
	* when working with EntityNode instance methods.
	*/
	new (node) EntNode;
	nodeAlloc.freeBlock(node, 1);
}

void EntityParser::pushNode(const uint16_t p_flags, const std::string_view p_name)
{
	EntNode* n = nodeAlloc.reserveBlock(1);
	n->textPtr = textAlloc.reserveBlock(p_name.length());
	n->nameLength = p_name.length();
	n->nodeFlags = p_flags;

	memcpy(n->textPtr, p_name.data(), p_name.length());

	tempChildren.push_back(n);
}

void EntityParser::pushNodeBoth(const uint16_t p_flags)
{
	EntNode* n = nodeAlloc.reserveBlock(1);
	n->textPtr = textAlloc.reserveBlock(activeID.length() + lastUniqueToken.length());
	n->nameLength = activeID.length();
	n->valLength = lastUniqueToken.length();
	n->nodeFlags = p_flags;

	memcpy(n->textPtr, activeID.data(), activeID.length());
	memcpy(n->textPtr + activeID.length(), lastUniqueToken.data(), lastUniqueToken.length());

	tempChildren.push_back(n);
}

void EntityParser::setNodeChildren(const size_t startIndex)
{
	size_t s = tempChildren.size();
	size_t childCount = s - startIndex;
	EntNode* parent = tempChildren[startIndex - 1];
	parent->childCount = (int)childCount;
	parent->maxChildren = OptimalMaxChildCount(parent->childCount);
	parent->children = childAlloc.reserveBlock(parent->maxChildren);

	// Fill child buffer, assign parent values to children
	EntNode **childrenPtr = parent->children, 
			**max = childrenPtr + childCount,
			**tempPtr = tempChildren.data() + startIndex;
	while (childrenPtr < max) { 
		(*tempPtr)->parent = parent;
		*childrenPtr++ = *tempPtr++;
	}
	tempChildren.resize(startIndex);
}

void EntityParser::assertLastType(uint32_t requiredType)
{
	if (lastTokenType != requiredType)
		throw Error("Bad Token Type assertLast");
}

void EntityParser::assertIgnore(uint32_t requiredType)
{
	Tokenize();
	if (lastTokenType != requiredType)
		throw Error("Bad token type assertIgnore");
}

void EntityParser::TokenizeAdjustValue()
{
	Tokenize();
	/*
	* A fun little micro-optimization that actually seems to make few-milliseconds difference
	* It's a simple logic: ensure the CPU is performing as many comparisons simultaneously as possible
	* Check for false first, since statistically speaking entities files have it more frequently than true
	*/
	if (lastTokenType == TT_Identifier) {
		const char* raw = lastUniqueToken.data();
		size_t len = lastUniqueToken.length();

		if (len == 5 && memcmp(raw, "false", 5) == 0)
			lastTokenType = TT_Keyword;
		else if (len == 4 && memcmp(raw, "true", 4) == 0 || memcmp(raw, "NULL", 4) == 0)
			lastTokenType = TT_Keyword;
	}
}

void EntityParser::TokenizeAdjustJson()
{
	/* Same as TokenizeAdjustValue except JSON has "null" written in lowercase instead of uppercase */
	Tokenize();
	if (lastTokenType == TT_Identifier) {
		const char* raw = lastUniqueToken.data();
		size_t len = lastUniqueToken.length();

		if (len == 5 && memcmp(raw, "false", 5) == 0)
			lastTokenType = TT_Keyword;
		else if (len == 4 && memcmp(raw, "true", 4) == 0 || memcmp(raw, "null", 4) == 0)
			lastTokenType = TT_Keyword;
	}
}

void EntityParser::Tokenize() 
{
	// Faster than STL isalpha(char) and isdigit(char) functions
	#define isLetter (((unsigned int)(*ch | 32) - 97) < 26U)
	#define isNum (((unsigned)*ch - '0') < 10u)
	const char* first; // Ptr to start of current identifier/value token

	LABEL_TOKENIZE_START:
	switch (*ch) // Not auto-incrementing should eliminate unnecessary arithmetic operations
	{                     // at the cost of needing to manually it++ in additional areas
		case '\r':
		if(*++ch != '\n')
			throw Error("Expected line feed after carriage return");
		case '\n':
		if (PARSEMODE == ParsingMode::PERMISSIVE) {
			ch++;
			lastTokenType = TT_Newline;
			return;
		}
		case ' ': case '\t':
		ch++;
		goto LABEL_TOKENIZE_START;

		case '\0': // Appending null character to end of string simplifies this function's bounds-checking
		lastTokenType = TT_End; // Do not it++ here to prevent out of bounds error
		return;

		case ';':
		lastTokenType = TT_Semicolon;
		ch++;
		return;

		case ':':
		lastTokenType = TT_Colon;
		ch++;
		return;

		case '[':
		lastTokenType = TT_BracketOpen;
		ch++;
		return;

		case ']':
		lastTokenType = TT_BracketClose;
		ch++;
		return;

		case '{':
		lastTokenType = TT_BraceOpen;
		ch++;
		return;

		case '}':
		lastTokenType = TT_BraceClose;
		ch++;
		return;

		case '=':
		lastTokenType = TT_EqualSign;
		ch++;
		return;

		case ',':
		lastTokenType = TT_Comma;
		ch++;
		return;

		case ')':
		lastTokenType = TT_Parenclose;
		ch++;
		return;

		case '(':
		{
			first = ch++;
			int numvalues = 0;
			
			LABEL_TUPLE_START:
			Tokenize();
			switch (lastTokenType)
			{
				case TT_Identifier: // declType( keyword )
				case TT_Number:
				case TT_Tuple:
				case TT_Comma: // TODO: Improve this?
				goto LABEL_TUPLE_START;

				case TT_Parenclose:
				lastUniqueToken = std::string_view(first, (size_t)(ch - first));
				lastTokenType = TT_Tuple;
				return;

				default:
				throw Error("Invalid format for a tuple");
				break;
			}
		}

		case '/':
		first = ch;
		if (*++ch == '/') {
			LABEL_COMMENT_START:
			switch (*++ch)
			{
				case '\n': case '\r': case '\0':
				lastTokenType = TT_Comment;
				lastUniqueToken = std::string_view(first, (size_t)(ch - first));
				return;

				default:
				goto LABEL_COMMENT_START;
			}
		}
		else if (*ch == '*') { // Don't increment here
			LABEL_COMMENT_MULTILINE_START:
			switch (*++ch) 
			{
				case '\0':
				throw Error("No end to multiline comment");

				case '*':
				if (*(ch + 1) != '/')
					goto LABEL_COMMENT_MULTILINE_START;
				ch += 2;
				lastTokenType = TT_Comment;
				lastUniqueToken = std::string_view(first, (size_t)(ch - first));
				return;

				default:
				goto LABEL_COMMENT_MULTILINE_START;
			}
		}
		else throw Error("Invalid Comment Syntax");


		case '"':
		first = ch;
		LABEL_STRING_START:
		switch (*++ch)
		{
			case '"':
			lastTokenType = TT_String;
			lastUniqueToken = std::string_view(first, (size_t)(++ch - first)); // Increment past quote to set to next char
			return;

			case '\r': case '\n': case '\0': // Again, relies on string ending in null character
			throw Error("No end-quote to complete string literal");

			case '\\':
			if (PARSEMODE == ParsingMode::JSON) {
				if (*(ch + 1) == '"') ch++;
			}
			
			goto LABEL_STRING_START;

			default:
			goto LABEL_STRING_START;
		}

		case '<':
		if(PARSEMODE != ParsingMode::PERMISSIVE)
			throw Error("Verbatim strings are for permissive mode only");
		first = ch;
		if(*++ch != '%')
			throw Error("Bad start to verbatim string");
		LABEL_VERBATIMSTRING_START:
		switch (*++ch)
		{
			case '%':
			if(*(ch+1) != '>')
				goto LABEL_VERBATIMSTRING_START;
			ch += 2; // Increment past > to set to next char
			lastTokenType = TT_String;
			lastUniqueToken = std::string_view(first, (size_t)(ch - first));
			return;

			case '\0':
			throw Error("No end to verbatim string");

			default:
			goto LABEL_VERBATIMSTRING_START;
		}

		case '$':
		{
			first = ch;

			// Multiple prefixes in one conditional creates spooky, undefined behavior
			if(*(ch + 1) != '0' || *(ch + 2) != 'x')
				throw Error("Bad start to dollar sign hexadecimal");
			ch += 3;

			LABEL_HEX_START:
			if (isNum || (*ch >= 'A' && *ch <= 'F') || (*ch >= 'a' && *ch <='f')) {
				ch++;
				goto LABEL_HEX_START;
			}

			Tokenize();
			if (lastTokenType != TT_Number) {
				throw Error("Number expected after dollar sign hex");
			}
				
			// The optimistic approach: Given that float fields will randomly not use hex numbers
			// throughout the Indiana entities files, I will assume they're entirely optional and
			// opt to ignore them in favor of the decimal number tokens. This approach ensures
			// compatability with existing spawnPosition/spawnOrientation related code

			//lastTokenType = TT_IndyHex;
			//lastUniqueToken = std::string_view(first, (size_t)(ch-first));
			return;
		}

		// NOTES:
		// 1. PERMANENT: Negative sign can't be seperated from the number by whitespace
		// 2. CHANGE IN THE FUTURE (?): Number token cannot begin with a period.
		case '-': //case '.':
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
		{
			bool hasDot = false;
			first = ch;
			LABEL_NUMBER_START:
			switch (*++ch)
			{
				case '0': case '1': case '2': case '3': case '4':
				case '5': case '6': case '7': case '8': case '9':
				goto LABEL_NUMBER_START;

				case '.':
				if(hasDot)
					throw Error("Decimal numbers can't have multiple periods.");
				hasDot = true;
				goto LABEL_NUMBER_START;

				case 'e': case 'E': case'+': case '-':
				goto LABEL_NUMBER_START;

				default:
				lastTokenType = TT_Number;
				lastUniqueToken = std::string_view(first, (size_t)(ch - first));
				return;
			}
			// Unimplemented remnants of previous version
			//if (hasDot && it - 1 == start)
			//	throw Error("Decimal numbers need at least one digit");
			//if (minusToken)
			//	lastUniqueToken = new string('-' + rawText.substr(start, it - start)); // Possibly optimize?
			//else
			//	lastUniqueToken = new string(rawText, start, it - start);
		}

		default:
		{
			if (!isLetter && *ch != '_') {
				if(*ch != '%' || PARSEMODE != ParsingMode::PERMISSIVE)
					throw Error("Unrecognized character");
			}
				
			first = ch;

			LABEL_ID_START:
			switch (*++ch)
			{
				case '[':
				LABEL_ID_BRACKET_START:
				switch(*++ch)
				{
					case '0': case '1': case '2': case '3': case '4':
					case '5': case '6': case '7': case '8': case '9':
					goto LABEL_ID_BRACKET_START;
					break;

					case ']':
					if(*(ch++ - 1) != '[')
						break;
					default:
					throw Error("Improper bracket usage in identifer");
				}
				break;

				case '(': // declType(keyword)
				Tokenize();
				break;

				default:
				if (isLetter || isNum || *ch == '_')
					goto LABEL_ID_START;
				if(*ch == '%' && PARSEMODE == ParsingMode::PERMISSIVE)
					goto LABEL_ID_START;
				break;
			}
			lastUniqueToken = std::string_view(first, (size_t)(ch - first));
			lastTokenType = TT_Identifier;
			return;
		}
	}
};

void EntityParser::FilteredSearch(const std::string& key, bool backwards, bool caseSensitive, bool exactLength) 
{
	//EntNode* startAfter = (EntNode*)view->GetCurrentItem().GetID();
	//if(startAfter == nullptr)
	//	startAfter = &root;
	//
	//EntNode* firstResult = nullptr; // Use this to prevent infinite loops from rejection of filtered results
	//EntNode* result;
	//while (true)
	//{
	//	/*
	//	* TODO: Ideally, we shouldn't be searching through filtered out nodes in the first place. If a massive
	//	* amount of nodes are filtered out on large files, search can take significant time to complete
	//	*/
	//	if (backwards)
	//		result = startAfter->searchUpwards(key, caseSensitive, exactLength);
	//	else result = startAfter->searchDownwards(key, caseSensitive, exactLength, nullptr);

	//	if (result == EntNode::SEARCH_404 || result == firstResult) {
	//		EntityLogger::log("Could not find key");
	//		return;
	//	}
	//	//wxLogMessage("%s", result->getNameWX());

	//	// Root should never have text, so this should be safe
	//	if (result->isFiltered()) {
	//		wxDataViewItem item(result);
	//		view->UnselectAll();
	//		view->Select(item);
	//		if(result->childCount > 0)
	//			view->Expand(item);
	//		view->EnsureVisible(item);
	//		//wxLogMessage("Found");
	//		return;
	//	}

	//	// Set sentinel in first loop iteration
	//	if(firstResult == nullptr) 
	//		firstResult = result;
	//	startAfter = result;
	//}
}