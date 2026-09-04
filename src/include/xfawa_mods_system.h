#ifndef XFAWA_MODS_SYSTEM_H
#define XFAWA_MODS_SYSTEM_H

// ---------------------------------------------------------------------------
// Mods system: code-level disable.
//
// The mods system is disabled at the code level. The source is preserved and
// still builds into the compiler, but it provides no functionality:
//   - loadMod / loadModFromFile / loadModFromContent are no-ops,
//   - applyModifications / expandSyntax return the input unchanged,
//   - processMods() in main.cpp ignores (strips) mod imports.
// Undefine XFAWA_MODS_DISABLED to re-enable the system.
// ---------------------------------------------------------------------------
#define XFAWA_MODS_DISABLED 1

#include "xfawa_error.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <regex>

namespace xfawa {

extern int g_debug_global;

enum class ModTokenType {
    END_OF_FILE,
    
    IDENTIFIER,
    STRING_LITERAL,
    NUMBER_LITERAL,
    
    KEYWORD_FN,
    KEYWORD_IF,
    KEYWORD_ELSE,
    KEYWORD_WHILE,
    KEYWORD_BREAK,
    KEYWORD_RETURN,
    KEYWORD_TRUE,
    KEYWORD_FALSE,
    KEYWORD_PRINT,
    KEYWORD_IMPORT,
    
    PUNCTUATOR_LPAREN,
    PUNCTUATOR_RPAREN,
    PUNCTUATOR_LBRACE,
    PUNCTUATOR_RBRACE,
    PUNCTUATOR_LBRACKET,
    PUNCTUATOR_RBRACKET,
    PUNCTUATOR_SEMICOLON,
    PUNCTUATOR_COMMA,
    PUNCTUATOR_COLON,
    
    PUNCTUATOR_PLUS,
    PUNCTUATOR_MINUS,
    PUNCTUATOR_STAR,
    PUNCTUATOR_SLASH,
    PUNCTUATOR_PERCENT,
    
    PUNCTUATOR_EQUAL,
    PUNCTUATOR_EQUAL_EQUAL,
    PUNCTUATOR_EXCLAIM,
    PUNCTUATOR_EXCLAIM_EQUAL,
    PUNCTUATOR_LESS,
    PUNCTUATOR_LESS_EQUAL,
    PUNCTUATOR_GREATER,
    PUNCTUATOR_GREATER_EQUAL,
    PUNCTUATOR_LESS_LESS,
    PUNCTUATOR_GREATER_GREATER,
    
    PUNCTUATOR_AND,
    PUNCTUATOR_OR,
    
    PUNCTUATOR_HASH,
    PUNCTUATOR_DOLLAR,
    PUNCTUATOR_AT,
    
    PUNCTUATOR_ARROW,
    PUNCTUATOR_FAT_ARROW,
    PUNCTUATOR_DOT_DOT_DOT,
    
    KEYWORD_NAME,
    KEYWORD_SYNTAX,
    KEYWORD_LOGIC,
    KEYWORD_ACTION,
    
    COMMENT,
    WHITESPACE
};

struct ModToken {
    ModTokenType type;
    std::string text;
    int line;
    int column;
    
    ModToken(ModTokenType t = ModTokenType::END_OF_FILE, const std::string& txt = "", 
              int l = 1, int c = 1) 
        : type(t), text(txt), line(l), column(c) {}
    
    bool is(ModTokenType t) const { return type == t; }
    
    bool isOneOf(ModTokenType t1, ModTokenType t2) const { return type == t1 || type == t2; }
    
    template<typename... Args>
    bool isOneOf(ModTokenType t1, ModTokenType t2, Args... args) const {
        return type == t1 || isOneOf(t2, args...);
    }
    
    std::string toString() const;
};

struct SyntaxModification {
    std::string from;
    std::string to;
    int line;
    std::string modName;
    int loadOrder;
    
