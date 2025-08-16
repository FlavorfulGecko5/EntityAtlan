/*
* This class is the centerpiece of EntitySlayer's backend
* 
* It is a very large class, where each function and member serves
* 1 of 3 distinct purposes:
* 
* 1. Parser - Constructs and maintains a tree of EntNodes from raw text read from files
* 2. Commander - Validates and performs modifications to the node tree,
*	logging the history of these edits to support undo/redo operations
* 3. Model - Communicates with the wxWidgets frontend to determine how the node
*    tree is presented to the user, using wxDataViewModel overrides and a filter system
*/

#include <string_view>
#include <vector>
#include <set>
#include "EntityNode.h"
#include "GenericBlockAllocator.h"

struct Sphere {
	float x = 0;
	float y = 0;
	float z = 0;
	float r = 0; // Radius
};

struct ParseResult {
	bool success = true;
	size_t errorLineNum = 0;
	std::string errorMessage;
};

enum class ParsingMode {
	ENTITIES,
	PERMISSIVE,
	JSON
};

class FilterCtrl;
class EntityParser {

	public:
	~EntityParser()
	{
		//delete[] rootchild_filter;
	}

	/*
	* =====================
	* NODE TREE DATA STORAGE
	* =====================
	*/
	private:
	ParsingMode PARSEMODE;
	bool fileWasCompressed;
	EntNode root = EntNode(EntNode::NFC_RootNode);
	BlockAllocator<char> textAlloc = BlockAllocator<char>(1000000);         // Allocator for node name/value buffers
	BlockAllocator<EntNode> nodeAlloc = BlockAllocator<EntNode>(1000);      // Allocator for the nodes
	BlockAllocator<EntNode*> childAlloc =  BlockAllocator<EntNode*>(30000); // Allocator for node child buffers

	// Internally tracks whether edits have been written to a file
	bool fileUpToDate = true;
	size_t lastUncompressedSize = 0;

	/* Accessors */
	public:
	EntNode* getRoot();
	bool wasFileCompressed();
	bool FileUpToDate() { return fileUpToDate;}
	ParsingMode getMode() { return PARSEMODE; };
	void SwitchParseMode(ParsingMode newMode) {PARSEMODE = newMode;}

	/* For Debugging */
	void logAllocatorInfo(bool includeBlockList, bool logToLogger, bool logToFile, const std::string filepath = "");

	void MarkFileOutdated() {
		fileUpToDate = false;
	}

	void WriteToFile(const std::string& filepath, bool compress) {
		lastUncompressedSize = root.writeToFile(filepath, lastUncompressedSize + 10000, compress, false);
		fileUpToDate = true;
	}

	/*
	* ==================
	* PURPOSE #1: PARSING
	* ==================
	*/

	/*
	* Variables used during a parse.
	* Their values should be reset or cleared at the start/end of each parse
	*/
	private:
	const char* firstChar = nullptr;            // Ptr to the cstring we're parsing
	const char* ch = nullptr;                   // Ptr to next char to be parsed
	uint32_t lastTokenType;                     // Type of the last-parsed token
	std::string_view lastUniqueToken;			// Stores most recent identifier or value token
	std::string_view activeID;					// Second-most-recent token (typically an identifier)
	size_t errorLine = 1;                       // If a grammar error is detected, this is the line it was found on

	// Every node generated during the current parse (except the root node)
	// is inside here, or childed to a node inside here, until the moment it's
	// made a child of the root node. Hence, when cancelling a parse due to an exception:
	// 1. Free all nodes stored here then clear the vector
	// 2. Free the root node's children and childArray
	std::vector<EntNode*> tempChildren;


	public:
	/* Constructs an EntityParser with minimal data */
	EntityParser();

	EntityParser(ParsingMode mode) : fileWasCompressed(false), PARSEMODE(mode) {}

	/*
	* Constructs an EntityParser containing fully parsed data from the given file
	* @param filepath .entites file to parse
	* @param mode Parsing mode that will be followed
	* @param debug_logParseTime If true, outputs execution time data
	* @throw runtime_error thrown when the file cannot be parsed
	*/
	EntityParser(const std::string& filepath, const ParsingMode mode, const bool debug_logParseTime = false);

	private:
	/*
	* Creates an exception for a supplied parsing error
	* The returned exception should be thrown immediately
	*/
	std::runtime_error Error(std::string msg);

	/*
	* RECURSIVE PARSING FUNCTIONS
	*/

	// TODO: Get rid of intiateParse somehow - it's sloppy (or not - we may need it when we have multiple parsing modes)
	// Consider renaming these other functions?

	/*
	* Begin parsing a text string
	* @param cstring - The null-terminated string to parse
	*/
	void initiateParse(const char* cstring, EntNode* tempRoot, EntNode* parent, ParseResult& results);
	void parseContentsFile();
	void parseContentsEntity();
	void parseContentsLayer();
	void parseContentsDefinition();
	void parseContentsPermissive();
	void parseJsonRoot();
	void parseJsonObject();
	void parseJsonArray();

	/*
	* ALLOCATION / DEALLOCATION FUNCTIONS
	*/
	void freeNode(EntNode* node);

	void pushNode(const uint16_t p_flags, const std::string_view p_name);
	void pushNodeBoth(const uint16_t p_flags);
	 
	void setNodeChildren(const size_t startIndex);


	/*
	* TOKENIZATION FUNCTIONS
	*/

	/* Throws an error if the last - parsed token is not of the required type */
	inline void assertLastType(uint32_t requiredType);

	/*
	 Parses raw text for the next token
	 Throws an error if the token is not of the required type
	*/
	inline void assertIgnore(uint32_t requiredType);

