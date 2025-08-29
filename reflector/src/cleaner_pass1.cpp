#include "cleaner.h"
#include <string_view>
#include <string>
#include <fstream>
#include <exception>
#include <vector>
#include <cassert>
#include <unordered_map>

std::string CleanName(const std::string_view from) {
	std::string to = std::string(from);

    for (char* c = to.data(), *max = to.data() + to.size(); c < max; c++) {
        switch (*c) {
            case ':': case ' ': case ',': case '*': case '-':
            *c = '_';
            break;

            case '<': case '>':
            *c = 'T';
            break;

            // Mostly in function pointers which we filter out
            // A few make it through into template types - should be fine
            case '&':
            *c = 'R';
            break;
        }
    }

	return to;
}

bool FindFlag(std::string_view flags, std::string_view target) {
    size_t pos = flags.find(target);

    while (pos != std::string_view::npos) {
        // Verify the left side is delimited
        if (pos != 0) {
            switch (flags[pos - 1]) {
                case ' ': case '\t': case '|':
                break;

                default:
                pos = flags.find(target, pos + 1);
                continue;
            }
        }

        // Verify the right side is delimited
        if (pos + target.length() != flags.length()) {
            switch (flags[pos + target.length()]) {
                case ' ': case '\t': case '|':
                break;

                default:
                pos = flags.find(target, pos + 1);
                continue;
            }
        }

        return true;
    }
    return false;
}

bool TokenizeFlags(std::string_view comment) {
    std::string_view flags = comment.substr(2); // Exclude the // chars

    // This string should only contain all-caps, or's and whitespace
    // Otherwise it's a developer comment
    for (char c : flags) {
        switch (c) {
            case '|': case ' ': case '\t':
            continue;

            default:
            if(c < 'A' && c > 'Z' && c != '_')
                return false;
            continue;
        }
    }

    if(FindFlag(flags, "EDIT"))
        return true;
    if(FindFlag(flags, "DESIGN"))
        return true;
    if(FindFlag(flags, "DEF"))
        return true;
    return false;
}

enum TokenType {
	TT_End,
	TT_Colon,
	TT_Equals,
	TT_Comma,
	TT_BraceOpen,
	TT_BraceClose,
	TT_Semicolon,
	TT_Asterisk,

    TT_Comment,
	TT_ParenthBlock,
	TT_TemplateBlock,
	TT_BracketBlock,
    TT_Number,
	TT_Keyword,
};

struct ParsedToken {
	TokenType type;
	std::string_view data;
};

class idlibCleaner {
	private:
	char* text = nullptr;
	char* ch = nullptr;
	size_t length = 0;
	size_t currentLine = 1;

	std::string cleanenums = "enums {\n";
    std::string cleantemplatesubs = "templatesubs {\n";
	std::string cleanstructs = "structs {\n";
    std::unordered_map<std::string, std::string> cleantemplates;

	public:
	~idlibCleaner() {
		delete[] text;
	}

    std::exception Error(const char* message) {
        printf("%zu %s\n", currentLine, message);
        throw std::exception(message);
    }

	idlibCleaner();

	ParsedToken Tokenize();
	void TokenAssert(TokenType type);
	void BuildStruct(std::string_view hashHex);
	void BuildEnum(std::string_view hashHex);
	void Build();
	void Write();
};

idlibCleaner::idlibCleaner() {
	std::ifstream fstream = std::ifstream("idlib.h", std::ios_base::binary);
	if (!fstream.is_open()) {
		printf("Could not open idlib\n");
		assert(0);
	}

	fstream.seekg(0, std::ios_base::end);
	length = static_cast<size_t>(fstream.tellg());
	text = new char[length + 1]; // Room for a null char
	text[length] = '\0';
	ch = text;
	fstream.seekg(0, std::ios_base::beg);
	fstream.read(text, length);
	fstream.close();

	cleanenums.reserve(1000000);
    cleanstructs.reserve(5000000);
    cleantemplatesubs.reserve(1000000);
    cleantemplates.reserve(200);
}

