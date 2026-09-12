#include "xfawa_parser.h"
#include "xfawa_error.h"
#include <sstream>
#include <algorithm>
#include <iostream>

namespace xfawa {

namespace {

bool isKeywordLikeNameToken(TokenType type) {
    switch (type) {
        case TokenType::IDENTIFIER:
        case TokenType::KEYWORD_FN:
        case TokenType::KEYWORD_IF:
        case TokenType::KEYWORD_ELSE:
        case TokenType::KEYWORD_WHILE:
        case TokenType::KEYWORD_BREAK:
        case TokenType::KEYWORD_RETURN:
        case TokenType::KEYWORD_TRUE:
        case TokenType::KEYWORD_FALSE:
        case TokenType::KEYWORD_PRINT:
        case TokenType::KEYWORD_IMPORT:
        case TokenType::KEYWORD_PERCENT_IMPORT:
        case TokenType::KEYWORD_INT:
        case TokenType::KEYWORD_LONG:
        case TokenType::KEYWORD_FLOAT:
        case TokenType::KEYWORD_BOOL:
        case TokenType::KEYWORD_STRING:
        case TokenType::KEYWORD_FOR:
        case TokenType::KEYWORD_WINDOW:
            return true;
        default:
            return false;
    }
}

// --- EXP auto-fix: map a misspelled keyword-like identifier back to a keyword ---

static const std::vector<std::pair<std::string, TokenType>>& autoFixCandidates() {
    static const std::vector<std::pair<std::string, TokenType>> map = {
        {"fn",     TokenType::KEYWORD_FN},
        {"if",     TokenType::KEYWORD_IF},
        {"else",   TokenType::KEYWORD_ELSE},
        {"while",  TokenType::KEYWORD_WHILE},
        {"break",  TokenType::KEYWORD_BREAK},
        {"return", TokenType::KEYWORD_RETURN},
        {"print",  TokenType::KEYWORD_PRINT},
        {"int",    TokenType::KEYWORD_INT},
        {"long",   TokenType::KEYWORD_LONG},
        {"float",  TokenType::KEYWORD_FLOAT},
        {"bool",   TokenType::KEYWORD_BOOL},
        {"string", TokenType::KEYWORD_STRING},
        {"for",    TokenType::KEYWORD_FOR},
        {"window", TokenType::KEYWORD_WINDOW},
        {"input",  TokenType::KEYWORD_INPUT},
        {"and",    TokenType::KEYWORD_AND},
        {"or",     TokenType::KEYWORD_OR},
        {"class",  TokenType::KEYWORD_CLASS},
        {"loop",   TokenType::KEYWORD_LOOP},
        {"boom",   TokenType::KEYWORD_BOOM},
        {"bsod",   TokenType::KEYWORD_BSOD},
        {"try",    TokenType::KEYWORD_TRY},
        {"expect", TokenType::KEYWORD_EXPECT},
        {"sorry",  TokenType::KEYWORD_SORRY},
    };
    return map;
}

// Returns the closest keyword if `word` is a plausible typo of it, else "".
static std::string nearestKeywordName(const std::string& word) {
    std::string best;
    int bestDist = 2; // only correct a word that is at most 2 edits away and similar length
    int wl = static_cast<int>(word.size());
    for (const auto& p : autoFixCandidates()) {
        int kl = static_cast<int>(p.first.size());
        int lenDiff = kl > wl ? kl - wl : wl - kl;
        if (lenDiff > 2) continue;
        int d = levenshteinDistance(word, p.first);
        if (d < bestDist) {
            bestDist = d;
            best = p.first;
        }
    }
    return best;
}

static TokenType keywordTokenType(const std::string& name) {
    for (const auto& p : autoFixCandidates()) {
        if (p.first == name) return p.second;
    }
    return TokenType::IDENTIFIER;
}

}

Parser::Parser(const std::vector<Token>& toks) : tokens(toks), current(0) {}

const Token& Parser::peek() const {
    if (current < tokens.size()) {
        return tokens[current];
    }
    return tokens.back();
}

const Token& Parser::peek(int offset) const {
    size_t pos = current + offset;
    if (pos < tokens.size()) {
        return tokens[pos];
    }
    return tokens.back();
}

Token Parser::consume() {
    if (current < tokens.size()) {
        return tokens[current++];
    }
    return tokens.back();
}

bool Parser::consume(TokenType type) {
    if (peek().is(type)) {
        current++;
        return true;
    }
    return false;
}

bool Parser::isAtEnd() const {
    return peek().is(TokenType::END_OF_FILE);
}

void Parser::advance() {
    if (current < tokens.size()) {
        current++;
    }
}

void Parser::addError(const std::string& message) {
    errors.push_back(message + " at line " + std::to_string(peek().location.line));
}

void Parser::addWarning(const std::string& message) {
    warnings.push_back(message + " at line " + std::to_string(peek().location.line));
}

bool Parser::isVariableDeclared(const std::string& name) const {
    return declaredVariables.find(name) != declaredVariables.end();
}

void Parser::declareVariable(const std::string& name) {
    declaredVariables.insert(name);
}

std::unique_ptr<Program> Parser::parseProgram() {
    auto program = std::make_unique<Program>();
    
    while (!isAtEnd() && !peek().is(TokenType::END_OF_FILE)) {
        if (peek().is(TokenType::KEYWORD_IMPORT) || peek().is(TokenType::KEYWORD_PERCENT_IMPORT)) {
            auto imp = parseImportStatement();
            if (imp) {
                program->addImport(std::move(imp));
            } else {
                break;
            }
        } else if (peek().is(TokenType::PUNCTUATOR_HASH) || peek().is(TokenType::COLOR_LITERAL)) {
            auto mod = parseModule();
            if (mod) {
                program->addModule(std::move(mod));
            } else {
                break;
            }
        } else {
            addError("Expected import or module definition");
            break;
        }
    }
    
    return program;
}

std::unique_ptr<Module> Parser::parseModule() {
    SourceLocation loc = peek().location;
    
    std::string name;
    if (peek().is(TokenType::COLOR_LITERAL)) {
        // '#' + hex was merged into a color token by the lexer
        // (module names that are exactly 3 or 6 hex digits, e.g. '#fac').
        name = peek().text;
        advance();
    } else {
        if (!consume(TokenType::PUNCTUATOR_HASH)) {
            return nullptr;
        }
        if (!isKeywordLikeNameToken(peek().type)) {
            addError("Expected module name after '#'");
            return nullptr;
        }
        advance();
        name = peek(-1).text;
    }
    
    if (!consume(TokenType::PUNCTUATOR_LBRACE)) {
        addError("Expected '{' after module name");
        return nullptr;
    }
    
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::unique_ptr<ImportStatement>> imports;
    // Note: class declarations are NOT allowed at module level in .xf files.
    // They can only be defined inside `window` blocks (per yfsj spec).
    // Only .xfw library files can define class declarations at top level.
    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        // Support import statements inside module
        if (peek().is(TokenType::KEYWORD_IMPORT) || peek().is(TokenType::KEYWORD_PERCENT_IMPORT)) {
            auto imp = parseImportStatement();
            if (imp) {
                imports.push_back(std::move(imp));
            } else {
                addError("Failed to parse import in module '" + name + "'");
                return nullptr;
            }
        } else if (peek().is(TokenType::KEYWORD_CLASS)) {
            // class declarations are not allowed at module level in .xf files
            addError("class declarations must be inside 'window' blocks in .xf files (only .xfw libraries allow top-level class)");
            return nullptr;
        } else {
            auto func = parseFunction();
            if (func) {
                // Alpha17: Set block name for the function
                if (func->ns.empty()) {
                    func->blockName = name;
                }
                functions.push_back(std::move(func));
            } else {
                addError("Failed to parse function in module '" + name + "' at token: " + peek().toString());
                return nullptr;
            }
        }
    }

    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close module");
        return nullptr;
    }

    auto module = std::make_unique<Module>(name, std::move(functions), loc);
    module->imports = std::move(imports);
    return module;
}

