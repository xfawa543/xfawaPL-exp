#ifndef XFAWA_AST_H
#define XFAWA_AST_H

#include "xfawa_types.h"
#include <memory>
#include <vector>
#include <string>

namespace xfawa {

class StringLiteral : public Expression {
public:
    std::string value;
    
    StringLiteral(const std::string& val, const SourceLocation& loc = SourceLocation()) 
        : Expression(NodeType::STRING_LITERAL, loc), value(val) {}
    
    std::string toString() const override {
        return "\"" + value + "\"";
    }
};

class NumberLiteral : public Expression {
public:
    int64_t value;
    
    NumberLiteral(int64_t val, const SourceLocation& loc = SourceLocation()) 
        : Expression(NodeType::NUMBER_LITERAL, loc), value(val) {}
    
    std::string toString() const override {
        return std::to_string(value);
    }
};

class FloatLiteral : public Expression {
public:
    double value;
    
    FloatLiteral(double val, const SourceLocation& loc = SourceLocation()) 
        : Expression(NodeType::FLOAT_LITERAL, loc), value(val) {}
    
    std::string toString() const override {
        return std::to_string(value);
    }
};

class BooleanLiteral : public Expression {
public:
    bool value;
    
    
    BooleanLiteral(bool val, const SourceLocation& loc = SourceLocation()) 
        : Expression(NodeType::BOOLEAN_LITERAL, loc), value(val) {}
    
    std::string toString() const override {
        return value ? "true" : "false";
    }
};

// EXP: o-literal (experimental). e.g. o, oo, ooo, 1o, 15o
// raw stores the original text (digit prefix + o run, digits may be empty for
// a pure o sequence). The numerical value is computed in codegen:
//   - pure 'o' run: 10^n
//   - digits + 'o' run: digits * 10^n
// In addition, o-literal + o-literal merges the o counts instead of adding
// normally (e.g. `1o + 1o == 100`).
class OLiteralExpression : public Expression {
public:
    std::string raw;  // the original text, e.g. "oo" or "15o"

    OLiteralExpression(const std::string& rawText, const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::O_LITERAL_EXPRESSION, loc), raw(rawText) {}

    std::string toString() const override {
        return raw;
    }
};

class VariableExpression : public Expression {
public:
    std::string name;
    
    VariableExpression(const std::string& n, const SourceLocation& loc = SourceLocation()) 
        : Expression(NodeType::VARIABLE_EXPRESSION, loc), name(n) {}
    
    std::string toString() const override {
        return name;
    }
};

// EXP `paradox`: a value that has been destroyed by its own causal chain.
// Produced by the paradox rewrite pass when a variable's causal root is broken.
// Deterministic semantics: evaluating it yields integer 0, but a *direct* print
// of a paradox expression prints "PARADOX".
class ParadoxExpression : public Expression {
public:
    ParadoxExpression(const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::PARADOX_EXPRESSION, loc) {}

    std::string toString() const override { return "PARADOX"; }
};

// EXP `paradox` stage 2 (`幽灵论`): a variable whose causal origin has been
// destroyed ("the past changed") but whose stored value survives the failed
// re-birth. Evaluating it yields the variable's current value (never 0); a
// *direct* print of a ghost expression appends "#" to the shown value. It is
// produced by the paradox rewrite pass for reads of variables in the ghost set.
class GhostExpression : public Expression {
public:
    std::string name;

    GhostExpression(const std::string& n, const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::GHOST_EXPRESSION, loc), name(n) {}

    std::string toString() const override { return "GHOST"; }
};

class BinaryOp : public Expression {
public:
    BinaryOpType op;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;
    
    BinaryOp(BinaryOpType o, std::unique_ptr<Expression> l, std::unique_ptr<Expression> r,
             const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::BINARY_OP, loc), op(o), left(std::move(l)), right(std::move(r)) {}
    