ParsedToken idlibCleaner::Tokenize() {
    // Faster than STL isalpha(char) and isdigit(char) functions
    #define isLetter (((unsigned int)(*ch | 32) - 97) < 26U)
    #define isNum (((unsigned)*ch - '0') < 10u)
    const char* first; // Ptr to start of current identifier/value token
    LABEL_TOKENIZE_START:
    switch (*ch) {

        case '\0':
        return {TT_End};

        case '=':
        ch++;
        return {TT_Equals};

        case ':':
        if(*(ch+1) == ':')
            goto LABEL_FORCE_DEFAULT;
        ch++;
        return {TT_Colon};

        case '*':
        ch++;
        return {TT_Asterisk, std::string_view(ch - 1, 1) }; // Need this to properly form a type token

        case ',':
        ch++;
        return {TT_Comma};

        case ';':
        ch++;
        return {TT_Semicolon};

        case '{':
        ch++;
        return {TT_BraceOpen};

        case '}':
        ch++;
        return {TT_BraceClose};

        case ' ': case '\t':
        ch++;
        goto LABEL_TOKENIZE_START;

        case '\r':
        if (*++ch != '\n')
            throw Error("Expected line feed after carriage return");
        case '\n':
        ch++;
        currentLine++;
        goto LABEL_TOKENIZE_START;

        case '/': {
            char* first = ch;
            if (*++ch == '/') {
                LABEL_COMMENT_START:
                switch (*++ch)
                {
                    // Todo: Optimize to skip newline?
                    case '\r': case '\n': case '\0':
                    return { TT_Comment, std::string_view(first, ch - first) };

                    default:
                    goto LABEL_COMMENT_START;
                }
            }
            else throw Error("Invalid Comment Syntax");
        }


        case '(':
        {
            first = ch;
            int parenStack = 1;
            LABEL_PARENBLOCK_START:
            switch (*++ch)
            {
                case '(':
                parenStack++;
                goto LABEL_PARENBLOCK_START;

                case ')':
                parenStack--;
                if (parenStack == 0) {
                    return {TT_ParenthBlock, std::string_view(first, ++ch - first)};
                }
                goto LABEL_PARENBLOCK_START;

                case '\r': case '\n': case '\0':
                throw Error("No end to parenth block");

                default:
                goto LABEL_PARENBLOCK_START;
            }
        }

        case '[':
        {
            first = ch;
            int brackStack = 1;
            LABEL_BRACKBLOCK_START:
            switch (*++ch)
            {
                case '[':
                brackStack++;
                goto LABEL_BRACKBLOCK_START;

                case ']':
                brackStack--;
                if (brackStack == 0) {
                    return {TT_BracketBlock, std::string_view(first, ++ch - first)};
                }
                goto LABEL_BRACKBLOCK_START;

                case '\r': case '\n': case '\0':
                throw Error("No end to bracket block");

                default:
                goto LABEL_BRACKBLOCK_START;
            }
        }

        case '<':
        {
            first = ch;
            int tempStack = 1;
            LABEL_TEMPBLOCK_START:
            switch (*++ch)
            {
                case '<':
                if (*(ch + 1) == '<') { // Templates with extremely long bitwise math as parameters
                    ch++;
                }
                else {
                    tempStack++;
                }
                goto LABEL_TEMPBLOCK_START;

                case '>':
                tempStack--;
                if (tempStack == 0) {
                    return {TT_TemplateBlock, std::string_view(first, ++ch - first)};
                }
                goto LABEL_TEMPBLOCK_START;

                case '\r': case '\n': case '\0':
                throw Error("No end to template block");

                default:
                goto LABEL_TEMPBLOCK_START;
            }
        }
        break;

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
                if (hasDot)
                    throw Error("Decimal numbers can't have multiple periods.");
                hasDot = true;
                goto LABEL_NUMBER_START;

                case 'e': case 'E': case'+': case '-':
                goto LABEL_NUMBER_START;

                default:
                return {TT_Number, std::string_view(first, (size_t)(ch - first)) };
            }
        }

		default:
		{
		    if (!isLetter && *ch != '_' && *ch != ':') {
		    	throw Error("Unrecognized character");
		    }

            LABEL_FORCE_DEFAULT:
		    first = ch;

		    LABEL_ID_START:
            ++ch;
		    if (isLetter || isNum || *ch == '_' || *ch == ':')
		    	goto LABEL_ID_START;
            return {TT_Keyword, std::string_view(first, (size_t)(ch - first))};
		}
    }
}