std::unique_ptr<WindowStatement> Parser::parseWindowStatement() {
    SourceLocation loc = peek().location;

    if (!consume(TokenType::KEYWORD_WINDOW)) {
        return nullptr;
    }

    if (!consume(TokenType::PUNCTUATOR_LBRACE)) {
        addError("Expected '{' after 'window'");
        return nullptr;
    }

    auto windowDecl = std::make_unique<WindowStatement>(loc);

    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        if (peek().is(TokenType::IDENTIFIER) && peek().text == "button" && peek(1).is(TokenType::PUNCTUATOR_LBRACE)) {
            auto button = parseButtonStatement();
            if (!button) {
                return nullptr;
            }
            windowDecl->buttons.push_back(std::move(button));
            continue;
        }
        if (peek().is(TokenType::IDENTIFIER) && peek().text == "text" && peek(1).is(TokenType::PUNCTUATOR_LBRACE)) {
            auto textItem = parseTextStatement();
            if (!textItem) {
                return nullptr;
            }
            windowDecl->texts.push_back(std::move(textItem));
            continue;
        }
        if (peek().is(TokenType::IDENTIFIER) && peek().text == "box" && peek(1).is(TokenType::PUNCTUATOR_LBRACE)) {
            auto boxItem = parseBoxStatement();
            if (!boxItem) {
                return nullptr;
            }
            windowDecl->boxes.push_back(std::move(boxItem));
            continue;
        }
        if (peek().is(TokenType::KEYWORD_INPUT) && peek(1).is(TokenType::PUNCTUATOR_LBRACE)) {
            auto inputItem = parseInputStatement();
            if (!inputItem) {
                return nullptr;
            }
            windowDecl->inputs.push_back(std::move(inputItem));
            continue;
        }
        if (peek().is(TokenType::KEYWORD_CLASS)) {
            auto classDecl = parseClassDeclarationStatement();
            if (!classDecl) {
                return nullptr;
            }
            windowDecl->classes.push_back(std::move(classDecl));
            continue;
        }
        if (peek().is(TokenType::KEYWORD_LOOP)) {
            auto loopStmt = parseLoopStatement();
            if (!loopStmt) {
                return nullptr;
            }
            windowDecl->loops.push_back(std::move(loopStmt));
            continue;
        }

        if (!consume(TokenType::IDENTIFIER)) {
            addError("Expected window property name, button block, text block, box block, input block, or class declaration");
            return nullptr;
        }

        std::string propertyName = peek(-1).text;
        if (!consume(TokenType::PUNCTUATOR_COLON)) {
            addError("Expected ':' after window property '" + propertyName + "'");
            return nullptr;
        }

        if (propertyName == "width") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for window width");
                return nullptr;
            }
            windowDecl->width = std::stoi(peek(-1).text);
        } else if (propertyName == "height" || propertyName == "high") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for window height");
                return nullptr;
            }
            windowDecl->height = std::stoi(peek(-1).text);
        } else if (propertyName == "title") {
            if (consume(TokenType::STRING_LITERAL)) {
                windowDecl->title = peek(-1).text;
            } else if (consume(TokenType::IDENTIFIER)) {
                windowDecl->title = peek(-1).text;
            } else {
                addError("Expected string or identifier for window title");
                return nullptr;
            }
        } else if (propertyName == "color") {
            if (consume(TokenType::IDENTIFIER) || consume(TokenType::STRING_LITERAL)) {
                windowDecl->color = peek(-1).text;
            } else {
                addError("Expected color name for window color");
                return nullptr;
            }
        } else if (propertyName == "style") {
            if (consume(TokenType::STRING_LITERAL) || consume(TokenType::IDENTIFIER)) {
                windowDecl->style = peek(-1).text;
            } else {
                addError("Expected string or identifier for window style");
                return nullptr;
            }
        } else {
            addError("Unknown window property: " + propertyName);
            return nullptr;
        }
    }

    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close window block");
        return nullptr;
    }

    if (windowDecl->width <= 0 || windowDecl->height <= 0) {
        addError("Window width and height must be greater than zero");
        return nullptr;
    }

    return windowDecl;
}

std::unique_ptr<ButtonStatement> Parser::parseButtonStatement() {
    SourceLocation loc = peek().location;

    if (!consume(TokenType::IDENTIFIER) || peek(-1).text != "button") {
        return nullptr;
    }

    if (!consume(TokenType::PUNCTUATOR_LBRACE)) {
        addError("Expected '{' after 'button'");
        return nullptr;
    }

    auto button = std::make_unique<ButtonStatement>(loc);

    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        if (peek().is(TokenType::IDENTIFIER) && peek(1).is(TokenType::PUNCTUATOR_COLON)) {
            advance();
            std::string propertyName = peek(-1).text;
            consume(TokenType::PUNCTUATOR_COLON);

            if (propertyName == "x") {
                if (!consume(TokenType::NUMBER_LITERAL)) {
                    addError("Expected integer literal for button x");
                    return nullptr;
                }
                button->x = std::stoi(peek(-1).text);
            } else if (propertyName == "y") {
                if (!consume(TokenType::NUMBER_LITERAL)) {
                    addError("Expected integer literal for button y");
                    return nullptr;
                }
                button->y = std::stoi(peek(-1).text);
            } else if (propertyName == "width") {
                if (!consume(TokenType::NUMBER_LITERAL)) {
                    addError("Expected integer literal for button width");
                    return nullptr;
                }
                button->width = std::stoi(peek(-1).text);
            } else if (propertyName == "height" || propertyName == "high") {
                if (!consume(TokenType::NUMBER_LITERAL)) {
                    addError("Expected integer literal for button height");
                    return nullptr;
                }
                button->height = std::stoi(peek(-1).text);
            } else if (propertyName == "text" || propertyName == "title") {
                if (consume(TokenType::STRING_LITERAL) || consume(TokenType::IDENTIFIER)) {
                    button->text = peek(-1).text;
                } else {
                    addError("Expected string or identifier for button text");
                    return nullptr;
                }
            } else {
                addError("Unknown button property: " + propertyName);
                return nullptr;
            }
            continue;
        }

        auto stmt = parseStatement();
        if (!stmt) {
            addError("Expected button property or statement");
            return nullptr;
        }
        button->body.push_back(std::move(stmt));
    }

    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close button block");
        return nullptr;
    }

    if (button->width <= 0 || button->height <= 0) {
        addError("Button width and height must be greater than zero");
        return nullptr;
    }

    return button;
}

std::unique_ptr<TextStatement> Parser::parseTextStatement() {
    SourceLocation loc = peek().location;

    if (!consume(TokenType::IDENTIFIER) || peek(-1).text != "text") {
        return nullptr;
    }

    if (!consume(TokenType::PUNCTUATOR_LBRACE)) {
        addError("Expected '{' after 'text'");
        return nullptr;
    }

    auto textItem = std::make_unique<TextStatement>(loc);

    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        if (!consume(TokenType::IDENTIFIER)) {
            addError("Expected text property name");
            return nullptr;
        }

        std::string propertyName = peek(-1).text;
        if (!consume(TokenType::PUNCTUATOR_COLON)) {
            addError("Expected ':' after text property '" + propertyName + "'");
            return nullptr;
        }

        if (propertyName == "x") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for text x");
                return nullptr;
            }
            textItem->x = std::stoi(peek(-1).text);
        } else if (propertyName == "y") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for text y");
                return nullptr;
            }
            textItem->y = std::stoi(peek(-1).text);
        } else if (propertyName == "width") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for text width");
                return nullptr;
            }
            textItem->width = std::stoi(peek(-1).text);
        } else if (propertyName == "height" || propertyName == "high") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for text height");
                return nullptr;
            }
            textItem->height = std::stoi(peek(-1).text);
        } else if (propertyName == "text" || propertyName == "title") {
            if (consume(TokenType::STRING_LITERAL) || consume(TokenType::IDENTIFIER)) {
                textItem->text = peek(-1).text;
            } else {
                addError("Expected string or identifier for text content");
                return nullptr;
            }
        } else {
            addError("Unknown text property: " + propertyName);
            return nullptr;
        }
    }

    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close text block");
        return nullptr;
    }

    if (textItem->width <= 0 || textItem->height <= 0) {
        addError("Text width and height must be greater than zero");
        return nullptr;
    }

    return textItem;
}