    std::string toString() const override {
        std::string result = left->toString();
        switch (op) {
            case BinaryOpType::ADD: result += " + "; break;
            case BinaryOpType::SUB: result += " - "; break;
            case BinaryOpType::MUL: result += " * "; break;
            case BinaryOpType::DIV: result += " / "; break;
            case BinaryOpType::MOD: result += " % "; break;
            case BinaryOpType::EQUAL: result += " == "; break;
            case BinaryOpType::NOT_EQUAL: result += " != "; break;
            case BinaryOpType::LESS: result += " < "; break;
            case BinaryOpType::LESS_EQUAL: result += " <= "; break;
            case BinaryOpType::GREATER: result += " > "; break;
            case BinaryOpType::GREATER_EQUAL: result += " >= "; break;
            case BinaryOpType::AND: result += " && "; break;
            case BinaryOpType::OR: result += " || "; break;
            default: result += " ? "; break;
        }
        result += right->toString();
        return result;
    }
};

class UnaryOp : public Expression {
public:
    UnaryOpType op;
    std::unique_ptr<Expression> expr;
    
    UnaryOp(UnaryOpType o, std::unique_ptr<Expression> e,
            const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::UNARY_OP, loc), op(o), expr(std::move(e)) {}
    
    std::string toString() const override {
        std::string result;
        switch (op) {
            case UnaryOpType::NEGATE: result = "-"; break;
            case UnaryOpType::NOT: result = "!"; break;
            default: result = "?"; break;
        }
        result += "(" + expr->toString() + ")";
        return result;
    }
};

class CallExpression : public Expression {
public:
    std::string name;
    std::string ns;
    std::vector<std::unique_ptr<Expression>> args;
    
    CallExpression(const std::string& n, std::vector<std::unique_ptr<Expression>> a,
                   const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::CALL_EXPRESSION, loc), name(n), ns(""), args(std::move(a)) {}
    
    CallExpression(const std::string& n, const std::string& ns_name, std::vector<std::unique_ptr<Expression>> a,
                   const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::CALL_EXPRESSION, loc), name(n), ns(ns_name), args(std::move(a)) {}
    
    std::string toString() const override {
        std::string result;
        if (!ns.empty()) {
            result = ns + ":" + name + "(";
        } else {
            result = name + "(";
        }
        for (size_t i = 0; i < args.size(); i++) {
            result += args[i]->toString();
            if (i < args.size() - 1) result += ", ";
        }
        result += ")";
        return result;
    }
};

enum class ArrayAccessType {
    SEQUENTIAL,
    RANDOM,
    RECIPROCAL
};

class ArrayRangeExpression : public Expression {
public:
    std::string accessType;
    std::unique_ptr<Expression> start;
    std::unique_ptr<Expression> end;
    std::unique_ptr<Expression> array;
    bool isSlice;
    
    ArrayRangeExpression(const std::string& access, std::unique_ptr<Expression> s, std::unique_ptr<Expression> e,
                          const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::ARRAY_RANGE_EXPRESSION, loc), accessType(access), start(std::move(s)), end(std::move(e)), isSlice(false) {}
    
    ArrayRangeExpression(std::unique_ptr<Expression> arr, std::unique_ptr<Expression> s, std::unique_ptr<Expression> e,
                          const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::ARRAY_RANGE_EXPRESSION, loc), start(std::move(s)), end(std::move(e)), array(std::move(arr)), isSlice(true) {}
    
    std::string toString() const override {
        if (isSlice && array) {
            return array->toString() + "[" + start->toString() + "..." + end->toString() + "]";
        }
        return accessType + "[" + start->toString() + "..." + end->toString() + "]";
    }
};

class ArrayLiteral : public Expression {
public:
    std::vector<std::unique_ptr<Expression>> elements;
    bool isRange;
    std::unique_ptr<Expression> rangeStart;
    std::unique_ptr<Expression> rangeEnd;
    
    ArrayLiteral(std::vector<std::unique_ptr<Expression>> elems, const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::ARRAY_LITERAL, loc), elements(std::move(elems)), isRange(false) {}
    
