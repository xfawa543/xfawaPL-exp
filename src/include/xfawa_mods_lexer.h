#ifndef XFAWA_MODS_LEXER_H
#define XFAWA_MODS_LEXER_H

#include <string>
#include <vector>

namespace xfawa {

enum class ModsTokenType {
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
    
    PUNCTUATOR_AND,
    PUNCTUATOR_OR,
    
    PUNCTUATOR_HASH,
    PUNCTUATOR_DOLLAR,
    
    COMMENT,
    WHITESPACE
};

struct ModsToken {
    ModsTokenType type;
    std::string text;
    int line;
    int column;
    
    ModsToken(ModsTokenType t = ModsTokenType::END_OF_FILE, const std::string& txt = "", 
              int l = 1, int c = 1) 
        : type(t), text(txt), line(l), column(c) {}
    
    bool is(ModsTokenType t) const { return type == t; }
    
    bool isOneOf(ModsTokenType t1, ModsTokenType t2) const { return type == t1 || type == t2; }
    
    template<typename... Args>
    bool isOneOf(ModsTokenType t1, ModsTokenType t2, Args... args) const {
        return type == t1 || isOneOf(t2, args...);
    }
    
    std::string toString() const;
};

class ModsLexer {
private:
    std::string source;
    size_t current_pos;
    int line;
    int column;
    std::vector<ModsToken> tokens;
    std::vector<std::string> errors;
    
public:
    static const std::vector<std::pair<std::string, ModsTokenType>> keywords;
    static const std::vector<std::pair<std::string, ModsTokenType>> punctuators;
    
    explicit ModsLexer(const std::string& source);
    
    std::vector<ModsToken> tokenize();
    
    bool hasErrors() const { return !errors.empty(); }
    const std::vector<std::string>& getErrors() const { return errors; }
    
private:
    bool isAtEnd();
    char peek(int offset = 0);
    char advance();
    void skipWhiteSpace();
    void skipComment();
    void addToken(ModsTokenType type, const std::string& text = "");
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

}

#endif