std::unique_ptr<BoxStatement> Parser::parseBoxStatement() {
    SourceLocation loc = peek().location;

    if (!consume(TokenType::IDENTIFIER) || peek(-1).text != "box") {
        return nullptr;
    }

    if (!consume(TokenType::PUNCTUATOR_LBRACE)) {
        addError("Expected '{' after 'box'");
        return nullptr;
    }

    auto boxItem = std::make_unique<BoxStatement>(loc);

    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        if (!consume(TokenType::IDENTIFIER)) {
            addError("Expected box property name");
            return nullptr;
        }

        std::string propertyName = peek(-1).text;
        if (!consume(TokenType::PUNCTUATOR_COLON)) {
            addError("Expected ':' after box property '" + propertyName + "'");
            return nullptr;
        }

        if (propertyName == "id") {
            if (consume(TokenType::IDENTIFIER) || consume(TokenType::STRING_LITERAL)) {
                boxItem->id = peek(-1).text;
            } else {
                addError("Expected identifier or string for box id");
                return nullptr;
            }
        } else if (propertyName == "x") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for box x");
                return nullptr;
            }
            boxItem->x = std::stoi(peek(-1).text);
        } else if (propertyName == "y") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for box y");
                return nullptr;
            }
            boxItem->y = std::stoi(peek(-1).text);
        } else if (propertyName == "width") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for box width");
                return nullptr;
            }
            boxItem->width = std::stoi(peek(-1).text);
        } else if (propertyName == "height" || propertyName == "high") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for box height");
                return nullptr;
            }
            boxItem->height = std::stoi(peek(-1).text);
        } else if (propertyName == "text" || propertyName == "title") {
            if (consume(TokenType::STRING_LITERAL) || consume(TokenType::IDENTIFIER)) {
                boxItem->text = peek(-1).text;
            } else {
                addError("Expected string or identifier for box text");
                return nullptr;
            }
        } else {
            addError("Unknown box property: " + propertyName);
            return nullptr;
        }
    }

    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close box block");
        return nullptr;
    }

    if (boxItem->width <= 0 || boxItem->height <= 0) {
        addError("Box width and height must be greater than zero");
        return nullptr;
    }

    return boxItem;
}

std::unique_ptr<InputStatement> Parser::parseInputStatement() {
    SourceLocation loc = peek().location;

    if (!consume(TokenType::KEYWORD_INPUT)) {
        return nullptr;
    }

    if (!consume(TokenType::PUNCTUATOR_LBRACE)) {
        addError("Expected '{' after 'input'");
        return nullptr;
    }

    auto inputItem = std::make_unique<InputStatement>(loc);

    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        if (!consume(TokenType::IDENTIFIER)) {
            addError("Expected input property name");
            return nullptr;
        }

        std::string propertyName = peek(-1).text;
        if (!consume(TokenType::PUNCTUATOR_COLON)) {
            addError("Expected ':' after input property '" + propertyName + "'");
            return nullptr;
        }

        if (propertyName == "id") {
            if (consume(TokenType::IDENTIFIER) || consume(TokenType::STRING_LITERAL)) {
                inputItem->id = peek(-1).text;
            } else {
                addError("Expected identifier or string for input id");
                return nullptr;
            }
        } else if (propertyName == "x") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for input x");
                return nullptr;
            }
            inputItem->x = std::stoi(peek(-1).text);
        } else if (propertyName == "y") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for input y");
                return nullptr;
            }
            inputItem->y = std::stoi(peek(-1).text);
        } else if (propertyName == "width") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for input width");
                return nullptr;
            }
            inputItem->width = std::stoi(peek(-1).text);
        } else if (propertyName == "height" || propertyName == "high") {
            if (!consume(TokenType::NUMBER_LITERAL)) {
                addError("Expected integer literal for input height");
                return nullptr;
            }
            inputItem->height = std::stoi(peek(-1).text);
        } else if (propertyName == "var") {
            if (consume(TokenType::IDENTIFIER)) {
                inputItem->varName = peek(-1).text;
            } else {
                addError("Expected identifier for input var");
                return nullptr;
            }
        } else {
            addError("Unknown input property: " + propertyName);
            return nullptr;
        }
    }

    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close input block");
        return nullptr;
    }

    if (inputItem->width <= 0 || inputItem->height <= 0) {
        addError("Input width and height must be greater than zero");
        return nullptr;
    }

    return inputItem;
}

std::unique_ptr<XraphicsObjectStatement> Parser::parseXraphicsObjectStatement() {
    SourceLocation loc = peek().location;

    // Parse: objectName = library.preset(params...)
    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected object name");
        return nullptr;
    }
    std::string objectName = peek(-1).text;

    if (!consume(TokenType::PUNCTUATOR_EQUAL)) {
        addError("Expected '=' after Xraphics object name");
        return nullptr;
    }

    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected x3d or x2d library name");
        return nullptr;
    }
    std::string library = peek(-1).text;
    if (library != "x3d" && library != "x2d") {
        addError("Expected 'x3d' or 'x2d', got '" + library + "'");
        return nullptr;
    }

    if (!consume(TokenType::PUNCTUATOR_DOT)) {
        addError("Expected '.' after library name");
        return nullptr;
    }

    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected preset name (e.g. box, sphere, rect)");
        return nullptr;
    }
    std::string preset = peek(-1).text;

    if (!consume(TokenType::PUNCTUATOR_LPAREN)) {
        addError("Expected '(' after preset name");
        return nullptr;
    }

    auto obj = std::make_unique<XraphicsObjectStatement>(loc);
    obj->objectName = objectName;
    obj->library = library;
    obj->preset = preset;

    // Parse parameters until ')'. Parameters are separated by whitespace (newlines).
    // Each parameter is: name = value
    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RPAREN)) {
        if (!consume(TokenType::IDENTIFIER)) {
            addError("Expected parameter name in Xraphics object definition");
            return nullptr;
        }
        std::string paramName = peek(-1).text;

        if (!consume(TokenType::PUNCTUATOR_EQUAL)) {
            addError("Expected '=' after parameter name '" + paramName + "'");
            return nullptr;
        }

        std::unique_ptr<Expression> value;

        // Parse value: tuple, color, number, float, string, or identifier
        if (peek().is(TokenType::PUNCTUATOR_LPAREN)) {
            // Tuple: (x, y, z) or (x, y)
            advance();  // consume '('
            std::vector<std::unique_ptr<Expression>> elements;
            while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RPAREN)) {
                auto elem = parseExpression();
                if (!elem) {
                    addError("Expected expression in tuple");
                    return nullptr;
                }
                elements.push_back(std::move(elem));
                if (peek().is(TokenType::PUNCTUATOR_COMMA)) {
                    advance();  // consume ','
                }
            }
            if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
                addError("Expected ')' to close tuple");
                return nullptr;
            }
            value = std::make_unique<TupleExpression>(std::move(elements), loc);
        } else if (peek().is(TokenType::COLOR_LITERAL)) {
            value = std::make_unique<ColorLiteral>(peek().text, peek().location);
            advance();
        } else if (peek().is(TokenType::STRING_LITERAL)) {
            value = std::make_unique<StringLiteral>(peek().text, peek().location);
            advance();
        } else if (peek().is(TokenType::NUMBER_LITERAL)) {
            value = std::make_unique<NumberLiteral>(std::stoll(peek().text), peek().location);
            advance();
        } else if (peek().is(TokenType::FLOAT_LITERAL)) {
            value = std::make_unique<FloatLiteral>(std::stod(peek().text), peek().location);
            advance();
        } else if (peek().is(TokenType::IDENTIFIER)) {
            value = std::make_unique<VariableExpression>(peek().text, peek().location);
            advance();
        } else if (peek().is(TokenType::PUNCTUATOR_MINUS)) {
            // Negative numeric literal: parse as unary minus expression
            value = parseExpression();
        } else {
            addError("Expected value for parameter '" + paramName + "', got: " + peek().toString());
            return nullptr;
        }

        obj->params.push_back(XraphicsParam(paramName, std::move(value)));
    }

    if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
        addError("Expected ')' to close Xraphics object definition");
        return nullptr;
    }

    return obj;
}

std::unique_ptr<ClassDeclarationStatement> Parser::parseClassDeclarationStatement() {
    SourceLocation loc = peek().location;

    if (!consume(TokenType::KEYWORD_CLASS)) {
        return nullptr;
    }

    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected class name after 'class'");
        return nullptr;
    }
    std::string className = peek(-1).text;

    if (!consume(TokenType::PUNCTUATOR_LBRACE)) {
        addError("Expected '{' after class name");
        return nullptr;
    }

    auto classDecl = std::make_unique<ClassDeclarationStatement>(loc);
    classDecl->className = className;

    // Parse class body: a collection of Xraphics object definitions
    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        // Each member is: objectName = x3d.preset(...) or x2d.preset(...)
        if (!peek().is(TokenType::IDENTIFIER) ||
            !peek(1).is(TokenType::PUNCTUATOR_EQUAL) ||
            !peek(2).is(TokenType::IDENTIFIER) ||
            (peek(2).text != "x3d" && peek(2).text != "x2d")) {
            addError("Expected Xraphics object definition in class body, got: " + peek().toString());
            // Skip to next recognizable token
            advance();
            continue;
        }

        auto obj = parseXraphicsObjectStatement();
        if (!obj) {
            return nullptr;
        }
        classDecl->objects.push_back(std::move(obj));
    }

    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close class definition");
        return nullptr;
    }

    return classDecl;
}