    ArrayLiteral(std::unique_ptr<Expression> start, std::unique_ptr<Expression> end, const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::ARRAY_LITERAL, loc), isRange(true), rangeStart(std::move(start)), rangeEnd(std::move(end)) {}
    
    std::string toString() const override {
        if (isRange) {
            return "[" + rangeStart->toString() + "..." + rangeEnd->toString() + "]";
        }
        std::string result = "[";
        for (size_t i = 0; i < elements.size(); i++) {
            result += elements[i]->toString();
            if (i < elements.size() - 1) result += ", ";
        }
        result += "]";
        return result;
    }
};

class ArrayIndexExpression : public Expression {
public:
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    
    ArrayIndexExpression(std::unique_ptr<Expression> arr, std::unique_ptr<Expression> idx,
                          const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::ARRAY_INDEX_EXPRESSION, loc), array(std::move(arr)), index(std::move(idx)) {}
    
    std::string toString() const override {
        return array->toString() + "[" + index->toString() + "]";
    }
};

class VariableDeclaration {
public:
    std::string name;
    SourceLocation location;
    
    VariableDeclaration(const std::string& n, const SourceLocation& loc = SourceLocation()) 
        : name(n), location(loc) {}
};

class PrintStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    std::string outputTarget;
    // EXP `un`: when true, this single print bypasses an active `un print`.
    bool overridden = false;
    
    PrintStatement(std::unique_ptr<Expression> e, const SourceLocation& loc = SourceLocation()) 
        : Statement(NodeType::PRINT_STATEMENT, loc), expr(std::move(e)) {}
    
    std::string toString() const override {
        if (!outputTarget.empty()) {
            return "print(" + expr->toString() + ", " + outputTarget + ")";
        }
        return "print(" + expr->toString() + ")";
    }
};

class ExpressionStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    
    ExpressionStatement(std::unique_ptr<Expression> e, const SourceLocation& loc = SourceLocation()) 
        : Statement(NodeType::EXPRESSION_STATEMENT, loc), expr(std::move(e)) {}
    
    std::string toString() const override {
        return expr->toString();
    }
};

class AssignmentStatement : public Statement {
public:
    std::string name;
    std::unique_ptr<Expression> value;
    VarType declaredType;
    bool hasExplicitType;
    bool isReassignment;
    
    AssignmentStatement(const std::string& n, std::unique_ptr<Expression> v,
                       const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::ASSIGNMENT_STATEMENT, loc), name(n), value(std::move(v)), 
          declaredType(VarType::UNKNOWN), hasExplicitType(false), isReassignment(false) {}
    
    AssignmentStatement(const std::string& n, std::unique_ptr<Expression> v, VarType t,
                       const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::ASSIGNMENT_STATEMENT, loc), name(n), value(std::move(v)), 
          declaredType(t), hasExplicitType(true), isReassignment(false) {}
    
    std::string toString() const override {
        if (hasExplicitType) {
            return varTypeToString(declaredType) + " " + name + " = " + value->toString();
        }
        return name + " = " + value->toString();
    }
};

class BreakStatement : public Statement {
public:
    BreakStatement(const SourceLocation& loc = SourceLocation()) 
        : Statement(NodeType::BREAK_STATEMENT, loc) {}
    
    std::string toString() const override {
        return "break";
    }
};

// EXP: `boom` — prints "BOOM!!" and terminates the program normally.
class BoomStatement : public Statement {
public:
    BoomStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::BOOM_STATEMENT, loc) {}

    std::string toString() const override {
        return "boom";
    }
};

// EXP: `bsod` — shows a fake blue screen, delays ~1s, then resumes.
class BsodStatement : public Statement {
public:
    BsodStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::BSOD_STATEMENT, loc) {}

    std::string toString() const override {
        return "bsod";
    }
};

// EXP `believe`: registers a belief like `believe "2 + 2 = 5"`.
class BelieveStatement : public Statement {
public:
    std::string raw; // the raw proposition text (e.g. "2 + 2 = 5")

    BelieveStatement(const std::string& r, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::BELIEVE_STATEMENT, loc), raw(r) {}

