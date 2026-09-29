#ifndef XFAWA_TYPES_H
#define XFAWA_TYPES_H

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <cstdint>
#include <optional>

namespace xfawa {

enum class NodeType {
    NONE,
    
    EXPRESSION,
    NUMBER_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    BOOLEAN_LITERAL,
    VARIABLE_EXPRESSION,
    BINARY_OP,
    UNARY_OP,
    CALL_EXPRESSION,
    ARRAY_RANGE_EXPRESSION,
    // EXP `valuable`: first-class uncompiled code. These sit inside the
    // EXPRESSION..ARRAY_RANGE_EXPRESSION span so isExpression() stays true.
    VALUABLE_FRAGMENT_EXPRESSION,
    VALUABLE_CALL_EXPRESSION,
    VALUABLE_INJECT_EXPRESSION,
    ARRAY_LITERAL,
    ARRAY_INDEX_EXPRESSION,
    O_LITERAL_EXPRESSION,
    PARADOX_EXPRESSION,
    GHOST_EXPRESSION,
    
    STATEMENT,
    PRINT_STATEMENT,
    EXPRESSION_STATEMENT,
    ASSIGNMENT_STATEMENT,
    BREAK_STATEMENT,
    RETURN_STATEMENT,
    BLOCK_STATEMENT,
    WHILE_STATEMENT,
    IF_STATEMENT,
    IMPORT_STATEMENT,
    FUNCTION_DECLARATION,
    FOR_IN_STATEMENT,
    TYPED_ASSIGNMENT_STATEMENT,
    WINDOW_STATEMENT,
    BUTTON_STATEMENT,
    TEXT_STATEMENT,
    BOX_STATEMENT,
    INPUT_STATEMENT,
    XRAPHICS_OBJECT_STATEMENT,
    CLASS_DECLARATION_STATEMENT,
    LOOP_STATEMENT,
    BOOM_STATEMENT,
    BSOD_STATEMENT,
    BELIEVE_STATEMENT,
    LIE_STATEMENT,
    UN_STATEMENT,
    IGNORE_STATEMENT,
    DO_STATEMENT,
    PLEASE_STATEMENT,
    PLEASE_NOTICE_STATEMENT,
    SHUTUP_STATEMENT,
    ELLIPSIS_STATEMENT,
    WRATH_STATEMENT,
    PARADOX_STATEMENT,
    DEJA_STATEMENT,
    FATE_STATEMENT,
    ENVY_STATEMENT,
    TRY_EXPECT_STATEMENT,
    SORRY_STATEMENT,
    SLEEP_STATEMENT,
    COME_STATEMENT,
    PINOCCHIO_STATEMENT,
    DUAL_STATEMENT,
    INDEXED_ASSIGNMENT_STATEMENT,
    DISPOSABLE_STATEMENT,
    INTEREST_STATEMENT,
    KILL_STATEMENT,
    CENSER_STATEMENT,
    NOCLIP_STATEMENT,
    SHUFFLEBACK_STATEMENT,
    WRONG_STATEMENT,
    // EXP `zombie x`: infect every arithmetic operand with x's value.
    ZOMBIE_STATEMENT,
    // EXP `valuable`: `A B { ... }` splices code values into one fragment.
    VALUABLE_USE_STATEMENT,
    // EXP `deny x = 41`: keep the value, refuse to acknowledge the fact.
    DENY_STATEMENT,
    // EXP `regret "1+1=3"` / `regret all`: take back what was believed.
    REGRET_STATEMENT,
    // EXP `doubt x`: break the lie that is currently covering x.
    DOUBT_STATEMENT,
    // EXP `env 类型 参数名 { f, g }`: f and g silently carry one extra value.
    ENV_BLOCK_STATEMENT,
    // EXP `env 发出者 = 承受者`: re-point the carried value.
    ENV_ASSIGN_STATEMENT,
    // EXP `a <->[K] b`: one step of a reversible reaction toward b/a = K.
    REACTION_STATEMENT,
    // EXP structs: `type 名字 { 类型 字段, ... }` at module level.
    STRUCT_DECLARATION,
    // EXP `变量 = { 值, ... }`: create a struct value.
    STRUCT_CREATION_STATEMENT,
    // EXP member access: `expr.字段` (the carried object's fields).
    MEMBER_EXPRESSION,
    // EXP member assignment: `expr.字段 op= 值`.
    MEMBER_ASSIGNMENT_STATEMENT,