std::unique_ptr<Function> Parser::parseFunction() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_FN)) {
        return nullptr;
    }
    
    std::string ns;
    std::string name;
    
    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected function name after 'fn'");
        return nullptr;
    }
    
    name = peek(-1).text;
    
    if (consume(TokenType::PUNCTUATOR_COLON)) {
        ns = name;
        if (!consume(TokenType::IDENTIFIER)) {
            addError("Expected function name after namespace ':'");
            return nullptr;
        }
        name = peek(-1).text;
    }
    
    if (!consume(TokenType::PUNCTUATOR_LPAREN)) {
        addError("Expected '(' after function name");
        return nullptr;
    }
    
    std::vector<std::unique_ptr<VariableDeclaration>> params;
    
    if (!peek().is(TokenType::PUNCTUATOR_RPAREN)) {
        while (true) {
            if (!consume(TokenType::IDENTIFIER)) {
                addError("Expected parameter name");
                return nullptr;
            }
            std::string param_name = peek(-1).text;
            params.push_back(std::make_unique<VariableDeclaration>(param_name, loc));
            
            if (consume(TokenType::PUNCTUATOR_COMMA)) {
                continue;
            }
            break;
        }
    }
    
    if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
        addError("Expected ')' after function parameters");
        return nullptr;
    }
    
    auto body = parseBlockStatement();
    if (!body) {
        return nullptr;
    }
    
    if (ns.empty()) {
        return std::make_unique<Function>(name, std::move(params), std::move(body), loc);
    } else {
        return std::make_unique<Function>(name, ns, std::move(params), std::move(body), loc);
    }
}

std::unique_ptr<Statement> Parser::parseStatement() {
    if (isAtEnd()) return nullptr;
    
    if (peek().is(TokenType::KEYWORD_IMPORT) || peek().is(TokenType::KEYWORD_PERCENT_IMPORT)) {
        return parseImportStatement();
    } else if (peek().is(TokenType::KEYWORD_PRINT)) {
        return parsePrintStatement();
    } else if (peek().is(TokenType::KEYWORD_WHILE)) {
        return parseWhileStatement();
    } else if (peek().is(TokenType::KEYWORD_IF)) {
        return parseIfStatement();
    } else if (peek().is(TokenType::KEYWORD_BREAK)) {
        return parseBreakStatement();
    } else if (peek().is(TokenType::KEYWORD_BOOM)) {
        return parseBoomStatement();
    } else if (peek().is(TokenType::KEYWORD_BSOD)) {
        return parseBsodStatement();
    } else if (peek().is(TokenType::KEYWORD_BELIEVE)) {
        return parseBelieveStatement();
    } else if (peek().is(TokenType::KEYWORD_LIE)) {
        return parseLieStatement();
    } else if (peek().is(TokenType::KEYWORD_UN)) {
        return parseUnStatement();
    } else if (peek().is(TokenType::KEYWORD_IGNORE)) {
        return parseIgnoreStatement();
    } else if (peek().is(TokenType::KEYWORD_DO)) {
        return parseDoStatement();
    } else if (peek().is(TokenType::KEYWORD_PLEASE)) {
        // `please` and `please.` are two SEPARATE keywords. `please.` is the
        // statement modifier (thank you! + inner). A bare `please` is the
        // red-hot-only compliance keyword with no runtime effect.
        if (peek(1).is(TokenType::PUNCTUATOR_DOT)) {
            return parsePleaseStatement();
        }
        return parsePleaseNoticeStatement();
    } else if (peek().is(TokenType::KEYWORD_SHUTUP)) {
        return parseShutupStatement();
    } else if (peek().is(TokenType::KEYWORD_WRATH)) {
        return parseWrathStatement();
    } else if (peek().is(TokenType::KEYWORD_PARADOX)) {
        return parseParadoxStatement();
    } else if (peek().is(TokenType::KEYWORD_TRY)) {
        return parseTryExpectStatement();
    } else if (peek().is(TokenType::KEYWORD_SORRY)) {
        return parseSorryStatement();
    } else if (peek().is(TokenType::PUNCTUATOR_DOT_DOT_DOT)) {
        return parseEllipsisStatement();
    } else if (peek().is(TokenType::KEYWORD_SLEEP)) {
        return parseSleepStatement();
    } else if (peek().is(TokenType::KEYWORD_COME)) {
        return parseComeStatement();
    } else if (peek().is(TokenType::PUNCTUATOR_EXCLAIM)) {
        return parseOverriddenStatement();
    } else if (peek().is(TokenType::KEYWORD_RETURN)) {
        return parseReturnStatement();
    } else if (peek().is(TokenType::KEYWORD_FOR)) {
        return parseForInStatement();
    } else if (peek().is(TokenType::KEYWORD_LOOP)) {
        return parseLoopStatement();
    } else if (peek().is(TokenType::KEYWORD_WINDOW)) {
        return parseWindowStatement();
    } else if (peek().is(TokenType::KEYWORD_CLASS)) {
        // class declarations are only allowed inside window blocks (in .xf files)
        // or at top level (in .xfw files, handled by XfwSystem regex extraction)
        addError("class declarations must be inside 'window' blocks in .xf files");
        return nullptr;
    } else if (peek().is(TokenType::KEYWORD_FN)) {
        SourceLocation loc = peek().location;
        auto func = parseFunction();
        if (!func) return nullptr;
        return std::make_unique<FunctionDeclarationStatement>(std::move(func), loc);
    } else if (peek().is(TokenType::KEYWORD_INT)) {
        advance();
        return parseTypedAssignmentStatement(VarType::INT);
    } else if (peek().is(TokenType::KEYWORD_LONG)) {
        advance();
        return parseTypedAssignmentStatement(VarType::LONG);
    } else if (peek().is(TokenType::KEYWORD_FLOAT)) {
        advance();
        return parseTypedAssignmentStatement(VarType::FLOAT);
    } else if (peek().is(TokenType::KEYWORD_BOOL)) {
        advance();
        return parseTypedAssignmentStatement(VarType::BOOL);
    } else if (peek().is(TokenType::KEYWORD_STRING)) {
        advance();
        return parseTypedAssignmentStatement(VarType::STRING);
    } else if (peek().is(TokenType::PUNCTUATOR_LBRACE)) {
        return parseBlockStatement();
    } else if (peek().is(TokenType::IDENTIFIER)) {
        // EXP auto-fix: detect a plausible keyword typo, and — only after the
        // user explicitly confirms — correct it. Never silently modify source.
        {
            std::string fix = nearestKeywordName(peek().text);
            if (!fix.empty()) {
                // Inside `try`, the programmer chose to handle the operation's
                // errors themselves via `expect`, so the repair prompt is
                // skipped and the typo becomes a catchable parse error (recorded
                // by parseTryExpectStatement's error recovery, swallowed).
                if (inTryBody) {
                    return nullptr;
                }
                std::cout << "Unknown statement: " << peek().text << std::endl;
                std::cout << "Did you mean: " << fix << "?" << std::endl;
                std::cout << "Apply this fix? [y/N] " << std::flush;
                std::string answer;
                std::getline(std::cin, answer);
                bool confirmed = (!answer.empty() && (answer[0] == 'y' || answer[0] == 'Y'));
                if (confirmed) {
                    std::cout << "✓ Fixed" << std::endl;
                    tokens[current] = Token(keywordTokenType(fix), fix, peek().location);
                    return parseStatement();
                }
                std::cout << "Fix rejected — keeping original and stopping." << std::endl;
                // Keep the original error behaviour: report and abort parsing.
                addError("Unknown statement: " + peek().text);
                return nullptr;
            }
        }
        // Check for function call patterns:
        // 1. identifier(  - direct function call
        // 2. identifier:  - namespace:function() syntax
        // 3. identifier.  - block.function() syntax
        if (peek(1).is(TokenType::PUNCTUATOR_LPAREN) ||
            peek(1).is(TokenType::PUNCTUATOR_COLON) ||
            peek(1).is(TokenType::PUNCTUATOR_DOT)) {
            SourceLocation loc = peek().location;
            auto expr = parseExpression();
            if (!expr) return nullptr;
            return std::make_unique<ExpressionStatement>(std::move(expr), loc);
        }
        // Note: Xraphics object definitions (name = x3d.box(...)) are only allowed
        // inside class blocks (parseClassDeclarationStatement), not in regular statements.
        // Class blocks themselves are only allowed inside window blocks.
        return parseAssignmentStatement();
    } else {
        addError("Unexpected token: " + peek().toString());
        advance();
        return nullptr;
    }
}