    std::string toString() const override {
        return "believe \"" + raw + "\"";
    }
};

// EXP `lie`: a block-scoped lie. `lie name = value { body }` makes all reads of
// `name` inside `body` observe `value` (the real stored value stays unchanged).
// The lie automatically expires when the block's `}` is reached.
class BlockStatement;

class LieStatement : public Statement {
public:
    std::string name;
    std::unique_ptr<Expression> value;
    std::unique_ptr<BlockStatement> body;

    LieStatement(const std::string& n, std::unique_ptr<Expression> v,
                 std::unique_ptr<BlockStatement> b,
                 const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::LIE_STATEMENT, loc), name(n), value(std::move(v)), body(std::move(b)) {}

    std::string toString() const override {
        return "lie " + name + " = " + (value ? value->toString() : "") + " { ... }";
    }
};

// EXP `un`: disables a statement keyword (e.g. `un print`) until end of block.
class UnStatement : public Statement {
public:
    TokenType target; // which keyword is disabled (e.g. KEYWORD_PRINT)

    UnStatement(TokenType t, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::UN_STATEMENT, loc), target(t) {}

    std::string toString() const override {
        return "un";
    }
};

class ReturnStatement : public Statement {
public:
    std::unique_ptr<Expression> value;
    
    ReturnStatement(std::unique_ptr<Expression> v, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::RETURN_STATEMENT, loc), value(std::move(v)) {}
    
    std::string toString() const override {
        return "return " + value->toString();
    }
};

// EXP: statement-modifier keywords. syntax: `<keyword>.<statement>`
class IgnoreStatement : public Statement {
public:
    std::unique_ptr<Statement> inner;
    IgnoreStatement(std::unique_ptr<Statement> stmt, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::IGNORE_STATEMENT, loc), inner(std::move(stmt)) {}
    std::string toString() const override { return "ignore." + (inner ? inner->toString() : ""); }
};

class DoStatement : public Statement {
public:
    std::unique_ptr<Statement> inner;
    DoStatement(std::unique_ptr<Statement> stmt, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::DO_STATEMENT, loc), inner(std::move(stmt)) {}
    std::string toString() const override { return "do." + (inner ? inner->toString() : ""); }
};

class PleaseStatement : public Statement {
public:
    std::unique_ptr<Statement> inner;
    PleaseStatement(std::unique_ptr<Statement> stmt, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::PLEASE_STATEMENT, loc), inner(std::move(stmt)) {}
    std::string toString() const override { return "please." + (inner ? inner->toString() : ""); }
};

// EXP `please` (bare keyword): a SEPARATE keyword from `please.stmt`. It has no
// runtime effect at all (no "thank you!" print). It only matters while the
// compiler is red-hot (rage >= 3): it is the statement that satisfies the
// "every 5 code lines must contain a please" rule and cools the compiler by a
// fixed 1. Outside red-hot it does nothing (see rage.md).
class PleaseNoticeStatement : public Statement {
public:
    PleaseNoticeStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::PLEASE_NOTICE_STATEMENT, loc) {}
    std::string toString() const override { return "please"; }
};

// EXP "shutup": threatens the compiler into suppressing warnings.
class ShutupStatement : public Statement {
public:
    ShutupStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::SHUTUP_STATEMENT, loc) {}
    std::string toString() const override { return "shutup"; }
};

// EXP "..." statement: dispatch a random allowed builtin/user-defined function.
class EllipsisStatement : public Statement {
public:
    EllipsisStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::ELLIPSIS_STATEMENT, loc) {}
    std::string toString() const override { return "..."; }
};

// EXP `sleep`: pause for a number of seconds, e.g. `sleep(2);`.
class SleepStatement : public Statement {
public:
    std::unique_ptr<Expression> expr; // duration in seconds
    // EXP `un`: when true, this single sleep bypasses an active `un sleep`.
    bool overridden = false;

    SleepStatement(std::unique_ptr<Expression> e, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::SLEEP_STATEMENT, loc), expr(std::move(e)) {}