    SyntaxModification() : line(0), loadOrder(0) {}
    SyntaxModification(const std::string& f, const std::string& t, int l = 0, const std::string& mod = "", int order = 0)
        : from(f), to(t), line(l), modName(mod), loadOrder(order) {}
};

struct SyntaxParameter {
    std::string name;
    bool isAction;
    
    SyntaxParameter(const std::string& n, bool action = false) 
        : name(n), isAction(action) {}
};

struct AddedSyntax {
    std::string name;
    std::string syntaxPattern;
    std::string logicCode;
    std::vector<SyntaxParameter> parameters;
    std::vector<std::string> parameterOrder;
    int line;
    int maxSyntaxCount;
    std::string modName;
    int loadOrder;
    static const int MAX_SYNTAX_LIMIT = 256;
    
    AddedSyntax() : line(0), maxSyntaxCount(0), loadOrder(0) {}
    AddedSyntax(const std::string& n, const std::string& syntax, const std::string& logic, int l = 0)
        : name(n), syntaxPattern(syntax), logicCode(logic), line(l), maxSyntaxCount(0), loadOrder(0) {}
};

struct PublicFunction {
    std::string ns;
    std::string name;
    std::vector<std::string> params;
    std::string body;
    int line;
    std::string modName;
    
    PublicFunction() : line(0) {}
};

struct ModMetaInfo {
    std::string author;
    std::string version;
    std::string description;
    std::string license;
    int line;
    bool hasMeta;
    
    ModMetaInfo() : line(0), hasMeta(false) {}
    
    void setAuthor(const std::string& a) { author = a; hasMeta = true; }
    void setVersion(const std::string& v) { version = v; hasMeta = true; }
    void setDescription(const std::string& d) { description = d; hasMeta = true; }
    void setLicense(const std::string& l) { license = l; hasMeta = true; }
};

struct ModBlock {
    std::string name;
    std::vector<SyntaxModification> modifications;
    std::vector<AddedSyntax> addedSyntaxes;
    std::vector<PublicFunction> publicFunctions;
    std::unordered_set<std::string> internalFunctionNames;
    
    ModBlock(const std::string& n) : name(n) {}
};

struct ConflictInfo {
    std::string type;
    std::string name;
    std::string firstMod;
    int firstLoadOrder;
    std::string secondMod;
    int secondLoadOrder;
    int line;
    
    ConflictInfo(const std::string& t, const std::string& n, 
                 const std::string& fm, int fo, const std::string& sm, int so, int l)
        : type(t), name(n), firstMod(fm), firstLoadOrder(fo), 
          secondMod(sm), secondLoadOrder(so), line(l) {}
};

class ModLexer {
private:
    std::string source;
    size_t current_pos;
    int line;
    int column;
    std::vector<ModToken> tokens;
    std::vector<std::string> errors;
    
    static const std::vector<std::pair<std::string, ModTokenType>> keywords;
    static const std::vector<std::pair<std::string, ModTokenType>> punctuators;
    
public:
    explicit ModLexer(const std::string& source);
    
    std::vector<ModToken> tokenize();
    
    bool hasErrors() const { return !errors.empty(); }
    const std::vector<std::string>& getErrors() const { return errors; }
    
private:
    bool isAtEnd();
    char peek(int offset = 0);
    char advance();
    void skipWhiteSpace();
    void skipComment();
    void addToken(ModTokenType type, const std::string& text = "");
    void addError(const std::string& message);
    void lexIdentifier();
    void lexNumber();
    void lexString();
    void lexPunctuator();
    
    static bool isAlpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    
    static bool isDigit(char c) {
        return c >= '0' && c <= '9';
    }
    
    static bool isAlphaNumeric(char c) {
        return isAlpha(c) || isDigit(c);
    }
};

class ModParser {
private:
    std::vector<ModToken> tokens;
    size_t current;
    std::vector<std::string> errors;
    std::vector<ModBlock> blocks;
    ModMetaInfo metaInfo;
    
public:
    explicit ModParser(const std::vector<ModToken>& toks);
    
    bool parse();
    const std::vector<ModBlock>& getBlocks() const { return blocks; }
    const ModMetaInfo& getMetaInfo() const { return metaInfo; }
    