void idlibCleaner::TokenAssert(TokenType type) {
	ParsedToken t = Tokenize();
	if (t.type != type) {
		throw Error("TokenAssert Failed");
	}
}

void idlibCleaner::BuildStruct(std::string_view hashHex) {
	TokenAssert(TT_Keyword); // __cppobj

	std::vector<ParsedToken> tokens;
	std::string_view fullName = "";
	std::string_view parentName = "";

	/* Read Struct Name */
	do {
		tokens.push_back(Tokenize());
	} while (tokens.back().type != TT_BraceOpen && tokens.back().type != TT_Colon);

	{
		const char* start = tokens[0].data.data();
		ParsedToken& endToken = tokens[tokens.size() - 2];
		const char* end = endToken.data.data() + endToken.data.size();
		fullName = std::string_view(start, end - start);
	}


    /* 
    * First analyze the name, and determine what string to write it to
    * Only 2 Possibilities: 1+ Keywords and 0-1 Template Blocks
    * Template block can occur: 
    *   - At the end (a true template instantation)
    *   - Prior to a subtype (instantiation is a supertype) (Super < >::Subtype)
    * Other than template supertypes, there will only be 2+ keywords for the C++ primitive types
    */
    
    enum StructType {
        ST_NORMAL = 0,
        ST_TEMPLATE = 1,
        ST_SUPERTEMP = 2
    };
    StructType stype = ST_NORMAL;
    for (int i = 0, max = (int)tokens.size() - 1; i < max; i++) { // Last token in the vector will be colon or brace
        TokenType ttype = tokens[i].type;

        if (ttype == TT_TemplateBlock) {
            assert(stype == ST_NORMAL);

            stype = (i == max - 1) ? ST_TEMPLATE : ST_SUPERTEMP;
        }
        assert(ttype = TT_Keyword);
    }


    std::string* writeto;

    if (stype == ST_NORMAL) {
        writeto = &cleanstructs;
    }
    else if (stype == ST_TEMPLATE) {
        std::string basetype = CleanName(tokens[0].data);
        auto pair = cleantemplates.find(basetype);
        if (pair == cleantemplates.end()) {
            auto newpair = cleantemplates.emplace(basetype, "");
            writeto = &newpair.first->second;

            writeto->reserve(50000);
            writeto->push_back('\t');
            writeto->append(basetype);
            writeto->append(" = {");

            // TODO: Analyze template parameters
        }
        else {
            writeto = &pair->second;
        }
    }
    else {
        writeto = &cleantemplatesubs;
    }

    /* Write Property Name Information */
    writeto->push_back('\t');
    writeto->append(CleanName(fullName));
    writeto->append(" = {\n\t\toriginalName = \"");
    writeto->append(fullName);
    writeto->append("\"\n");

    

	/*
    * Read struct parent type if it exists
    * Should follow the same rules as the struct name
    * If the parent is a template instantiation, it shouldn't be necessary to parse the individual types
    */
	if (tokens.back().type == TT_Colon) {
		tokens.clear();

		do {
			tokens.push_back(Tokenize());
		} while (tokens.back().type != TT_BraceOpen);

		{
			const char* start = tokens[0].data.data();
			ParsedToken& endToken = tokens[tokens.size() - 2];
			const char* end = endToken.data.data() + endToken.data.size();
			parentName = std::string_view(start, end - start);
		}
	}

    if (!parentName.empty()) {
        writeto->append("\t\tparentName = ");
        writeto->append(CleanName(parentName));
        writeto->push_back('\n');
    }

    if (!hashHex.empty()) {
        writeto->append("\t\thash = \"");
        writeto->append(hashHex);
        writeto->append("\"\n");
    }


    writeto->append("\t\tvalues = {\n");
    

    /* 
    * Read Struct Variables 
    * 
    * During this time we identify certain variables to exclude at the cleaning phase.
    * These would be too complicated to parse and we're assuming we don't need to serialize them.
    * These include:
    * - Function pointer variables
    * - Multi-dimensional static arrays
    * - Static arrays defined by more than an explicit integer (i.e. [ENUM_MAXIMUM] instead of [3];
    * 
    */
    std::string exclusions;
    while (true) {
        struct {
            bool excluding = false;
            bool INCLUDE = false; // Determined by parsing reflection flags
            int pointers = 0;
            int staticArrayIndex = -1;
            int typeEndIndex = -1;
            std::string_view name = "";
            std::string_view type = "";
        } val;

        tokens.clear();

        /* 
        * Parse comments preceding each variable declaration 
        */
        {
            ParsedToken comment = Tokenize();
            if(comment.type == TT_BraceClose)
                break;
            assert(comment.type == TT_Comment); // Offset + Size comment


            /* Parse the specifierFlags_t that indicate which variables are exposed to idStudio */
            comment = Tokenize();
            if (comment.type == TT_Comment) {

                val.INCLUDE = TokenizeFlags(comment.data);

                comment = Tokenize(); // Optional third developer comment
                if(comment.type != TT_Comment)
                    tokens.push_back(comment);
            }
            else tokens.push_back(comment);
        }

        /* Gather tokens */
        do {
            tokens.push_back(Tokenize());
        } while(tokens.back().type != TT_Semicolon);


        int i; /* Identify the name token, and static array if placed there*/
        for(i = (int)tokens.size() - 2; i > -1; i--) {
            ParsedToken t = tokens[i];
            
            if (tokens[i].type == TT_BracketBlock) {
                if(val.staticArrayIndex > -1)
                    val.excluding = true;
                val.staticArrayIndex = i;
            }
            else if (tokens[i].type == TT_Keyword) {
                val.name = tokens[i].data; // i will equal index of name token
                break;
            }
            else {
                assert(0); // This shouldn't be possible
            }
        }

        for (i = i-1; i > -1; i--) {
            switch (tokens[i].type)
            {
                case TT_Asterisk:
                val.pointers++;
                break;

                case TT_ParenthBlock:
                val.excluding = true;
                if(val.typeEndIndex < 0)
                    val.typeEndIndex = i;
                break;

                case TT_TemplateBlock:
                if(val.typeEndIndex < 0)
                    val.typeEndIndex = i;
                for (char c : tokens[i].data) {
                    if(c == '(' || c == ')')
                        val.excluding = true;
                }
                break;

                case TT_BracketBlock:
                if (val.staticArrayIndex > -1)
                    val.excluding = true;
                val.staticArrayIndex = i;
                break;

                case TT_Keyword:
                if(val.typeEndIndex < 0)
                    val.typeEndIndex = i;
                break;

                case TT_Number:
                printf("Line %zu - There is a variable name beginning with a number. Please edit the idlib to fix this.", 
                    currentLine);
                assert(0);
                break;

                default:
                assert(0);
                break;
            }
        }

        {
            // Set the type string
            const char* start = tokens[0].data.data();
            ParsedToken& endToken = tokens[val.typeEndIndex];
            const char* end = endToken.data.data() + endToken.data.size();
            val.type = std::string_view(start, end - start);

            // Analyze the static array token - exclude if there are non-numbers
            if (val.staticArrayIndex > -1) {
                std::string_view brack = tokens[val.staticArrayIndex].data;

                for (char c : brack.substr(1, brack.length() - 2)) {
                    if (c < '0' || c > '9') {
                        val.excluding = true;
                        break;
                    }
                }
            }
            
        }

        if(val.excluding) {
            if(val.INCLUDE)
                printf("Excluding value with include flags %.*s %.*s\n", (int)val.type.length(), val.type.data(), (int)val.name.length(), val.name.data());

            const char* start = tokens[0].data.data();
            ParsedToken& endToken = tokens[tokens.size() - 2];
            const char* end = endToken.data.data() + endToken.data.size();
            exclusions.append("\t\t\t\"");
            exclusions.append(std::string_view(start, end - start));
            exclusions.append("\"\n");
        }
        else {
            
            bool obj = val.pointers > 0 || val.staticArrayIndex > -1 || val.INCLUDE;

            writeto->append("\t\t\t");
            writeto->append(CleanName(val.type));
            writeto->push_back(' ');
            writeto->append(val.name);

            if(obj)
                writeto->append(" {");

            if (val.pointers > 0) {
                writeto->append("\n\t\t\t\tpointers = ");
                writeto->append(std::to_string(val.pointers));
            }

            if (val.staticArrayIndex > -1) {
                std::string_view brack = tokens[val.staticArrayIndex].data;

                writeto->append("\n\t\t\t\tarray = ");
                writeto->append(brack.substr(1, brack.length() - 2));
            }

            if(val.INCLUDE)
                writeto->append("\n\t\t\t\tINCLUDE");
            
            writeto->append(obj ? "\n\t\t\t}\n" : "\n");
        }

    }

    /*
    * END OF READ VARIABLE LOOP
    */
    TokenAssert(TT_Semicolon);
    writeto->append("\t\t}\n");

    if (exclusions.size() > 0) {
        writeto->append("\t\texclusions = {\n");
        writeto->append(exclusions);
        writeto->append("\t\t}\n");
    }

    writeto->append("\t}\n");


}