    std::string toString() const override {
        return "sleep(" + (expr ? expr->toString() : "") + ")";
    }
};

// EXP `come`: reverse goto. `come 20` declares that whenever the statement on
// physical source line 20 executes, control jumps back to the come statement's
// own position (the statement right after `come` re-runs). `come if(cond) 20`
// evaluates `cond` AT line 20: true -> jump back, false -> continue normally.
// The come and its target must be in the same function; invalid or
// uncrosable targets are reported as compile errors by the LLVM backend.
class ComeStatement : public Statement {
public:
    int targetLine;                            // physical source line to react to
    std::unique_ptr<Expression> condition;     // optional `if(cond)`; evaluated at the target line

    ComeStatement(int target, std::unique_ptr<Expression> cond, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::COME_STATEMENT, loc), targetLine(target), condition(std::move(cond)) {}

    std::string toString() const override {
        if (condition) {
            return "come if(" + condition->toString() + ") " + std::to_string(targetLine);
        }
        return "come " + std::to_string(targetLine);
    }
};

// EXP `wrath`: retroactively rewrite the history of a variable. `wrath x = v`
// assigns `v` to `x`, then re-evaluates every later variable that (transitively)
// depended on `x`, so future reads of those dependents see the new value. Already
// emitted side effects (prints) are NOT rewritten — only stored state.
class WrathStatement : public Statement {
public:
    std::string name;
    std::unique_ptr<Expression> value;

    WrathStatement(const std::string& n, std::unique_ptr<Expression> v,
                   const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::WRATH_STATEMENT, loc), name(n), value(std::move(v)) {}

    std::string toString() const override {
        return "wrath " + name + " = " + (value ? value->toString() : "");
    }
};

// EXP `paradox`: break a variable's own causal chain (grandfather paradox).
// `paradox x` marks `x` and every variable that depended on it as PARADOX,
// because the reason they exist has been destroyed after they were computed.
class ParadoxStatement : public Statement {
public:
    std::string name;

    ParadoxStatement(const std::string& n, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::PARADOX_STATEMENT, loc), name(n) {}

    std::string toString() const override {
        return "paradox " + name;
    }
};

class BlockStatement : public Statement {
public:
    std::vector<std::unique_ptr<Statement>> statements;

    BlockStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::BLOCK_STATEMENT, loc) {}
    BlockStatement(std::vector<std::unique_ptr<Statement>> stmts, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::BLOCK_STATEMENT, loc), statements(std::move(stmts)) {}

    void addStatement(std::unique_ptr<Statement> stmt) {
        statements.push_back(std::move(stmt));
    }

    std::string toString() const override {
        std::string result = "{\n";
        for (const auto& stmt : statements) {
            result += "  " + stmt->toString() + "\n";
        }
        result += "}";
        return result;
    }
};

// EXP `try...expect`: a compile-time-only error CHECK zone. The try block never
// produces runtime code; it only lets the compiler check a batch of statements
// for catchable errors (parse-stage typos or semantic errors) and, when one is
// intercepted, the expect block runs instead.
//   - The Parser may set `parseFailed` when a parse-stage error (e.g. a keyword
//     typo like `prin`) is found inside the try block and could not be
//     represented as a normal statement.
//   - The SemanticAnalyzer sets `trySucceeded`:
//       false -> a catchable error was intercepted, emit expectBlock only;
//       true  -> the try checked out clean, emit NOTHING (try never executes).
class TryExpectStatement : public Statement {
public:
    std::unique_ptr<BlockStatement> tryBlock;
    std::unique_ptr<BlockStatement> expectBlock;
    // true  -> the try block is clean; neither block runs.
    // false -> a catchable error was intercepted; the expect block runs.
    bool trySucceeded = true;
    // Set by the Parser when a parse-stage error occurred inside the try block.
    bool parseFailed = false;

    TryExpectStatement(std::unique_ptr<BlockStatement> t,
                       std::unique_ptr<BlockStatement> e,
                       const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::TRY_EXPECT_STATEMENT, loc),
          tryBlock(std::move(t)), expectBlock(std::move(e)) {}