std::unique_ptr<PrintStatement> Parser::parsePrintStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_PRINT)) {
        return nullptr;
    }
    
    if (!consume(TokenType::PUNCTUATOR_LPAREN)) {
        addError("Expected '(' after 'print'");
        return nullptr;
    }
    
    auto expr = parseExpression();
    if (!expr) {
        return nullptr;
    }

    auto printStmt = std::make_unique<PrintStatement>(std::move(expr), loc);

    if (consume(TokenType::PUNCTUATOR_COMMA)) {
        if (consume(TokenType::IDENTIFIER)) {
            printStmt->outputTarget = peek(-1).text;
        } else {
            addError("Expected output box identifier after ',' in print statement");
            return nullptr;
        }
    }
    
    if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
        addError("Expected ')' after print argument");
        return nullptr;
    }
    
    return printStmt;
}

std::unique_ptr<AssignmentStatement> Parser::parseAssignmentStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::IDENTIFIER)) {
        return nullptr;
    }
    
    std::string name = peek(-1).text;
    
    if (!consume(TokenType::PUNCTUATOR_EQUAL)) {
        addError("Expected '=' after variable name");
        return nullptr;
    }
    
    auto expr = parseExpression();
    if (!expr) {
        return nullptr;
    }
    
    bool alreadyDeclared = isVariableDeclared(name);
    if (!alreadyDeclared) {
        addWarning("Implicit type declaration for variable '" + name + "'. Consider using explicit type declaration (e.g., int " + name + " = ...)");
        declareVariable(name);
    }
    
    auto stmt = std::make_unique<AssignmentStatement>(name, std::move(expr), loc);
    stmt->isReassignment = alreadyDeclared;
    return stmt;
}

std::unique_ptr<AssignmentStatement> Parser::parseTypedAssignmentStatement(VarType type) {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected variable name after type");
        return nullptr;
    }
    
    std::string name = peek(-1).text;
    
    if (!consume(TokenType::PUNCTUATOR_EQUAL)) {
        addError("Expected '=' after variable name");
        return nullptr;
    }
    
    auto expr = parseExpression();
    if (!expr) {
        return nullptr;
    }
    
    declareVariable(name);
    
    return std::make_unique<AssignmentStatement>(name, std::move(expr), type, loc);
}

std::unique_ptr<BreakStatement> Parser::parseBreakStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_BREAK)) {
        return nullptr;
    }
    
    return std::make_unique<BreakStatement>(loc);
}

std::unique_ptr<BoomStatement> Parser::parseBoomStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_BOOM)) {
        return nullptr;
    }
    
    return std::make_unique<BoomStatement>(loc);
}

std::unique_ptr<BsodStatement> Parser::parseBsodStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_BSOD)) {
        return nullptr;
    }
    
    return std::make_unique<BsodStatement>(loc);
}

std::unique_ptr<BelieveStatement> Parser::parseBelieveStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_BELIEVE)) {
        return nullptr;
    }
    
    if (!consume(TokenType::STRING_LITERAL)) {
        addError("Expected a string proposition after 'believe', e.g. believe \"2 + 2 = 5\"");
        return nullptr;
    }
    
    std::string raw = peek(-1).text;
    return std::make_unique<BelieveStatement>(raw, loc);
}

std::unique_ptr<LieStatement> Parser::parseLieStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_LIE)) {
        return nullptr;
    }
    
    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected a variable name after 'lie'");
        return nullptr;
    }
    std::string name = peek(-1).text;
    
    if (!consume(TokenType::PUNCTUATOR_EQUAL)) {
        addError("Expected '=' after variable name in 'lie'");
        return nullptr;
    }
    
    auto expr = parseExpression();
    if (!expr) {
        return nullptr;
    }
    
    auto body = parseBlockStatement();
    if (!body) {
        addError("Expected '{' block after 'lie name = value'");
        return nullptr;
    }
    
    return std::make_unique<LieStatement>(name, std::move(expr), std::move(body), loc);
}

std::unique_ptr<UnStatement> Parser::parseUnStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_UN)) {
        return nullptr;
    }
    
    // First version supports disabling `print`, `boom`, `bsod`, `sleep`.
    TokenType target = peek().type;
    if (target == TokenType::KEYWORD_PRINT ||
        target == TokenType::KEYWORD_BOOM ||
        target == TokenType::KEYWORD_BSOD ||
        target == TokenType::KEYWORD_SLEEP) {
        advance();
    } else {
        addError("'un' can only disable a statement keyword (print / boom / bsod / sleep) in this version");
        return nullptr;
    }
    
    return std::make_unique<UnStatement>(target, loc);
}

std::unique_ptr<Statement> Parser::parseOverriddenStatement() {
    // `!` prefix: allow a single statement to bypass an active `un`.
    if (!consume(TokenType::PUNCTUATOR_EXCLAIM)) {
        return nullptr;
    }
    
    if (peek().is(TokenType::KEYWORD_PRINT)) {
        auto stmt = parsePrintStatement();
        if (stmt) {
            stmt->overridden = true;
        }
        return stmt;
    }
    if (peek().is(TokenType::KEYWORD_SLEEP)) {
        auto stmt = parseSleepStatement();
        if (stmt) {
            stmt->overridden = true;
        }
        return stmt;
    }
    
    addError("'!' can only override a statement like '!print(...)' in this version");
    return nullptr;
}

std::unique_ptr<Statement> Parser::parseIgnoreStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_IGNORE)) return nullptr;
    if (!consume(TokenType::PUNCTUATOR_DOT)) {
        addError("Expected '.' after 'ignore'");
        return nullptr;
    }
    auto inner = parseStatement();
    if (!inner) return nullptr;
    return std::make_unique<IgnoreStatement>(std::move(inner), loc);
}

std::unique_ptr<Statement> Parser::parseDoStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_DO)) return nullptr;
    if (!consume(TokenType::PUNCTUATOR_DOT)) {
        addError("Expected '.' after 'do'");
        return nullptr;
    }
    auto inner = parseStatement();
    if (!inner) return nullptr;
    return std::make_unique<DoStatement>(std::move(inner), loc);
}

std::unique_ptr<Statement> Parser::parsePleaseStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_PLEASE)) return nullptr;
    if (!consume(TokenType::PUNCTUATOR_DOT)) {
        addError("Expected '.' after 'please'");
        return nullptr;
    }
    auto inner = parseStatement();
    if (!inner) return nullptr;
    return std::make_unique<PleaseStatement>(std::move(inner), loc);
}

// EXP bare `please`: a keyword on its own with no runtime effect (see
// PleaseNoticeStatement / rage.md). Only meaningful while the compiler is
// red-hot; parsing is unconditional so it stays valid everywhere.
std::unique_ptr<Statement> Parser::parsePleaseNoticeStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_PLEASE)) return nullptr;
    return std::make_unique<PleaseNoticeStatement>(loc);
}

std::unique_ptr<Statement> Parser::parseShutupStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_SHUTUP)) return nullptr;
    // The compiler apologizes and shuts up (suppresses all following warnings).
    std::cout << "o……o…ok" << std::endl;
    xfawa::ErrorReporter::get().setWarningsEnabled(false);
    suppressWarnings = true;
    return std::make_unique<ShutupStatement>(loc);
}

std::unique_ptr<Statement> Parser::parseEllipsisStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::PUNCTUATOR_DOT_DOT_DOT)) return nullptr;
    return std::make_unique<EllipsisStatement>(loc);
}

std::unique_ptr<SleepStatement> Parser::parseSleepStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_SLEEP)) return nullptr;

    if (!consume(TokenType::PUNCTUATOR_LPAREN)) {
        addError("Expected '(' after 'sleep'");
        return nullptr;
    }

    auto expr = parseExpression();
    if (!expr) return nullptr;

    if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
        addError("Expected ')' after sleep duration");
        return nullptr;
    }

    return std::make_unique<SleepStatement>(std::move(expr), loc);
}

// EXP `come`: `come <line>` or `come if(<cond>) <line>`.
std::unique_ptr<Statement> Parser::parseComeStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_COME)) return nullptr;

    std::unique_ptr<Expression> cond;
    if (peek().is(TokenType::KEYWORD_IF)) {
        advance();
        if (!consume(TokenType::PUNCTUATOR_LPAREN)) {
            addError("Expected '(' after 'come if'");
            return nullptr;
        }
        cond = parseExpression();
        if (!cond) return nullptr;
        if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
            addError("Expected ')' after come condition");
            return nullptr;
        }
    }

    if (!peek().isOneOf(TokenType::NUMBER_LITERAL, TokenType::LONG_LITERAL)) {
        addError("Expected a line number after 'come'");
        return nullptr;
    }

    std::string text = peek().text;
    if (!text.empty() && (text.back() == 'L' || text.back() == 'l')) {
        text.pop_back();
    }
    int targetLine = 0;
    try {
        targetLine = static_cast<int>(std::stoll(text));
    } catch (...) {
        targetLine = 0;
    }
    advance();

    if (targetLine < 1) {
        addError("come: invalid target line number " + std::to_string(targetLine) + " (line numbers start at 1)");
        return nullptr;
    }

    return std::make_unique<ComeStatement>(targetLine, std::move(cond), loc);
}