void idlibCleaner::BuildEnum(std::string_view hashHex) {
	ParsedToken name = Tokenize();
	TokenAssert(TT_Colon);
	TokenAssert(TT_Keyword);  // Parent Type - don't need this since enums are hashed
	TokenAssert(TT_BraceOpen);

	cleanenums.append("\t");
	cleanenums.append(CleanName(name.data));
	cleanenums.append(" {\n\t\toriginalName = \"");
	cleanenums.append(name.data);
    cleanenums.append("\"\n\t\thash = \"");
    cleanenums.append(hashHex);
	cleanenums.append("\"\n\t\tvalues = {\n");
	ParsedToken var = Tokenize();

	while (var.type != TT_BraceClose) {
		cleanenums.append("\t\t\t");
		cleanenums.append(var.data);
		cleanenums.push_back('\n');

		TokenAssert(TT_Equals);
		TokenAssert(TT_Number);
		TokenAssert(TT_Comma);
        TokenAssert(TT_Comment);
		var = Tokenize();
	}
	TokenAssert(TT_Semicolon);
	cleanenums.append("\t\t}\n\t}\n");
}

void idlibCleaner::Build() {

    ParsedToken comment = Tokenize();
	//ParsedToken keyword = Tokenize();
	while (comment.type == TT_Comment || comment.type == TT_Keyword) {
        std::string_view hashHex = "";

        // Some structs don't seem to have a hash
        if (comment.type == TT_Comment) {
            // Get the Hex Index
            size_t hashString = comment.data.find("Hash: ");

            // Enums might have a specifier flag comment before the hash comment
            if (hashString == std::string_view::npos) {
                comment = Tokenize();
                continue;
            }

            size_t hexIndex = comment.data.find("0x", hashString);
            assert(hexIndex != std::string_view::npos);
            hashHex = comment.data.substr(hexIndex, 10);

            comment = Tokenize();
        }

		if (comment.data == "enum") {
			BuildEnum(hashHex);
		}
		else if (comment.data == "struct") {
			BuildStruct(hashHex);
		}
		else { // idlib ends with a bunch of typedefs
			break;
		}

        comment = Tokenize();
	}
	printf("Cleaning Pass 1 Finished.\n");
}

void idlibCleaner::Write() {
    std::ofstream writer = std::ofstream("idlibcleaned_pass1.txt", std::ios_base::binary);

	cleanenums.append("}\n");
    writer.write(cleanenums.data(), cleanenums.length());

    writer << "templates {\n";
    for (auto& pair : cleantemplates) {
        pair.second.append("\t}\n");
        writer.write(pair.second.data(), pair.second.length());
    }
    writer << "}\n";
    
    cleantemplatesubs.append("}\n");
    writer.write(cleantemplatesubs.data(), cleantemplatesubs.length());
	
    cleanstructs.append("}\n");
    writer.write(cleanstructs.data(), cleanstructs.length());

	writer.close();
}

void idlibCleaning::Pass1()
{
	idlibCleaner cleaner = idlibCleaner();
	cleaner.Build();
	cleaner.Write();
	
}