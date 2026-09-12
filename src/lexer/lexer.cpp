#include "xfawa_lexer.h"
#include <vector>
#include <cctype>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <iostream>

namespace xfawa {

const std::vector<std::pair<std::string, TokenType>> Lexer::keywords = {
    {"fn", TokenType::KEYWORD_FN},
    {"if", TokenType::KEYWORD_IF},
    {"else", TokenType::KEYWORD_ELSE},
    {"while", TokenType::KEYWORD_WHILE},
    {"break", TokenType::KEYWORD_BREAK},
    {"return", TokenType::KEYWORD_RETURN},
    {"true", TokenType::KEYWORD_TRUE},
    {"false", TokenType::KEYWORD_FALSE},
    {"print", TokenType::KEYWORD_PRINT},
    {"import", TokenType::KEYWORD_IMPORT},
    {"%import", TokenType::KEYWORD_PERCENT_IMPORT},
    {"int", TokenType::KEYWORD_INT},
    {"long", TokenType::KEYWORD_LONG},
    {"float", TokenType::KEYWORD_FLOAT},
    {"bool", TokenType::KEYWORD_BOOL},
    {"string", TokenType::KEYWORD_STRING},
    {"for", TokenType::KEYWORD_FOR},
    {"window", TokenType::KEYWORD_WINDOW},
    {"input", TokenType::KEYWORD_INPUT},
    {"and", TokenType::KEYWORD_AND},
    {"or", TokenType::KEYWORD_OR},
    {"class", TokenType::KEYWORD_CLASS},
    {"loop", TokenType::KEYWORD_LOOP},
    {"boom", TokenType::KEYWORD_BOOM},
    {"bsod", TokenType::KEYWORD_BSOD},
    {"believe", TokenType::KEYWORD_BELIEVE},
    {"lie", TokenType::KEYWORD_LIE},
    {"un", TokenType::KEYWORD_UN},
    {"ignore", TokenType::KEYWORD_IGNORE},
    {"do", TokenType::KEYWORD_DO},
    {"please", TokenType::KEYWORD_PLEASE},
    {"shutup", TokenType::KEYWORD_SHUTUP},
    {"wrath", TokenType::KEYWORD_WRATH},
    {"paradox", TokenType::KEYWORD_PARADOX},
    {"try", TokenType::KEYWORD_TRY},
    {"expect", TokenType::KEYWORD_EXPECT},
    {"sorry", TokenType::KEYWORD_SORRY},
    {"sleep", TokenType::KEYWORD_SLEEP},
    {"come", TokenType::KEYWORD_COME}
};

const std::vector<std::pair<std::string, TokenType>> Lexer::punctuatuators = {
    {"(", TokenType::PUNCTUATOR_LPAREN},
    {")", TokenType::PUNCTUATOR_RPAREN},
    {"{", TokenType::PUNCTUATOR_LBRACE},
    {"}", TokenType::PUNCTUATOR_RBRACE},
    {"[", TokenType::PUNCTUATOR_LBRACKET},
    {"]", TokenType::PUNCTUATOR_RBRACKET},
    {";", TokenType::PUNCTUATOR_SEMICOLON},
    {",", TokenType::PUNCTUATOR_COMMA},
    {":", TokenType::PUNCTUATOR_COLON},
    {"+", TokenType::PUNCTUATOR_PLUS},
    {"-", TokenType::PUNCTUATOR_MINUS},
    {"*", TokenType::PUNCTUATOR_STAR},
    {"/", TokenType::PUNCTUATOR_SLASH},
    {"%", TokenType::PUNCTUATOR_PERCENT},
    {"=", TokenType::PUNCTUATOR_EQUAL},
    {"==", TokenType::PUNCTUATOR_EQUAL_EQUAL},
    {"!", TokenType::PUNCTUATOR_EXCLAIM},
    {"!=", TokenType::PUNCTUATOR_EXCLAIM_EQUAL},
    {"<", TokenType::PUNCTUATOR_LESS},
    {"<=", TokenType::PUNCTUATOR_LESS_EQUAL},
    {">", TokenType::PUNCTUATOR_GREATER},
    {">=", TokenType::PUNCTUATOR_GREATER_EQUAL},
    {"&&", TokenType::PUNCTUATOR_AND},
    {"||", TokenType::PUNCTUATOR_OR},
    {"#", TokenType::PUNCTUATOR_HASH},
    {"$", TokenType::PUNCTUATOR_DOLLAR},
    {".", TokenType::PUNCTUATOR_DOT},
    {"...", TokenType::PUNCTUATOR_DOT_DOT_DOT}
};

std::string Lexer::tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::IDENTIFIER: return "identifier";
        case TokenType::NUMBER_LITERAL: return "number";
        case TokenType::LONG_LITERAL: return "long";
        case TokenType::STRING_LITERAL: return "string";
        case TokenType::FLOAT_LITERAL: return "float";
        case TokenType::COLOR_LITERAL: return "color";
        case TokenType::O_LITERAL: return "o-literal";
        case TokenType::KEYWORD_FN: return "fn";
        case TokenType::KEYWORD_IF: return "if";
        case TokenType::KEYWORD_ELSE: return "else";
        case TokenType::KEYWORD_WHILE: return "while";
        case TokenType::KEYWORD_BREAK: return "break";
        case TokenType::KEYWORD_RETURN: return "return";
        case TokenType::KEYWORD_TRUE: return "true";
        case TokenType::KEYWORD_FALSE: return "false";
        case TokenType::KEYWORD_PRINT: return "print";
        case TokenType::KEYWORD_IMPORT: return "import";
        case TokenType::KEYWORD_INT: return "int";
        case TokenType::KEYWORD_LONG: return "long";
        case TokenType::KEYWORD_FLOAT: return "float";
        case TokenType::KEYWORD_BOOL: return "bool";
        case TokenType::KEYWORD_STRING: return "string";
        case TokenType::KEYWORD_FOR: return "for";
        case TokenType::KEYWORD_WINDOW: return "window";
        case TokenType::KEYWORD_INPUT: return "input";
        case TokenType::KEYWORD_AND: return "and";
        case TokenType::KEYWORD_OR: return "or";
        case TokenType::KEYWORD_CLASS: return "class";
        case TokenType::KEYWORD_LOOP: return "loop";
        case TokenType::KEYWORD_BOOM: return "boom";
        case TokenType::KEYWORD_BSOD: return "bsod";
        case TokenType::KEYWORD_BELIEVE: return "believe";
        case TokenType::KEYWORD_LIE: return "lie";
        case TokenType::KEYWORD_UN: return "un";
        case TokenType::KEYWORD_IGNORE: return "ignore";
        case TokenType::KEYWORD_DO: return "do";
        case TokenType::KEYWORD_PLEASE: return "please";
        case TokenType::KEYWORD_SHUTUP: return "shutup";
        case TokenType::KEYWORD_WRATH: return "wrath";
        case TokenType::KEYWORD_PARADOX: return "paradox";
        case TokenType::KEYWORD_TRY: return "try";
        case TokenType::KEYWORD_EXPECT: return "expect";
        case TokenType::KEYWORD_SORRY: return "sorry";
        case TokenType::KEYWORD_SLEEP: return "sleep";
        case TokenType::KEYWORD_COME: return "come";
        case TokenType::PUNCTUATOR_LPAREN: return "(";
        case TokenType::PUNCTUATOR_RPAREN: return ")";
        case TokenType::PUNCTUATOR_LBRACE: return "{";
        case TokenType::PUNCTUATOR_RBRACE: return "}";
        case TokenType::PUNCTUATOR_LBRACKET: return "[";
        case TokenType::PUNCTUATOR_RBRACKET: return "]";
        case TokenType::PUNCTUATOR_SEMICOLON: return ";";
        case TokenType::PUNCTUATOR_COMMA: return ",";
        case TokenType::PUNCTUATOR_PLUS: return "+";
        case TokenType::PUNCTUATOR_MINUS: return "-";
        case TokenType::PUNCTUATOR_STAR: return "*";
        case TokenType::PUNCTUATOR_SLASH: return "/";
        case TokenType::PUNCTUATOR_PERCENT: return "%";
        case TokenType::PUNCTUATOR_EQUAL: return "=";
        case TokenType::PUNCTUATOR_EQUAL_EQUAL: return "==";
        case TokenType::PUNCTUATOR_EXCLAIM: return "!";
        case TokenType::PUNCTUATOR_EXCLAIM_EQUAL: return "!=";
        case TokenType::PUNCTUATOR_LESS: return "<";
        case TokenType::PUNCTUATOR_LESS_EQUAL: return "<=";
        case TokenType::PUNCTUATOR_GREATER: return ">";
        case TokenType::PUNCTUATOR_GREATER_EQUAL: return ">=";
        case TokenType::PUNCTUATOR_AND: return "&&";
        case TokenType::PUNCTUATOR_OR: return "||";
        case TokenType::PUNCTUATOR_HASH: return "#";
        case TokenType::PUNCTUATOR_DOLLAR: return "$";
        case TokenType::PUNCTUATOR_COLON: return ":";
        case TokenType::PUNCTUATOR_DOT: return ".";
        case TokenType::PUNCTUATOR_DOT_DOT_DOT: return "...";
        default: return "unknown";
    }
}