	/*
	 Parses raw text for the next token
	 If it's an identifier, distinguish whether it's a true ID or special keyword value
	*/
	inline void TokenizeAdjustValue();
	inline void TokenizeAdjustJson();

	/* Parses raw text for the next token, writes results to instance variables */
	void Tokenize(); 


	/*
	* ===================
	* PURPOSE #2: COMMANDING
	* ===================
	*/
	private:
	enum class CommandType : unsigned char
	{
		UNDECLARED,
		EDIT_TREE,
		EDIT_TEXT,
		EDIT_POSITION
	};

	struct ParseCommand
	{
		std::string text = "";                      // Text we must parse for this command
		int parentID = 0;                           // Positional id of the parent node
		int insertionIndex = 0;                     // Purpose varies depending on command type
		int removalCount = 0;                       // Purpose varies depending on command type
		bool lastInGroup = false;                   // If true, this is the last command of this group command
		CommandType type = CommandType::UNDECLARED; // Determines what operation we perform
	};

	/*
	* Variables for tracking command history
	* 
	* The Command Pattern is implemented privately, to support undo/redo on groups of editing operations
	* 
	* Publicly, users call a handful of functions to edit the node tree, without needing to construct command objects
	* These actions should be automatically converted into the appropriate undo/redo action by the called function
	*/
	private:
	std::vector<ParseCommand> history;      // Command history, serves as the undo/redo stack
	const size_t MAX_COMMAND_MEMORY = 100;  // Maximum number of command groups that will be stored in the history before old ones are deleted
	int commandCount = 0;                   // Current number of command groups being stored in the undo/redo stack                   
	int redoIndex = 0;                      // Current position on the undo/redo stack.
	std::vector<ParseCommand> reverseGroup; // Reverse of the current command group incase we must cancel

	/*
	* Functions related to the command history
	*/

	private:
	/* Executes the given command object */
	void ExecuteCommand(ParseCommand& cmd);

	public:
	/* Pushes the current command group into the history and starts a new command group */
	void PushGroupCommand();

	/* Undoes the results of the current command group, resetting it without placing it into the history */
	void CancelGroupCommand();

	/* Clears the command history */
	void ClearHistory();

	bool Undo();
	bool Redo();

	/*
	* Tree Editing Operations
	*/
	public:
	
	/*
	* Edits a node's text without validating the contents
	* Should be called only by undo/redo and other scenarios where we know the new text is valid
	* @param text Text replacing the node's original text
	* @param node Node whose text we're editing
	* @param nameLength Length of the node's new name string
	* @param highlight If true, the GUI will highlight the edited node
	*/
	void EditText(const std::string& text, EntNode* node, int nameLength, bool highlight);

	public:
	/*
	* Parses a given string of text and replaces a pre-existing block of EntNodes
	* belonging to the same parent.
	* @param text Text to parse
	* @param parent Node whose children we're editing
	* @param insertionIndex Index to insert new nodes at
	* @param removeCount Number of nodes to delete, starting at insertionIndex
	* @param renumberLists If true, perform automatic list renumbering
	* @param highlightNew If true, the GUI will highlight newly created nodes
	*/
	ParseResult EditTree(const std::string& text, EntNode* parent, int insertionIndex, int removeCount, bool renumberLists, bool highlightNew);

	/*
	* Moves a node's child to a different index. The other children are shifted up/down to fill the original slot
	* @param parent The node whose child we're moving
	* @param childIndex Index of the child we're moving
	* @param insertionIndex Index the child is being moved too
	* @param highlight If true, the GUI will highlight the moved node
	*/
	void EditPosition(EntNode* parent, int childIndex, int insertionIndex, bool highlight); // TODO: ADD LIST RENUMBERING PARAM

	/*
	* Perform automatic idList renumbering on a node's children
	* @param parent Node whose children operating on
	* @param recursive If true, performs this operation on all of this node's descendants
	* @param highlight If true, the GUI will highlight modified nodes
	*/
	void fixListNumberings(EntNode* parent, bool recursive, bool highlight);

	//void EditName(std::string text, EntNode* node);
	//void EditValue(std::string text, EntNode* node);

	/*
	* ===================
	* PURPOSE #3: MODELING
	* ===================
	*/

	private:
	static const std::string FILTER_NOLAYERS;
	static const std::string FILTER_NOCOMPONENTS;

	// To represent filtering we have a buffer of booleans, the same length as the root child buffer
	// If a node passes all the filters, it's boolean value is true. If a node is filtered out, it's value is false
	// We refactored the filter system to this, replacing it's integration into the wxDataViewModel's GetChildren function
	// for a multitude of reasons:
	// 1. Performance - GetChildren is called a lot, resulting in many wasteful filter evaluations on all root children
	//	These become very problematic for large operations that add lots of entities at once
	// 2. Prevents debug errors and (possibly) memory leaks. If a new entity was created that didn't pass the filters, it would
	// be filtered out on-committ and a debug error throne from trying to add it to the tree.
	// 3. It's a relatively simple solution that will stay localized to the parser's core command-execution functions.
	// 
	// Using this new system we have full control over when we notify the control of changes - allowing us to cancel alerts
	// for filtered-out nodes. With debug errors gone, we also no longer need to clear the parser's undo/redo history
	// whenever we change filters
	// 
	// DEPRECATED: Filter system has been refactored to local node variables. However, the same logic as above still applies
	//bool* rootchild_filter = nullptr; // Should be same size as child buffer. true = include, false = exclude

	public:

	/*
	* FILTER FUNCTIONS
	*/
	public:


	void FilteredSearch(const std::string& key, bool backwards, bool caseSensitive, bool exactLength);
};