std::unique_ptr<Statement> Parser::parseWrathStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_WRATH)) return nullptr;

    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected variable name after 'wrath'");
        return nullptr;
    }
    std::string name = peek(-1).text;

    if (!consume(TokenType::PUNCTUATOR_EQUAL)) {
        addError("Expected '=' after variable name in 'wrath'");
        return nullptr;
    }

    auto expr = parseExpression();
    if (!expr) return nullptr;

    return std::make_unique<WrathStatement>(name, std::move(expr), loc);
}

std::unique_ptr<Statement> Parser::parseParadoxStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_PARADOX)) return nullptr;

    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected variable name after 'paradox'");
        return nullptr;
    }
    std::string name = peek(-1).text;

    return std::make_unique<ParadoxStatement>(name, loc);
}

std::unique_ptr<Statement> Parser::parseTryExpectStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_TRY)) return nullptr;

    if (!peek().is(TokenType::PUNCTUATOR_LBRACE)) {
        addError("Expected '{' block after 'try'");
        return nullptr;
    }
    advance(); // consume '{'

    // `try` 块是编译期错误检查区：可包含任意数量的语句（运行时都不执行）。
    // 在 try 内发生的解析阶段错误（如关键字拼写错误 `prin`）通过错误恢复
    // 记录下来并吞掉 parser 错误，交给语义分析阶段判定是否被 try 拦截。
    auto tryBlock = std::make_unique<BlockStatement>(loc);
    bool parseFailed = false;
    inTryBody = true;
    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        size_t errBefore = errors.size();
        auto stmt = parseStatement();
        if (stmt) {
            tryBlock->statements.push_back(std::move(stmt));
        } else {
            // A parse-stage error occurred inside the try check zone: record it
            // as a catchable error and swallow the parser-side diagnostics.
            parseFailed = true;
            errors.resize(errBefore);
            recoverTryBlockError();
        }
    }
    inTryBody = false;

    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close 'try' block");
        return nullptr;
    }

    if (!consume(TokenType::KEYWORD_EXPECT)) {
        addError("Expected 'expect' after the 'try' block");
        return nullptr;
    }

    auto expectBlock = parseBlockStatement();
    if (!expectBlock) {
        addError("Expected '{' block after 'expect'");
        return nullptr;
    }

    auto te = std::make_unique<TryExpectStatement>(std::move(tryBlock), std::move(expectBlock), loc);
    te->parseFailed = parseFailed;
    return te;
}

// Error recovery inside the try check zone: skip forward past the offending
// tokens, balancing nested braces so a `}` that closes an inner block
// (if/while/function body, even a malformed one) is never mistaken for the
// try block's own closing brace. Stops at the try block's closing '}' (depth 0)
// or end-of-input; the loop in parseTryExpectStatement consumes that '}'.
void Parser::recoverTryBlockError() {
    int depth = 0;
    while (!isAtEnd()) {
        if (peek().is(TokenType::PUNCTUATOR_RBRACE)) {
            if (depth == 0) return;
            --depth;
            advance();
            continue;
        }
        if (peek().is(TokenType::PUNCTUATOR_LBRACE)) {
            ++depth;
        }
        advance();
    }
}

std::unique_ptr<Statement> Parser::parseSorryStatement() {
    SourceLocation loc = peek().location;
    if (!consume(TokenType::KEYWORD_SORRY)) return nullptr;
    return std::make_unique<SorryStatement>(loc);
}

std::unique_ptr<ReturnStatement> Parser::parseReturnStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_RETURN)) {
        return nullptr;
    }
    
    auto expr = parseExpression();
    if (!expr) {
        return nullptr;
    }
    
    return std::make_unique<ReturnStatement>(std::move(expr), loc);
}

std::unique_ptr<BlockStatement> Parser::parseBlockStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::PUNCTUATOR_LBRACE)) {
        return nullptr;
    }
    
    std::vector<std::unique_ptr<Statement>> statements;
    
    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        auto stmt = parseStatement();
        if (stmt) {
            statements.push_back(std::move(stmt));
        } else {
            return nullptr;
        }
    }
    
    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close block");
        return nullptr;
    }
    
    return std::make_unique<BlockStatement>(std::move(statements), loc);
}

std::unique_ptr<WhileStatement> Parser::parseWhileStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_WHILE)) {
        return nullptr;
    }
    
    if (!consume(TokenType::PUNCTUATOR_LPAREN)) {
        addError("Expected '(' after 'while'");
        return nullptr;
    }
    
    auto cond = parseExpression();
    if (!cond) {
        return nullptr;
    }
    
    if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
        addError("Expected ')' after while condition");
        return nullptr;
    }
    
    auto body = parseStatement();
    if (!body) {
        return nullptr;
    }
    
    return std::make_unique<WhileStatement>(std::move(cond), std::move(body), loc);
}

std::unique_ptr<LoopStatement> Parser::parseLoopStatement() {
    SourceLocation loc = peek().location;

    if (!consume(TokenType::KEYWORD_LOOP)) {
        return nullptr;
    }

    if (!consume(TokenType::PUNCTUATOR_LBRACE)) {
        addError("Expected '{' after 'loop'");
        return nullptr;
    }

    auto loopStmt = std::make_unique<LoopStatement>(loc);
    while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RBRACE)) {
        auto stmt = parseStatement();
        if (stmt) {
            loopStmt->body.push_back(std::move(stmt));
        } else {
            // Skip to recover
            advance();
        }
    }

    if (!consume(TokenType::PUNCTUATOR_RBRACE)) {
        addError("Expected '}' to close 'loop' block");
        return nullptr;
    }

    return loopStmt;
}

std::unique_ptr<ForInStatement> Parser::parseForInStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_FOR)) {
        return nullptr;
    }
    
    if (!consume(TokenType::IDENTIFIER)) {
        addError("Expected variable name after 'for'");
        return nullptr;
    }
    
    std::string varName = peek(-1).text;
    
    if (!consume(TokenType::IDENTIFIER) || peek(-1).text != "in") {
        addError("Expected 'in' after loop variable");
        return nullptr;
    }
    
    auto iterable = parseExpression();
    if (!iterable) {
        return nullptr;
    }
    
    auto body = parseStatement();
    if (!body) {
        return nullptr;
    }
    
    return std::make_unique<ForInStatement>(varName, std::move(iterable), std::move(body), loc);
}

std::unique_ptr<IfStatement> Parser::parseIfStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_IF)) {
        return nullptr;
    }
    
    auto cond = parseExpression();
    if (!cond) {
        return nullptr;
    }
    
    auto thenBranch = parseStatement();
    if (!thenBranch) {
        return nullptr;
    }
    
    std::vector<std::pair<std::unique_ptr<Expression>, std::unique_ptr<Statement>>> elseIfBranches;
    while (true) {
        size_t savedPos = current;
        if (consume(TokenType::KEYWORD_ELSE) && consume(TokenType::KEYWORD_IF)) {
            auto elseIfCond = parseExpression();
            if (!elseIfCond) {
                return nullptr;
            }
            auto elseIfThen = parseStatement();
            if (!elseIfThen) {
                return nullptr;
            }
            elseIfBranches.push_back({std::move(elseIfCond), std::move(elseIfThen)});
        } else {
            current = savedPos;
            break;
        }
    }
    
    std::unique_ptr<Statement> elseBranch = nullptr;
    if (consume(TokenType::KEYWORD_ELSE)) {
        elseBranch = parseStatement();
    }
    
    auto ifStmt = std::make_unique<IfStatement>(std::move(cond), std::move(thenBranch), std::move(elseBranch), loc);
    ifStmt->elseIfBranches = std::move(elseIfBranches);
    
    return ifStmt;
}

std::unique_ptr<Expression> Parser::parseExpression() {
    return parseLogicalOr();
}

std::unique_ptr<Expression> Parser::parseLogicalOr() {
    auto left = parseLogicalAnd();
    
    while (!isAtEnd() && (peek().is(TokenType::PUNCTUATOR_OR) || peek().is(TokenType::KEYWORD_OR))) {
        BinaryOpType op = BinaryOpType::OR;
        advance();
        auto right = parseLogicalAnd();
        left = std::make_unique<BinaryOp>(op, std::move(left), std::move(right), peek().location);
    }
    
    return left;
}