    VALUE_EXPRESSION,

    DECLARATION,
    VARIABLE_DECLARATION,

    FUNCTION,
    MODULE,
    PROGRAM
};

enum class VarType {
    UNKNOWN,
    INT,
    LONG,
    FLOAT,
    BOOL,
    STRING,
    // EXP structs: a named struct value (`type 角色 { int 行动点, int 血量 }`).
    // The struct's name lives beside the type map (localStructOf / callArgStructs).
    STRUCT,
    ARRAY_INT,
    ARRAY_LONG,
    ARRAY_FLOAT,
    ARRAY_BOOL,
    ARRAY_STRING
};

// EXP compound assignment: the operator of `x -= 1` / `a.b += v`.
enum class AssignOp {
    EQ,
    PLUS_EQ,
    MINUS_EQ,
    STAR_EQ,
    SLASH_EQ,
    PERCENT_EQ
};

std::string varTypeToString(VarType type);

std::string nodeTypeToString(NodeType type);

struct SourceLocation {
    int line;
    int column;
    int length;
    
    SourceLocation(int l = 1, int c = 1, int len = 0) : line(l), column(c), length(len) {}
    
    std::string toString() const {
        return "line " + std::to_string(line) + ", column " + std::to_string(column);
    }
    
    bool operator==(const SourceLocation& other) const {
        return line == other.line && column == other.column;
    }
    
    bool operator<(const SourceLocation& other) const {
        if (line != other.line) return line < other.line;
        return column < other.column;
    }
};

class ASTNode {
public:
    NodeType nodeType;
    SourceLocation location;
    
    ASTNode(NodeType nt = NodeType::NONE, const SourceLocation& loc = SourceLocation()) 
        : nodeType(nt), location(loc) {}
    virtual ~ASTNode() = default;
    virtual std::string toString() const = 0;
    
    NodeType getNodeType() const { return nodeType; }
    bool isExpression() const { 
        return nodeType >= NodeType::EXPRESSION && nodeType <= NodeType::ARRAY_RANGE_EXPRESSION; 
    }
    bool isStatement() const { 
        return nodeType >= NodeType::STATEMENT && nodeType <= NodeType::CLASS_DECLARATION_STATEMENT; 
    }
};

class Expression : public ASTNode {
public:
    Expression(NodeType nt = NodeType::EXPRESSION, const SourceLocation& loc = SourceLocation()) 
        : ASTNode(nt, loc) {}
    virtual ~Expression() = default;
};

class Statement : public ASTNode {
public:
    Statement(NodeType nt = NodeType::STATEMENT, const SourceLocation& loc = SourceLocation()) 
        : ASTNode(nt, loc) {}
    virtual ~Statement() = default;
};

enum class TokenType {
    END_OF_FILE,
    