Lexer::Lexer(const std::string& source) 
    : source(source), current_pos(0), line(1), column(1) {}

bool Lexer::isAtEnd() {
    return current_pos >= source.length();
}

char Lexer::peek(int offset) {
    size_t pos = current_pos + offset;
    if (pos >= source.length()) return '\0';
    return source[pos];
}

char Lexer::advance() {
    char c = source[current_pos];
    current_pos++;
    column++;
    if (c == '\n') {
        line++;
        column = 1;
    }
    return c;
}

void Lexer::skipWhiteSpace() {
    while (!isAtEnd()) {
        char c = peek();
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            advance();
        } else if (c == '\\' && peek(1) == '\n') {
            advance();
            advance();
        } else {
            return;
        }
    }
}

void Lexer::skipComment() {
    if (peek() == '/' && peek(1) == '/') {
        // Single-line comment. Record the comment text (without "//").
        int commentLine = line;
        int commentColumn = column + 2;
        advance(); // '/'
        advance(); // '/'
        std::string text;
        while (!isAtEnd() && peek() != '\n') {
            text += advance();
        }
        comments.emplace_back(CommentInfo{text, commentLine, commentColumn});
    } else if (peek() == '/' && peek(1) == '*') {
        // Multi-line comment. Record the text between /* and */.
        int commentLine = line;
        int commentColumn = column + 2;
        advance(); // '/'
        advance(); // '*'
        std::string text;
        while (!isAtEnd()) {
            if (peek() == '*' && peek(1) == '/') {
                comments.emplace_back(CommentInfo{text, commentLine, commentColumn});
                advance(); // '*'
                advance(); // '/'
                break;
            } else {
                text += advance();
            }
        }
    }
}