std::unique_ptr<Expression> Parser::parseLogicalAnd() {
    auto left = parseEquality();
    
    while (!isAtEnd() && (peek().is(TokenType::PUNCTUATOR_AND) || peek().is(TokenType::KEYWORD_AND))) {
        BinaryOpType op = BinaryOpType::AND;
        advance();
        auto right = parseEquality();
        left = std::make_unique<BinaryOp>(op, std::move(left), std::move(right), peek().location);
    }
    
    return left;
}

std::unique_ptr<Expression> Parser::parseEquality() {
    auto left = parseRelational();
    
    while (!isAtEnd()) {
        BinaryOpType op;
        if (consume(TokenType::PUNCTUATOR_EQUAL_EQUAL)) {
            op = BinaryOpType::EQUAL;
        } else if (consume(TokenType::PUNCTUATOR_EXCLAIM_EQUAL)) {
            op = BinaryOpType::NOT_EQUAL;
        } else {
            break;
        }
        auto right = parseRelational();
        left = std::make_unique<BinaryOp>(op, std::move(left), std::move(right), peek().location);
    }
    
    return left;
}

std::unique_ptr<Expression> Parser::parseRelational() {
    auto left = parseAdditive();
    
    while (!isAtEnd()) {
        BinaryOpType op;
        if (consume(TokenType::PUNCTUATOR_LESS_EQUAL)) {
            op = BinaryOpType::LESS_EQUAL;
        } else if (consume(TokenType::PUNCTUATOR_GREATER_EQUAL)) {
            op = BinaryOpType::GREATER_EQUAL;
        } else if (consume(TokenType::PUNCTUATOR_LESS)) {
            op = BinaryOpType::LESS;
        } else if (consume(TokenType::PUNCTUATOR_GREATER)) {
            op = BinaryOpType::GREATER;
        } else {
            break;
        }
        auto right = parseAdditive();
        left = std::make_unique<BinaryOp>(op, std::move(left), std::move(right), peek().location);
    }
    
    return left;
}

std::unique_ptr<Expression> Parser::parseAdditive() {
    auto left = parseMultiplicative();
    
    while (!isAtEnd()) {
        BinaryOpType op;
        if (consume(TokenType::PUNCTUATOR_PLUS)) {
            op = BinaryOpType::ADD;
        } else if (consume(TokenType::PUNCTUATOR_MINUS)) {
            op = BinaryOpType::SUB;
        } else {
            break;
        }
        auto right = parseMultiplicative();
        left = std::make_unique<BinaryOp>(op, std::move(left), std::move(right), peek().location);
    }
    
    return left;
}

std::unique_ptr<Expression> Parser::parseMultiplicative() {
    auto left = parseUnary();
    
    while (!isAtEnd()) {
        BinaryOpType op;
        if (consume(TokenType::PUNCTUATOR_STAR)) {
            op = BinaryOpType::MUL;
        } else if (consume(TokenType::PUNCTUATOR_SLASH)) {
            op = BinaryOpType::DIV;
        } else if (consume(TokenType::PUNCTUATOR_PERCENT)) {
            op = BinaryOpType::MOD;
        } else {
            break;
        }
        auto right = parseUnary();
        left = std::make_unique<BinaryOp>(op, std::move(left), std::move(right), peek().location);
    }
    
    return left;
}

std::unique_ptr<Expression> Parser::parseUnary() {
    if (consume(TokenType::PUNCTUATOR_MINUS)) {
        auto expr = parseUnary();
        return std::make_unique<UnaryOp>(UnaryOpType::NEGATE, std::move(expr), peek().location);
    }
    
    if (consume(TokenType::PUNCTUATOR_EXCLAIM)) {
        auto expr = parseUnary();
        return std::make_unique<UnaryOp>(UnaryOpType::NOT, std::move(expr), peek().location);
    }
    
    return parsePrimary();
}

std::unique_ptr<Expression> Parser::parsePrimary() {
    if (consume(TokenType::O_LITERAL)) {
        return std::make_unique<OLiteralExpression>(peek(-1).text, peek(-1).location);
    }
    
    if (consume(TokenType::NUMBER_LITERAL)) {
        int64_t value = std::stoll(peek(-1).text);
        return std::make_unique<NumberLiteral>(value, peek(-1).location);
    }
    
    if (consume(TokenType::LONG_LITERAL)) {
        int64_t value = std::stoll(peek(-1).text);
        return std::make_unique<NumberLiteral>(value, peek(-1).location);
    }
    
    if (consume(TokenType::FLOAT_LITERAL)) {
        double value = std::stod(peek(-1).text);
        return std::make_unique<FloatLiteral>(value, peek(-1).location);
    }
    
    if (consume(TokenType::KEYWORD_TRUE)) {
        return std::make_unique<BooleanLiteral>(true, peek(-1).location);
    }
    
    if (consume(TokenType::KEYWORD_FALSE)) {
        return std::make_unique<BooleanLiteral>(false, peek(-1).location);
    }
    
    if (consume(TokenType::STRING_LITERAL)) {
        std::string value = peek(-1).text;
        return std::make_unique<StringLiteral>(value, peek(-1).location);
    }
    
    if (peek().is(TokenType::PUNCTUATOR_LBRACKET)) {
        return parseArrayLiteral();
    }
    
    if (consume(TokenType::KEYWORD_INPUT)) {
        SourceLocation loc = peek(-1).location;
        
        if (!consume(TokenType::PUNCTUATOR_LPAREN)) {
            addError("Expected '(' after 'input'");
            return nullptr;
        }
        
        if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
            addError("Expected ')' after 'input('");
            return nullptr;
        }
        
        std::vector<std::unique_ptr<Expression>> args;
        return std::make_unique<CallExpression>("input", std::move(args), loc);
    }
    
    if (consume(TokenType::PUNCTUATOR_LPAREN)) {
        auto expr = parseExpression();
        if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
            addError("Expected ')' after expression");
            return nullptr;
        }
        return expr;
    }
    
    if (consume(TokenType::IDENTIFIER)) {
        std::string name = peek(-1).text;
        SourceLocation loc = peek(-1).location;

        // EXP o-literal: a purely 'o'-sequence identifier (o, oo, ooo, ...) that
        // is NOT declared as a variable is interpreted as an o-literal. A
        // declared variable named e.g. `o` takes priority and stays a variable.
        {
            bool allO = !name.empty() && name.find_first_not_of('o') == std::string::npos;
            if (allO && !isVariableDeclared(name)) {
                return std::make_unique<OLiteralExpression>(name, loc);
            }
        }

        std::string blockName;

        // 支持 block.function() 语法（使用点号）
        if (consume(TokenType::PUNCTUATOR_DOT)) {
            blockName = name;
            if (!consume(TokenType::IDENTIFIER)) {
                addError("Expected function name after block name '.'");
                return nullptr;
            }
            name = peek(-1).text;

            // Handle api.txt input/camera member access patterns
            // mouse.x, mouse.y, mouse.dx, mouse.dy, mouse.left, mouse.right, mouse.middle, mouse.wheel
            // keyboard.w/a/s/d/space/shift/ctrl
            if (blockName == "mouse" || blockName == "keyboard") {
                // These are property accesses (no parens), generate special CallExpression
                std::string specialName = "__" + blockName + "_" + name;
                std::vector<std::unique_ptr<Expression>> noArgs;
                auto result = std::make_unique<CallExpression>(specialName, std::move(noArgs), loc);
                return parsePostfix(std::move(result));
            }

            // camera.move.forward(d), camera.move.backward(d), etc.
            // camera.look(yaw=..., pitch=...)
            if (blockName == "camera" && name == "move") {
                if (consume(TokenType::PUNCTUATOR_DOT)) {
                    if (!consume(TokenType::IDENTIFIER)) {
                        addError("Expected direction after 'camera.move.'");
                        return nullptr;
                    }
                    std::string dir = peek(-1).text;
                    if (!consume(TokenType::PUNCTUATOR_LPAREN)) {
                        addError("Expected '(' after camera.move." + dir);
                        return nullptr;
                    }
                    auto arg = parseExpression();
                    if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
                        addError("Expected ')' after camera.move." + dir + " argument");
                        return nullptr;
                    }
                    std::vector<std::unique_ptr<Expression>> args;
                    if (arg) args.push_back(std::move(arg));
                    std::string specialName = "__camera_move_" + dir;
                    auto result = std::make_unique<CallExpression>(specialName, std::move(args), loc);
                    return parsePostfix(std::move(result));
                }
            }

            // camera.look(yaw=..., pitch=...) — parse two arguments
            if (blockName == "camera" && name == "look") {
                if (!consume(TokenType::PUNCTUATOR_LPAREN)) {
                    addError("Expected '(' after camera.look");
                    return nullptr;
                }
                // Parse named args: yaw=expr, pitch=expr (order may vary)
                std::unique_ptr<Expression> yawArg, pitchArg;
                while (!isAtEnd() && !peek().is(TokenType::PUNCTUATOR_RPAREN)) {
                    if (peek().is(TokenType::IDENTIFIER) && peek(1).is(TokenType::PUNCTUATOR_EQUAL)) {
                        std::string argName = peek().text;
                        advance();  // consume identifier
                        advance();  // consume '='
                        auto val = parseExpression();
                        if (argName == "yaw") yawArg = std::move(val);
                        else if (argName == "pitch") pitchArg = std::move(val);
                    } else {
                        // positional arg fallback
                        auto val = parseExpression();
                        if (!yawArg) yawArg = std::move(val);
                        else if (!pitchArg) pitchArg = std::move(val);
                    }
                    if (peek().is(TokenType::PUNCTUATOR_COMMA)) advance();
                }
                if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
                    addError("Expected ')' after camera.look arguments");
                    return nullptr;
                }
                std::vector<std::unique_ptr<Expression>> args;
                if (yawArg) args.push_back(std::move(yawArg));
                else args.push_back(std::make_unique<NumberLiteral>(0, loc));
                if (pitchArg) args.push_back(std::move(pitchArg));
                else args.push_back(std::make_unique<NumberLiteral>(0, loc));
                auto result = std::make_unique<CallExpression>("__camera_look", std::move(args), loc);
                return parsePostfix(std::move(result));
            }
        }
        // 兼容旧的 ns:name() 语法（使用冒号）
        else if (consume(TokenType::PUNCTUATOR_COLON)) {
            blockName = name;
            if (!consume(TokenType::IDENTIFIER)) {
                addError("Expected function name after namespace ':'");
                return nullptr;
            }
            name = peek(-1).text;
        }
        
        if (consume(TokenType::PUNCTUATOR_LPAREN)) {
            std::vector<std::unique_ptr<Expression>> args;
            
            if (!peek().is(TokenType::PUNCTUATOR_RPAREN)) {
                while (true) {
                    auto arg = parseExpression();
                    if (arg) {
                        args.push_back(std::move(arg));
                    } else {
                        return nullptr;
                    }
                    
                    if (consume(TokenType::PUNCTUATOR_COMMA)) {
                        continue;
                    }
                    break;
                }
            }
            
            if (!consume(TokenType::PUNCTUATOR_RPAREN)) {
                addError("Expected ')' after function arguments");
                return nullptr;
            }
            
            std::unique_ptr<Expression> result;
            if (blockName.empty()) {
                result = std::make_unique<CallExpression>(name, std::move(args), loc);
            } else {
                result = std::make_unique<CallExpression>(name, blockName, std::move(args), loc);
            }
            return parsePostfix(std::move(result));
        }
        
        if (consume(TokenType::PUNCTUATOR_LBRACKET)) {
            auto startExpr = parseExpression();
            if (!startExpr) {
                addError("Expected expression for array range start");
                return nullptr;
            }
            
            if (!consume(TokenType::PUNCTUATOR_DOT_DOT_DOT)) {
                if (!consume(TokenType::PUNCTUATOR_RBRACKET)) {
                    addError("Expected ']' after array index");
                    return nullptr;
                }
                auto varExpr = std::make_unique<VariableExpression>(name, loc);
                return std::make_unique<ArrayIndexExpression>(std::move(varExpr), std::move(startExpr), loc);
            }
            
            auto endExpr = parseExpression();
            if (!endExpr) {
                addError("Expected expression for array range end");
                return nullptr;
            }
            
            if (!consume(TokenType::PUNCTUATOR_RBRACKET)) {
                addError("Expected ']' after array range");
                return nullptr;
            }
            
            auto varExprForRange = std::make_unique<VariableExpression>(name, loc);
            return std::make_unique<ArrayRangeExpression>(std::move(varExprForRange), std::move(startExpr), std::move(endExpr), loc);
        }
        
        auto varExpr = std::make_unique<VariableExpression>(name, loc);
        return parsePostfix(std::move(varExpr));
    }
    
    addError("Expected expression, got: " + peek().toString());
    return nullptr;
}