    bool hasErrors() const { return !errors.empty(); }
    const std::vector<std::string>& getErrors() const { return errors; }
    
private:
    const ModToken& peek() const;
    const ModToken& peek(int offset) const;
    ModToken consume();
    bool consume(ModTokenType type);
    bool isAtEnd() const;
    void advance();
    void addError(const std::string& message);
    
    bool parseBlock();
    bool parseMetaBlock();
    bool parseBlockContent(ModBlock& block);
    bool parseFunction(ModBlock& block);
    bool parseNestedPublicFunction(PublicFunction& pubFunc);
    bool parseNestedInternalFunction(PublicFunction& internalFunc);
    bool parseAddCommand(ModBlock& block);
    bool parseModification(ModBlock& block);
    
    AddedSyntax parseAddSyntaxContent(const std::string& blockContent, int startLine);
    std::string extractBlockContent();
    std::vector<SyntaxParameter> extractParameters(const std::string& syntaxPattern);
};

class ModsSystem {
private:
    std::vector<ModBlock> loadedBlocks;
    std::vector<SyntaxModification> allModifications;
    std::vector<AddedSyntax> allAddedSyntaxes;
    std::vector<PublicFunction> allPublicFunctions;
    std::unordered_set<std::string> allInternalFunctionNames;
    std::vector<std::string> errors;
    std::vector<ConflictInfo> conflicts;
    std::unordered_map<std::string, AddedSyntax> syntaxRegistry;
    std::unordered_map<std::string, SyntaxModification> modificationRegistry;
    std::unordered_set<std::string> loadedModNames;
    int totalAddedSyntaxCount;
    int currentLoadOrder;
    std::string currentModName;
    ModMetaInfo globalMeta;
    
public:
    ModsSystem() : totalAddedSyntaxCount(0), currentLoadOrder(0) {}
    
    bool loadMod(const std::string& modName);
    bool loadModFromFile(const std::string& filePath);
    bool loadModFromContent(const std::string& content);
    
    std::string applyModifications(const std::string& source);
    std::string expandSyntax(const std::string& source);
    
    bool hasModifications() const { return !allModifications.empty(); }
    bool hasAddedSyntaxes() const { return !allAddedSyntaxes.empty(); }
    bool hasPublicFunctions() const { return !allPublicFunctions.empty(); }
    bool hasConflicts() const { return !conflicts.empty(); }
    bool hasMetaInfo() const { return globalMeta.hasMeta; }
    
    const std::vector<SyntaxModification>& getModifications() const { return allModifications; }
    const std::vector<AddedSyntax>& getAddedSyntaxes() const { return allAddedSyntaxes; }
    const std::vector<PublicFunction>& getPublicFunctions() const { return allPublicFunctions; }
    const std::unordered_set<std::string>& getInternalFunctionNames() const { return allInternalFunctionNames; }
    const std::vector<ConflictInfo>& getConflicts() const { return conflicts; }
    const ModMetaInfo& getMetaInfo() const { return globalMeta; }
    
    bool hasErrors() const { return !errors.empty(); }
    const std::vector<std::string>& getErrors() const { return errors; }
    
    void clear();
    
private:
    void collectModifications();
    void registerSyntaxes();
    bool checkForConflicts(size_t startBlockIndex = 0);
    bool checkModificationConflict(const SyntaxModification& mod);
    bool checkSyntaxConflict(const AddedSyntax& syntax);
    std::string expandSingleSyntax(const AddedSyntax& syntax, const std::string& matchedContent,
                                    const std::unordered_map<std::string, std::string>& args);
    std::vector<std::string> extractArguments(const std::string& syntaxPattern, 
                                               const std::string& matchedContent);
    std::unordered_map<std::string, std::string> parseSyntaxUsage(const AddedSyntax& syntax, 
                                                                   const std::string& usage);
    bool validateSyntaxLimit();
    
    static bool isAlpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    
    static bool isDigit(char c) {
        return c >= '0' && c <= '9';
    }
};

}

#endif