void Lexer::addToken(TokenType type, const std::string& text) {
        tokens.push_back(Token(type, text, SourceLocation(line, column, text.length())));
    }

void Lexer::addError(const std::string& message) {
    errors.push_back(message + " at line " + std::to_string(line) + ", column " + std::to_string(column));
}

void Lexer::lexIdentifier() {
    int start_column = column;
    std::string text;
    
    // A pure run of 'o' (o, oo, ooo, ...) stays an IDENTIFIER at the lexer level.
    // The parser decides whether it is an o-literal or a variable reference: an
    // undeclared identifier made only of 'o' characters becomes an o-literal,
    // while a declared variable named `o` keeps referring to that variable.
    // This keeps `int o = 20` working and lets the o-literal coexist with it.
    while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
        text += advance();
    }
    
    auto it = std::find_if(keywords.begin(), keywords.end(), 
        [&text](const std::pair<std::string, TokenType>& pair) { 
            return pair.first == text; 
        });
    
    if (it != keywords.end()) {
        addToken(it->second, text);
    } else {
        addToken(TokenType::IDENTIFIER, text);
    }
}

void Lexer::lexNumber() {
    int start_column = column;
    std::string text;
    bool isFloat = false;
    
    while (!isAtEnd() && std::isdigit(peek())) {
        text += advance();
    }
    
    if (!isAtEnd() && peek() == '.' && std::isdigit(peek(1))) {
        isFloat = true;
        text += advance();
        while (!isAtEnd() && std::isdigit(peek())) {
            text += advance();
        }
    }
    
    // EXP o-literal: digits followed by one or more 'o' characters, e.g. `1o`,
    // `15o`, `10o`. These form a single O_LITERAL token (digit prefix + o run).
    if (!isFloat && !isAtEnd() && peek() == 'o') {
        while (!isAtEnd() && peek() == 'o') {
            text += advance();
        }
        addToken(TokenType::O_LITERAL, text);
        return;
    }
    
    if (isFloat) {
        addToken(TokenType::FLOAT_LITERAL, text);
    } else {
        try {
            unsigned long long value = std::stoull(text);
            constexpr unsigned long long INT32_MAX_VAL = 2147483647ULL;
            constexpr unsigned long long INT64_MAX_VAL = 9223372036854775807ULL;
            
            if (value > INT64_MAX_VAL) {
                addError("Integer literal '" + text + "' out of range (valid range: 0 to 9223372036854775807 for positive)");
                addToken(TokenType::LONG_LITERAL, text);
            } else if (value > INT32_MAX_VAL) {
                addToken(TokenType::LONG_LITERAL, text);
            } else {
                addToken(TokenType::NUMBER_LITERAL, text);
            }
        } catch (const std::out_of_range&) {
            addError("Integer literal '" + text + "' out of range (valid range: 0 to 9223372036854775807 for positive)");
            addToken(TokenType::LONG_LITERAL, text);
        }
    }
}