std::unique_ptr<Expression> Parser::parseArrayLiteral() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::PUNCTUATOR_LBRACKET)) {
        return nullptr;
    }
    
    if (peek().is(TokenType::PUNCTUATOR_RBRACKET)) {
        advance();
        return std::make_unique<ArrayLiteral>(std::vector<std::unique_ptr<Expression>>(), loc);
    }
    
    auto firstExpr = parseExpression();
    if (!firstExpr) {
        return nullptr;
    }
    
    if (consume(TokenType::PUNCTUATOR_DOT_DOT_DOT)) {
        auto endExpr = parseExpression();
        if (!endExpr) {
            addError("Expected expression after '...' in range");
            return nullptr;
        }
        
        if (!consume(TokenType::PUNCTUATOR_RBRACKET)) {
            addError("Expected ']' after range");
            return nullptr;
        }
        
        return std::make_unique<ArrayLiteral>(std::move(firstExpr), std::move(endExpr), loc);
    }
    
    std::vector<std::unique_ptr<Expression>> elements;
    elements.push_back(std::move(firstExpr));
    
    while (consume(TokenType::PUNCTUATOR_COMMA)) {
        auto elem = parseExpression();
        if (!elem) {
            return nullptr;
        }
        elements.push_back(std::move(elem));
    }
    
    if (!consume(TokenType::PUNCTUATOR_RBRACKET)) {
        addError("Expected ']' after array elements");
        return nullptr;
    }
    
    return std::make_unique<ArrayLiteral>(std::move(elements), loc);
}

std::unique_ptr<Expression> Parser::parsePostfix(std::unique_ptr<Expression> expr) {
    while (peek().is(TokenType::PUNCTUATOR_LBRACKET)) {
        advance();
        auto indexExpr = parseExpression();
        if (!indexExpr) {
            return nullptr;
        }
        
        if (consume(TokenType::PUNCTUATOR_DOT_DOT_DOT)) {
            auto endExpr = parseExpression();
            if (!endExpr) {
                addError("Expected expression after '...' in array slice");
                return nullptr;
            }
            
            if (!consume(TokenType::PUNCTUATOR_RBRACKET)) {
                addError("Expected ']' after array slice");
                return nullptr;
            }
            
            expr = std::make_unique<ArrayRangeExpression>(std::move(expr), std::move(indexExpr), std::move(endExpr), expr->location);
            continue;
        }
        
        if (!consume(TokenType::PUNCTUATOR_RBRACKET)) {
            addError("Expected ']' after array index");
            return nullptr;
        }
        
        expr = std::make_unique<ArrayIndexExpression>(std::move(expr), std::move(indexExpr), expr->location);
    }
    
    return expr;
}

std::unique_ptr<ImportStatement> Parser::parseImportStatement() {
    SourceLocation loc = peek().location;
    
    if (!consume(TokenType::KEYWORD_IMPORT) && !consume(TokenType::KEYWORD_PERCENT_IMPORT)) {
        return nullptr;
    }
    
    if (!consume(TokenType::STRING_LITERAL)) {
        addError("Expected string literal after 'import'");
        return nullptr;
    }
    
    std::string name = peek(-1).text;
    
    std::string path;
    ImportType type = ImportType::MOD;
    
    size_t lastDot = name.find_last_of('.');
    if (lastDot != std::string::npos && lastDot < name.length() - 1) {
        std::string ext = name.substr(lastDot);
        if (ext == ".xfmod") {
            type = ImportType::MOD;
            path = "mods/" + name;
        } else if (ext == ".xfw") {
            type = ImportType::XFW;
            path = "libs/" + name;
        } else {
            type = ImportType::FILE;
            path = name;
        }
    } else {
        path = "mods/" + name + ".xfmod";
    }
    
    return std::make_unique<ImportStatement>(name, path, type, loc);
}

std::string Parser::statementTypeToString(Statement* stmt) {
    if (dynamic_cast<PrintStatement*>(stmt)) return "print";
    if (dynamic_cast<AssignmentStatement*>(stmt)) return "assignment";
    if (dynamic_cast<WhileStatement*>(stmt)) return "while";
    if (dynamic_cast<IfStatement*>(stmt)) return "if";
    if (dynamic_cast<BreakStatement*>(stmt)) return "break";
    if (dynamic_cast<ReturnStatement*>(stmt)) return "return";
    if (dynamic_cast<BlockStatement*>(stmt)) return "block";
    if (dynamic_cast<ImportStatement*>(stmt)) return "import";
    return "unknown";
}

}