    std::string toString() const override {
        return "try " + (tryBlock ? tryBlock->toString() : "{}") +
               " expect " + (expectBlock ? expectBlock->toString() : "{}");
    }
};

// EXP `sorry`: tell the compiler you're sorry -> rage decreases by a RANDOM
// amount in [0, rage] (min 0). It never skips errors, never silences warnings,
// and never changes program semantics; it only affects the compiler's
// (entertainment-only) rage meter.
class SorryStatement : public Statement {
public:
    SorryStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::SORRY_STATEMENT, loc) {}

    std::string toString() const override {
        return "sorry";
    }
};

// Loop statement: game loop that runs its body every frame (api.txt)
class LoopStatement : public Statement {
public:
    std::vector<std::unique_ptr<Statement>> body;

    LoopStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::LOOP_STATEMENT, loc) {}

    std::string toString() const override {
        std::string result = "loop {\n";
        for (const auto& stmt : body) {
            result += "  " + stmt->toString() + "\n";
        }
        result += "}";
        return result;
    }
};

class WhileStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> body;
    
    WhileStatement(std::unique_ptr<Expression> c, std::unique_ptr<Statement> b,
                   const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::WHILE_STATEMENT, loc), condition(std::move(c)), body(std::move(b)) {}
    
    std::string toString() const override {
        return "while (" + condition->toString() + ") " + body->toString();
    }
};

class IfStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> thenBranch;
    std::unique_ptr<Statement> elseBranch;
    std::vector<std::pair<std::unique_ptr<Expression>, std::unique_ptr<Statement>>> elseIfBranches;
    
    IfStatement(std::unique_ptr<Expression> c, std::unique_ptr<Statement> t,
                std::unique_ptr<Statement> e,
                const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::IF_STATEMENT, loc), condition(std::move(c)), thenBranch(std::move(t)), elseBranch(std::move(e)) {}
    
    std::string toString() const override {
        std::string result = "if " + condition->toString() + " " + thenBranch->toString();
        for (const auto& elseIf : elseIfBranches) {
            result += " else if " + elseIf.first->toString() + " " + elseIf.second->toString();
        }
        if (elseBranch) {
            result += " else " + elseBranch->toString();
        }
        return result;
    }
};

class ForInStatement : public Statement {
public:
    std::string varName;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Statement> body;
    
    ForInStatement(const std::string& var, std::unique_ptr<Expression> iter, std::unique_ptr<Statement> b,
                   const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::FOR_IN_STATEMENT, loc), varName(var), iterable(std::move(iter)), body(std::move(b)) {}
    
    std::string toString() const override {
        return "for " + varName + " in " + iterable->toString() + " " + body->toString();
    }
};

// Color literal expression, e.g. #FFFFFF
class ColorLiteral : public Expression {
public:
    std::string value;  // hex string without '#', e.g. "FFFFFF"

    ColorLiteral(const std::string& val, const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::STRING_LITERAL, loc), value(val) {}

    std::string toString() const override {
        return "#" + value;
    }
};

// Tuple expression, e.g. (0, 0, 0) for position/size
class TupleExpression : public Expression {
public:
    std::vector<std::unique_ptr<Expression>> elements;

    TupleExpression(std::vector<std::unique_ptr<Expression>> elems,
                    const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::ARRAY_LITERAL, loc), elements(std::move(elems)) {}

    std::string toString() const override {
        std::string result = "(";
        for (size_t i = 0; i < elements.size(); i++) {
            if (i > 0) result += ", ";
            result += elements[i] ? elements[i]->toString() : "null";
        }
        result += ")";
        return result;
    }
};

// A single named parameter of a Xraphics object, e.g. position=(0,0,0)
struct XraphicsParam {
    std::string name;
    std::unique_ptr<Expression> value;
    XraphicsParam() = default;
    XraphicsParam(const std::string& n, std::unique_ptr<Expression> v)
        : name(n), value(std::move(v)) {}
};

