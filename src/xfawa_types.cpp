#include "xfawa_types.h"

namespace xfawa {

std::string varTypeToString(VarType type) {
    switch (type) {
        case VarType::UNKNOWN: return "auto";
        case VarType::INT: return "int";
        case VarType::LONG: return "long";
        case VarType::FLOAT: return "float";
        case VarType::BOOL: return "bool";
        case VarType::STRING: return "string";
        case VarType::ARRAY_INT: return "int[]";
        case VarType::ARRAY_LONG: return "long[]";
        case VarType::ARRAY_FLOAT: return "float[]";
        case VarType::ARRAY_BOOL: return "bool[]";
        case VarType::ARRAY_STRING: return "string[]";
        default: return "unknown";
    }
}

std::string nodeTypeToString(NodeType type) {
    switch (type) {
        case NodeType::NONE: return "none";
        case NodeType::EXPRESSION: return "expression";
        case NodeType::NUMBER_LITERAL: return "number_literal";
        case NodeType::FLOAT_LITERAL: return "float_literal";
        case NodeType::STRING_LITERAL: return "string_literal";
        case NodeType::BOOLEAN_LITERAL: return "boolean_literal";
        case NodeType::VARIABLE_EXPRESSION: return "variable_expression";
        case NodeType::BINARY_OP: return "binary_op";
        case NodeType::UNARY_OP: return "unary_op";
        case NodeType::CALL_EXPRESSION: return "call_expression";
        case NodeType::ARRAY_RANGE_EXPRESSION: return "array_range_expression";
        case NodeType::ARRAY_LITERAL: return "array_literal";
        case NodeType::ARRAY_INDEX_EXPRESSION: return "array_index_expression";
        case NodeType::O_LITERAL_EXPRESSION: return "o_literal_expression";
        case NodeType::PARADOX_EXPRESSION: return "paradox_expression";
        case NodeType::GHOST_EXPRESSION: return "ghost_expression";
        case NodeType::WRATH_STATEMENT: return "wrath_statement";
        case NodeType::PARADOX_STATEMENT: return "paradox_statement";
        case NodeType::TRY_EXPECT_STATEMENT: return "try_expect_statement";
        case NodeType::SORRY_STATEMENT: return "sorry_statement";
        case NodeType::PLEASE_STATEMENT: return "please_statement";
        case NodeType::PLEASE_NOTICE_STATEMENT: return "please_notice_statement";
        case NodeType::SLEEP_STATEMENT: return "sleep_statement";
        case NodeType::COME_STATEMENT: return "come_statement";
        case NodeType::STATEMENT: return "statement";
        case NodeType::PRINT_STATEMENT: return "print_statement";
        case NodeType::EXPRESSION_STATEMENT: return "expression_statement";
        case NodeType::ASSIGNMENT_STATEMENT: return "assignment_statement";
        case NodeType::BREAK_STATEMENT: return "break_statement";
        case NodeType::RETURN_STATEMENT: return "return_statement";
        case NodeType::BLOCK_STATEMENT: return "block_statement";
        case NodeType::WHILE_STATEMENT: return "while_statement";
        case NodeType::IF_STATEMENT: return "if_statement";
        case NodeType::IMPORT_STATEMENT: return "import_statement";
        case NodeType::FUNCTION_DECLARATION: return "function_declaration";
        case NodeType::FOR_IN_STATEMENT: return "for_in_statement";
        case NodeType::TYPED_ASSIGNMENT_STATEMENT: return "typed_assignment_statement";
        case NodeType::WINDOW_STATEMENT: return "window_statement";
        case NodeType::BUTTON_STATEMENT: return "button_statement";
        case NodeType::TEXT_STATEMENT: return "text_statement";
        case NodeType::BOX_STATEMENT: return "box_statement";
        case NodeType::INPUT_STATEMENT: return "input_statement";
        case NodeType::XRAPHICS_OBJECT_STATEMENT: return "xraphics_object_statement";
        case NodeType::CLASS_DECLARATION_STATEMENT: return "class_declaration_statement";
        case NodeType::LOOP_STATEMENT: return "loop_statement";
        case NodeType::DECLARATION: return "declaration";
        case NodeType::VARIABLE_DECLARATION: return "variable_declaration";
        case NodeType::FUNCTION: return "function";
        case NodeType::MODULE: return "module";
        case NodeType::PROGRAM: return "program";
        default: return "unknown";
    }
}

std::string Token::toString() const {
    std::string typeStr;
    switch (type) {
        case TokenType::END_OF_FILE: typeStr = "EOF"; break;
        case TokenType::IDENTIFIER: typeStr = "identifier"; break;
        case TokenType::NUMBER_LITERAL: typeStr = "number"; break;
        case TokenType::LONG_LITERAL: typeStr = "long"; break;
        case TokenType::STRING_LITERAL: typeStr = "string"; break;
        case TokenType::FLOAT_LITERAL: typeStr = "float"; break;
        case TokenType::KEYWORD_FN: typeStr = "fn"; break;
        case TokenType::KEYWORD_IF: typeStr = "if"; break;
        case TokenType::KEYWORD_ELSE: typeStr = "else"; break;
        case TokenType::KEYWORD_WHILE: typeStr = "while"; break;
        case TokenType::KEYWORD_BREAK: typeStr = "break"; break;
        case TokenType::KEYWORD_RETURN: typeStr = "return"; break;
        case TokenType::KEYWORD_TRUE: typeStr = "true"; break;
        case TokenType::KEYWORD_FALSE: typeStr = "false"; break;
        case TokenType::KEYWORD_PRINT: typeStr = "print"; break;
        case TokenType::KEYWORD_IMPORT: typeStr = "import"; break;
        case TokenType::KEYWORD_INT: typeStr = "int"; break;
        case TokenType::KEYWORD_LONG: typeStr = "long"; break;
        case TokenType::KEYWORD_FLOAT: typeStr = "float"; break;
        case TokenType::KEYWORD_BOOL: typeStr = "bool"; break;
        case TokenType::KEYWORD_STRING: typeStr = "string"; break;
        case TokenType::KEYWORD_FOR: typeStr = "for"; break;
        case TokenType::KEYWORD_WINDOW: typeStr = "window"; break;
        case TokenType::KEYWORD_INPUT: typeStr = "input"; break;
        case TokenType::PUNCTUATOR_LPAREN: typeStr = "("; break;
        case TokenType::PUNCTUATOR_RPAREN: typeStr = ")"; break;
        case TokenType::PUNCTUATOR_LBRACE: typeStr = "{"; break;
        case TokenType::PUNCTUATOR_RBRACE: typeStr = "}"; break;
        case TokenType::PUNCTUATOR_LBRACKET: typeStr = "["; break;
        case TokenType::PUNCTUATOR_RBRACKET: typeStr = "]"; break;
        case TokenType::PUNCTUATOR_SEMICOLON: typeStr = ";"; break;
        case TokenType::PUNCTUATOR_COMMA: typeStr = ","; break;
        case TokenType::PUNCTUATOR_PLUS: typeStr = "+"; break;
        case TokenType::PUNCTUATOR_MINUS: typeStr = "-"; break;
        case TokenType::PUNCTUATOR_STAR: typeStr = "*"; break;
        case TokenType::PUNCTUATOR_SLASH: typeStr = "/"; break;
        case TokenType::PUNCTUATOR_PERCENT: typeStr = "%"; break;
        case TokenType::PUNCTUATOR_EQUAL: typeStr = "="; break;
        case TokenType::PUNCTUATOR_EQUAL_EQUAL: typeStr = "=="; break;
        case TokenType::PUNCTUATOR_EXCLAIM: typeStr = "!"; break;
        case TokenType::PUNCTUATOR_EXCLAIM_EQUAL: typeStr = "!="; break;
        case TokenType::PUNCTUATOR_LESS: typeStr = "<"; break;
        case TokenType::PUNCTUATOR_LESS_EQUAL: typeStr = "<="; break;
        case TokenType::PUNCTUATOR_GREATER: typeStr = ">"; break;
        case TokenType::PUNCTUATOR_GREATER_EQUAL: typeStr = ">="; break;
        case TokenType::PUNCTUATOR_AND: typeStr = "&&"; break;
        case TokenType::PUNCTUATOR_OR: typeStr = "||"; break;
        case TokenType::PUNCTUATOR_HASH: typeStr = "#"; break;
        case TokenType::PUNCTUATOR_DOLLAR: typeStr = "$"; break;
        case TokenType::PUNCTUATOR_COLON: typeStr = ":"; break;
        case TokenType::PUNCTUATOR_DOT_DOT_DOT: typeStr = "..."; break;
        default: typeStr = "unknown"; break;
    }
    return typeStr + " " + text + " (" + std::to_string(location.line) + ":" + std::to_string(location.column) + ")";
}

}