void Lexer::lexString() {
    advance();  // opening quote
    std::string text;
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\\') {
            advance();
            if (!isAtEnd()) {
                char escaped = advance();
                switch (escaped) {
                    case 'n': text += '\n'; break;
                    case 't': text += '\t'; break;
                    case 'r': text += '\r'; break;
                    case '\\': text += '\\'; break;
                    case '"': text += '"'; break;
                    case '0': text += '\0'; break;
                    default: text += escaped; break;
                }
            }
        } else {
            text += advance();
        }
    }
    if (!isAtEnd()) {
        advance();
    }
    addToken(TokenType::STRING_LITERAL, text);
}

void Lexer::lexColor() {
    advance();  // consume '#'
    std::string text;
    while (!isAtEnd() && isHexDigit(peek())) {
        text += advance();
    }
    // Accept 3-digit (#FFF) or 6-digit (#FFFFFF) hex colors
    if (text.size() == 3 || text.size() == 6) {
        addToken(TokenType::COLOR_LITERAL, text);
    } else {
        addError("Invalid color literal: #" + text + " (expected 3 or 6 hex digits)");
        addToken(TokenType::COLOR_LITERAL, text);
    }
}

void Lexer::lexPunctuatuator() {
    int start_column = column;
    std::string text;
    
    for (int len = 3; len >= 1; len--) {
        text.clear();
        for (int i = 0; i < len && !isAtEnd(); i++) {
            text += peek(i);
        }
        
        auto it = std::find_if(punctuatuators.begin(), punctuatuators.end(), 
            [&text](const std::pair<std::string, TokenType>& pair) { 
                return pair.first == text; 
            });
        
        if (it != punctuatuators.end()) {
            for (int i = 0; i < len; i++) {
                advance();
            }
            addToken(it->second, text);
            return;
        }
    }
    
    text = advance();
    addError("Unexpected punctuator: " + text);
}

std::vector<Token> Lexer::tokenize() {
    tokens.clear();
    errors.clear();
    current_pos = 0;
    line = 1;
    column = 1;
    
    while (!isAtEnd()) {
        skipWhiteSpace();
        if (isAtEnd()) break;
        
        char c = peek();
        
        if (c == '/' && (peek(1) == '/' || peek(1) == '*')) {
            skipComment();
            continue;
        }
        
        if (c == '%' &&
            peek(1) == 'i' &&
            peek(2) == 'm' &&
            peek(3) == 'p' &&
            peek(4) == 'o' &&
            peek(5) == 'r' &&
            peek(6) == 't' &&
            !isAlphaNumeric(peek(7)) &&
            peek(7) != '_') {
            advance();
            std::string text;
            while (!isAtEnd() && (isAlphaNumeric(peek()) || peek() == '_')) {
                text += advance();
            }
            addToken(TokenType::KEYWORD_IMPORT, text);
            continue;
        }
        
        if (isAlpha(c)) {
            lexIdentifier();
        } else if (isDigit(c)) {
            lexNumber();
        } else if (c == '"') {
            lexString();
        } else if (c == '#') {
            // Disambiguate '#' colors from '#' module names.
            // A color literal is '#hex' where the hex run is exactly 3 or 6
            // digits AND is not part of a longer name (e.g. '#factory' is a
            // module, not a color, because 'fact' is not a hex color).
            int hexLen = 0;
            while (isHexDigit(peek(hexLen + 1))) {
                hexLen++;
            }
            bool isColor = (hexLen == 3 || hexLen == 6) && !isAlphaNumeric(peek(hexLen + 1));
            if (isColor) {
                lexColor();
            } else {
                lexPunctuatuator();
            }
        } else {
            lexPunctuatuator();
        }
    }
    
    SourceLocation loc(line, column, 0);
    tokens.push_back(Token(TokenType::END_OF_FILE, "", loc));
    
    return tokens;
}

}