// Xraphics object definition statement, e.g. block = x3d.box(...)
class XraphicsObjectStatement : public Statement {
public:
    std::string objectName;      // e.g. "block"
    std::string library;          // "x3d" or "x2d"
    std::string preset;           // e.g. "box", "sphere", "rect", "circle"
    std::vector<XraphicsParam> params;

    XraphicsObjectStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::XRAPHICS_OBJECT_STATEMENT, loc) {}

    std::string toString() const override {
        std::string result = objectName + " = " + library + "." + preset + "(\n";
        for (const auto& p : params) {
            result += "    " + p.name + " = " + (p.value ? p.value->toString() : "null") + "\n";
        }
        result += "  )";
        return result;
    }
};

// Class declaration statement, e.g. class MyModel { obj = x3d.box(...) }
class ClassDeclarationStatement : public Statement {
public:
    std::string className;
    std::vector<std::unique_ptr<XraphicsObjectStatement>> objects;

    ClassDeclarationStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::CLASS_DECLARATION_STATEMENT, loc) {}

    std::string toString() const override {
        std::string result = "class " + className + " {\n";
        for (const auto& obj : objects) {
            result += "  " + obj->toString() + "\n";
        }
        result += "}";
        return result;
    }
};

class Function {
public:
    std::string name;
    std::string ns;
    std::string blockName;  // Alpha17: Block name for block.function syntax
    std::vector<std::unique_ptr<VariableDeclaration>> params;
    std::unique_ptr<BlockStatement> body;
    SourceLocation location;
    
    Function(const std::string& n, std::vector<std::unique_ptr<VariableDeclaration>> p,
             std::unique_ptr<BlockStatement> b, const SourceLocation& loc = SourceLocation())
        : name(n), ns(""), blockName(""), params(std::move(p)), body(std::move(b)), location(loc) {}
    
    Function(const std::string& n, const std::string& ns_name, std::vector<std::unique_ptr<VariableDeclaration>> p,
             std::unique_ptr<BlockStatement> b, const SourceLocation& loc = SourceLocation())
        : name(n), ns(ns_name), blockName(""), params(std::move(p)), body(std::move(b)), location(loc) {}
    
    std::string toString() const {
        std::string result = "fn ";
        if (!ns.empty()) {
            result += ns + ":";
        }
        result += name + "(";
        for (size_t i = 0; i < params.size(); i++) {
            result += params[i]->name;
            if (i < params.size() - 1) result += ", ";
        }
        result += ") " + body->toString();
        return result;
    }
};

class Module {
public:
    std::string name;
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::unique_ptr<ImportStatement>> imports;
    SourceLocation location;

    Module(const std::string& n, std::vector<std::unique_ptr<Function>> f,
           const SourceLocation& loc = SourceLocation())
        : name(n), functions(std::move(f)), location(loc) {}

    std::string toString() const {
        std::string result = "#" + name + " {\n";
        for (const auto& imp : imports) {
            result += "  " + imp->toString() + "\n";
        }
        for (const auto& func : functions) {
            result += "  " + func->toString() + "\n";
        }
        result += "}";
        return result;
    }
};

class ButtonStatement : public Statement {
public:
    int x = 20;
    int y = 20;
    int width = 120;
    int height = 36;
    std::string text = "button";
    std::vector<std::unique_ptr<Statement>> body;

    ButtonStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::BUTTON_STATEMENT, loc) {}

    std::string toString() const override {
        std::string result = "button {\n"
                             "    x: " + std::to_string(x) + "\n"
                             "    y: " + std::to_string(y) + "\n"
                             "    width: " + std::to_string(width) + "\n"
                             "    height: " + std::to_string(height) + "\n"
                             "    text: \"" + text + "\"\n";
        for (const auto& stmt : body) {
            result += "    " + stmt->toString() + "\n";
        }
        result += "  }";
        return result;
    }
};

class TextStatement : public Statement {
public:
    int x = 20;
    int y = 20;
    int width = 160;
    int height = 24;
    std::string text = "text";

    TextStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::TEXT_STATEMENT, loc) {}

    std::string toString() const override {
        return "text {\n"
               "    x: " + std::to_string(x) + "\n"
               "    y: " + std::to_string(y) + "\n"
               "    width: " + std::to_string(width) + "\n"
               "    height: " + std::to_string(height) + "\n"
               "    text: \"" + text + "\"\n"
               "  }";
    }
};

class BoxStatement : public Statement {
public:
    int x = 20;
    int y = 20;
    int width = 240;
    int height = 120;
    std::string id = "output";
    std::string text = "";

    BoxStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::BOX_STATEMENT, loc) {}

    std::string toString() const override {
        return "box {\n"
               "    id: " + id + "\n"
               "    x: " + std::to_string(x) + "\n"
               "    y: " + std::to_string(y) + "\n"
               "    width: " + std::to_string(width) + "\n"
               "    height: " + std::to_string(height) + "\n"
               "    text: \"" + text + "\"\n"
               "  }";
    }
};

class InputStatement : public Statement {
public:
    int x = 20;
    int y = 20;
    int width = 200;
    int height = 32;
    std::string id = "input";
    std::string varName = "";

    InputStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::INPUT_STATEMENT, loc) {}

    std::string toString() const override {
        return "input {\n"
               "    id: " + id + "\n"
               "    x: " + std::to_string(x) + "\n"
               "    y: " + std::to_string(y) + "\n"
               "    width: " + std::to_string(width) + "\n"
               "    height: " + std::to_string(height) + "\n"
               "    var: " + varName + "\n"
               "  }";
    }
};

class WindowStatement : public Statement {
public:
    int width = 800;
    int height = 600;
    std::string title = "xfawa";
    std::string color = "white";
    std::string style = "";
    std::vector<std::unique_ptr<ButtonStatement>> buttons;
    std::vector<std::unique_ptr<TextStatement>> texts;
    std::vector<std::unique_ptr<BoxStatement>> boxes;
    std::vector<std::unique_ptr<InputStatement>> inputs;
    std::vector<std::unique_ptr<ClassDeclarationStatement>> classes;
    std::vector<std::unique_ptr<LoopStatement>> loops;

    WindowStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::WINDOW_STATEMENT, loc) {}

    std::string toString() const override {
        std::string result = "window {\n"
               "  width: " + std::to_string(width) + "\n"
               "  height: " + std::to_string(height) + "\n"
               "  title: \"" + title + "\"\n"
               "  color: " + color + "\n";
        if (!style.empty()) {
            result += "  style: \"" + style + "\"\n";
        }
        for (const auto& button : buttons) {
            result += "  " + button->toString() + "\n";
        }
        for (const auto& textItem : texts) {
            result += "  " + textItem->toString() + "\n";
        }
        for (const auto& box : boxes) {
            result += "  " + box->toString() + "\n";
        }
        for (const auto& input : inputs) {
            result += "  " + input->toString() + "\n";
        }
        for (const auto& cls : classes) {
            result += "  " + cls->toString() + "\n";
        }
        result += "}";
        return result;
    }
};

class Program {
public:
    std::vector<std::unique_ptr<Module>> modules;
    std::vector<std::unique_ptr<ImportStatement>> imports;
    
    void addModule(std::unique_ptr<Module> mod) {
        modules.push_back(std::move(mod));
    }
    
    void addImport(std::unique_ptr<ImportStatement> imp) {
        imports.push_back(std::move(imp));
    }
    
    std::string toString() const {
        std::string result;
        for (const auto& imp : imports) {
            result += imp->toString() + "\n";
        }
        for (const auto& mod : modules) {
            result += mod->toString() + "\n";
        }
        return result;
    }
};

class FunctionDeclarationStatement : public Statement {
public:
    std::unique_ptr<Function> func;
    
    FunctionDeclarationStatement(std::unique_ptr<Function> f, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::FUNCTION_DECLARATION, loc), func(std::move(f)) {}
    
    std::string toString() const override {
        return func ? func->toString() : "fn <null>";
    }
};

}

#endif
