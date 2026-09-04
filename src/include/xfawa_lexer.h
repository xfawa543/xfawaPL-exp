#ifndef XFAWA_LEXER_H
#define XFAWA_LEXER_H

#include "xfawa_types.h"
#include <string>
#include <vector>

namespace xfawa {

class Lexer {
private:
    std::string source;
    size_t current_pos;
    int line;
    int column;
    std::vector<Token> tokens;
    std::vector<std::string> errors;
    // Comments are collected separately from the token stream so they can be
    // observed by later passes without polluting normal parsing (EXP-001).
    std::vector<CommentInfo> comments;
    
public:
    static const std::vector<std::pair<std::string, TokenType>> keywords;
    static const std::vector<std::pair<std::string, TokenType>> punctuatuators;
    
    explicit Lexer(const std::string& source);
    
    std::vector<Token> tokenize();
    
    bool hasErrors() const { return !errors.empty(); }
    const std::vector<std::string>& getErrors() const { return errors; }
    const std::vector<CommentInfo>& getComments() const { return comments; }
    
private:
    bool isAtEnd();
    char peek(int offset = 0);
    char advance();
    void skipWhiteSpace();
    void skipComment();
    void addToken(TokenType type, const std::string& text = "");
    void addError(const std::string& message);
    void lexIdentifier();
    void lexNumber();
    void lexString();
    void lexColor();
    void lexPunctuatuator();
    
    static std::string tokenTypeToString(TokenType type);
    
    static bool isAlpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    
    static bool isDigit(char c) {
        return c >= '0' && c <= '9';
    }
    
    static bool isAlphaNumeric(char c) {
        return isAlpha(c) || isDigit(c);
    }
    
    static bool isHexDigit(char c) {
        return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
};

}

#endif