    IDENTIFIER,
    NUMBER_LITERAL,
    LONG_LITERAL,
    STRING_LITERAL,
    FLOAT_LITERAL,
    COLOR_LITERAL,
    O_LITERAL,
    
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
    KEYWORD_PERCENT_IMPORT,
    KEYWORD_INT,
    KEYWORD_LONG,
    KEYWORD_FLOAT,
    KEYWORD_BOOL,
    KEYWORD_STRING,
    KEYWORD_FOR,
    KEYWORD_WINDOW,
    KEYWORD_INPUT,
    KEYWORD_AND,
    KEYWORD_OR,
    KEYWORD_CLASS,
    KEYWORD_LOOP,
    KEYWORD_BOOM,
    KEYWORD_BSOD,
    KEYWORD_BELIEVE,
    KEYWORD_LIE,
    KEYWORD_UN,
    KEYWORD_IGNORE,
    KEYWORD_DO,
    KEYWORD_PLEASE,
    KEYWORD_SHUTUP,
    KEYWORD_WRATH,
    KEYWORD_PARADOX,
    KEYWORD_DEJA,
    KEYWORD_FATE,
    KEYWORD_ENVY,
    KEYWORD_TRY,
    KEYWORD_EXPECT,
    KEYWORD_SORRY,
    KEYWORD_SLEEP,
    KEYWORD_COME,
    KEYWORD_DRIFT,
    KEYWORD_PINOCCHIO,
    KEYWORD_DUAL,
    KEYWORD_DISPOSABLE,
    // EXP `interest`: `¥` (simple interest) prefix token. `$` (compound
    // interest) reuses PUNCTUATOR_DOLLAR.
    KEYWORD_INTEREST_SIMPLE,
    KEYWORD_KILL,
    KEYWORD_CENSER,
    KEYWORD_VALUE,
    KEYWORD_NOCLIP,
    KEYWORD_SHUFFLEBACK,
    KEYWORD_WRONG,
    KEYWORD_VALUABLE,
    KEYWORD_CALL,
    KEYWORD_INJECT,
    KEYWORD_ZOMBIE,
    // EXP `deny`: the value stays, the program refuses to acknowledge it.
    KEYWORD_DENY,
    // EXP `regret` / `doubt`: take back a `believe` rule / break a `lie`.
    KEYWORD_REGRET,
    KEYWORD_DOUBT,
    // EXP `env`: a set of functions that silently carry one extra value.
    KEYWORD_ENV,
    // EXP structs: module-level `type 名字 { 类型 字段, ... }` declaration.
    KEYWORD_TYPE,

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
    PUNCTUATOR_BANG_QUESTION,
    PUNCTUATOR_LESS,
    PUNCTUATOR_LESS_EQUAL,
    PUNCTUATOR_GREATER,
    PUNCTUATOR_GREATER_EQUAL,
    
    PUNCTUATOR_AND,
    PUNCTUATOR_OR,
    
    PUNCTUATOR_HASH,
    PUNCTUATOR_DOLLAR,
    PUNCTUATOR_COLON,
    PUNCTUATOR_DOT,
    // EXP compound assignment: `-=` `+=` `*=` `/=` `%=` (submission's `env` uses them).
    PUNCTUATOR_MINUS_EQUAL,
    PUNCTUATOR_PLUS_EQUAL,
    PUNCTUATOR_STAR_EQUAL,
    PUNCTUATOR_SLASH_EQUAL,
    PUNCTUATOR_PERCENT_EQUAL,
    PUNCTUATOR_DOT_DOT_DOT,
    PUNCTUATOR_FU_K,
    // EXP `<->[K]`: reversible reaction between two numeric variables.
    PUNCTUATOR_REACTION,

    COMMENT,
    WHITESPACE
};

struct Token {
    TokenType type;
    std::string text;
    SourceLocation location;
    
    Token(TokenType t = TokenType::END_OF_FILE, const std::string& txt = "", 
          const SourceLocation& loc = SourceLocation()) 
        : type(t), text(txt), location(loc) {}
    
    bool is(TokenType t) const { return type == t; }
    
    bool isOneOf(TokenType t1, TokenType t2) const { return type == t1 || type == t2; }
    
    template<typename... Args>
    bool isOneOf(TokenType t1, TokenType t2, Args... args) const {
        return type == t1 || isOneOf(t2, args...);
    }
    
    std::string toString() const;
};

// Comment metadata collected by the lexer (not part of the token stream).
// Used by the EXP-001 "executable comments" experiment so the compiler
// can observe comments without polluting the normal token flow.
struct CommentInfo {
    std::string text;   // raw comment text (without the leading `//` or `/* */`)
    int line;           // 1‑based line where the comment starts
    int column;         // 1‑based column where the comment content starts
};

enum class BinaryOpType {
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,
    AND,
    OR,
    BANG_QUESTION,
    FU_K,
    NONE
};

enum class UnaryOpType {
    NEGATE,
    NOT,
    NONE
};

enum class ImportType {
    MOD,
    FILE,
    XFW
};

struct ImportStatement : public Statement {
    std::string name;
    std::string path;
    ImportType type;
    
    ImportStatement(const std::string& n, const std::string& p, ImportType t, const SourceLocation& loc)
        : Statement(NodeType::IMPORT_STATEMENT, loc), name(n), path(p), type(t) {}
    
    std::string toString() const override {
        std::string typeStr;
        if (type == ImportType::MOD) typeStr = "mod";
        else if (type == ImportType::XFW) typeStr = "xfw";
        else typeStr = "file";
        return "%import \"" + name + "\" (" + typeStr + ")";
    }
};

}

#endif
