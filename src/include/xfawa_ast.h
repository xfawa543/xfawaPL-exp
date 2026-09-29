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
            case BinaryOpType::BANG_QUESTION: result += " ?! "; break;
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
    // EXP compound assignment: the operator of `x -= 1`; ASSIGN_EQ for plain `=`.
    AssignOp op = AssignOp::EQ;
    
    AssignmentStatement(const std::string& n, std::unique_ptr<Expression> v,
                       const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::ASSIGNMENT_STATEMENT, loc), name(n), value(std::move(v)), 
          declaredType(VarType::UNKNOWN), hasExplicitType(false), isReassignment(false) {}
    
    AssignmentStatement(const std::string& n, std::unique_ptr<Expression> v, VarType t,
                       const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::ASSIGNMENT_STATEMENT, loc), name(n), value(std::move(v)), 
          declaredType(t), hasExplicitType(true), isReassignment(false) {}
    
    std::string toString() const override {
        static const char* ops[] = {"=", "+=", "-=", "*=", "/=", "%="};
        std::string sign = std::string(" ") + ops[(int)op] + " ";
        if (hasExplicitType) {
            return varTypeToString(declaredType) + " " + name + sign + value->toString();
        }
        return name + sign + value->toString();
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

// EXP `deny`: the fact stays true in storage but the program stops acknowledging
// it. `deny answer = 41` makes every later `answer == 41` come out false while
// `print(answer)` still prints 41. The string form denies a `believe` rule:
// `deny "2 + 2 = 5"` takes the belief back, so `2 + 2` is 4 again.
class DenyStatement : public Statement {
public:
    std::string name;      // the denied variable (identifier form)
    int64_t value = 0;     // the denied value (identifier form)
    std::string raw;       // the raw proposition text (string form, believe)

    DenyStatement(const std::string& n, int64_t v, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::DENY_STATEMENT, loc), name(n), value(v) {}

    DenyStatement(const std::string& r, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::DENY_STATEMENT, loc), raw(r) {}

    std::string toString() const override {
        if (!raw.empty()) return "deny \"" + raw + "\"";
        return "deny " + name + " = " + std::to_string(value);
    }
};

// EXP `regret`: take back a belief. `regret "1+1=3"` removes that one rule,
// `regret all` removes every rule the program currently believes.
class RegretStatement : public Statement {
public:
    std::string raw;    // the raw proposition text
    bool all = false;   // `regret all`

    RegretStatement(const std::string& r, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::REGRET_STATEMENT, loc), raw(r) {}

    explicit RegretStatement(bool everything, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::REGRET_STATEMENT, loc), all(everything) {}

    std::string toString() const override {
        return all ? "regret all" : "regret \"" + raw + "\"";
    }
};

// EXP `doubt x`: inside a `lie` block, stop believing the lie that covers `x`.
// The rest of the block reads the real value again; a `doubt` on a variable
// that no lie is covering is a compile error. The lie pass sets `broken` when it
// really did break a lie, so codegen can tell the two cases apart.
class DoubtStatement : public Statement {
public:
    std::string name;
    bool broken = false;

    explicit DoubtStatement(const std::string& n, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::DOUBT_STATEMENT, loc), name(n) {}

    std::string toString() const override { return "doubt " + name; }
};

// EXP `env`: `env 类型 参数名 { f, g, h }` gives every listed function one extra
// parameter (`参数名`) that nobody has to pass: a call from one of these functions
// to another carries it along by itself.
class EnvBlockStatement : public Statement {
public:
    std::string typeName;                     // declared type, e.g. 角色
    std::string paramName;                    // the carried value's name, e.g. 发出者
    std::vector<std::string> functions;       // the functions that carry it
    // Set by the env pass when the block cannot be carried out. Kept as a
    // message rather than a diagnostic so it can fail the build at codegen,
    // where an error actually stops compilation.
    std::string error;

    EnvBlockStatement(const std::string& t, const std::string& p,
                      std::vector<std::string> fns, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::ENV_BLOCK_STATEMENT, loc), typeName(t), paramName(p),
          functions(std::move(fns)) {}

    std::string toString() const override {
        std::string s = "env " + typeName + " " + paramName + " { ";
        for (size_t i = 0; i < functions.size(); i++) {
            if (i) s += ", ";
            s += functions[i];
        }
        return s + " }";
    }
};

// EXP `env 发出者 = 承受者`: re-point the carried value at something else.
class EnvAssignStatement : public Statement {
public:
    std::string name;
    std::unique_ptr<Expression> value;

    EnvAssignStatement(const std::string& n, std::unique_ptr<Expression> v,
                       const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::ENV_ASSIGN_STATEMENT, loc), name(n), value(std::move(v)) {}

    std::string toString() const override {
        return "env " + name + " = " + (value ? value->toString() : "");
    }
};

// EXP `a <->[K] b`: one step of a reversible reaction. Each execution moves `a`
// and `b` toward the balance where b/a = K, keeping a + b constant; the closer
// it gets, the smaller the step, and at the balance it floats at random.
class ReactionStatement : public Statement {
public:
    std::string leftName;
    std::string rightName;
    int64_t k = 1;                 // the equilibrium ratio b/a

    ReactionStatement(const std::string& l, const std::string& r, int64_t ratio,
                      const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::REACTION_STATEMENT, loc), leftName(l), rightName(r), k(ratio) {}

    std::string toString() const override {
        return leftName + " <->[" + std::to_string(k) + "] " + rightName;
    }
};

// EXP structs: module-level `type 名字 { 类型 字段, ... }`. Fields are basic
// types only; the struct's values are created with `类型名 变量 = { 值, ... }`
// and every struct variable reads as a pointer, so member writes persist.
class StructDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::pair<std::string, VarType>> fields; // (fieldName, type) in order

    StructDeclaration(const std::string& n,
                      std::vector<std::pair<std::string, VarType>> f,
                      const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::STRUCT_DECLARATION, loc), name(n), fields(std::move(f)) {}

    std::string toString() const override {
        std::string s = "type " + name + " { ";
        for (size_t i = 0; i < fields.size(); i++) {
            if (i) s += ", ";
            s += varTypeToString(fields[i].second) + " " + fields[i].first;
        }
        return s + " }";
    }
};

// EXP member access: `expr.字段`. The base must be a struct variable (read as a
// pointer); the result is the field's value.
class MemberExpression : public Expression {
public:
    std::unique_ptr<Expression> base;
    std::string field;

    MemberExpression(std::unique_ptr<Expression> b, const std::string& f,
                     const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::MEMBER_EXPRESSION, loc), base(std::move(b)), field(f) {}

    std::string toString() const override {
        return (base ? base->toString() : "?") + "." + field;
    }
};

// EXP member assignment: `expr.字段 op= 值` (op is ASSIGN_EQ for plain `=`).
class MemberAssignmentStatement : public Statement {
public:
    std::unique_ptr<Expression> base;
    std::string field;
    AssignOp op;
    std::unique_ptr<Expression> value;

    MemberAssignmentStatement(std::unique_ptr<Expression> b, const std::string& f, AssignOp o,
                              std::unique_ptr<Expression> v,
                              const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::MEMBER_ASSIGNMENT_STATEMENT, loc), base(std::move(b)), field(f),
          op(o), value(std::move(v)) {}

    std::string toString() const override {
        static const char* ops[] = {"=", "+=", "-=", "*=", "/=", "%="};
        return (base ? base->toString() : "?") + "." + field + " " + ops[(int)op] + " " +
               (value ? value->toString() : "");
    }
};

// EXP struct creation: `类型名 变量 = { 值, ... }` (positional, in field order).
class StructCreationStatement : public Statement {
public:
    std::string varName;
    std::string structName;
    std::vector<std::unique_ptr<Expression>> values;

    StructCreationStatement(const std::string& v, const std::string& s,
                            std::vector<std::unique_ptr<Expression>> vals,
                            const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::STRUCT_CREATION_STATEMENT, loc), varName(v), structName(s),
          values(std::move(vals)) {}

    std::string toString() const override {
        std::string s = structName + " " + varName + " = { ";
        for (size_t i = 0; i < values.size(); i++) {
            if (i) s += ", ";
            s += values[i]->toString();
        }
        return s + " }";
    }
};


// EXP `wrong <condition>`: a result that should not exist is forbidden. When the
// condition evaluates true at runtime, the program recomputes the result by
// minimally perturbing the participant variables until it is no longer true.
class WrongStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;

    WrongStatement(std::unique_ptr<Expression> c, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::WRONG_STATEMENT, loc), condition(std::move(c)) {}

    std::string toString() const override {
        return "wrong " + (condition ? condition->toString() : "<null>");
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

// EXP `pinocchio`: a self-referential proposition (Pinocchio paradox).
// `pinocchio (P) { THEN } else { ELSE } limit: N` re-evaluates the boolean
// proposition P over user state; each round runs THEN when P is true and ELSE
// when P is false (either may mutate state). The loop terminates when:
//   - the state P depends on stops changing          -> stable (P self-consistent)
//   - P's truth alternates (T,F,T, ...)              -> oscillation (paradox)
//   - `limit` rounds (default 1000) elapse            -> no stable solution
// Each termination prints an explicit `[pinocchio] ...` runtime result.
class PinocchioStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;    // the self-referential proposition P
    std::unique_ptr<Statement> thenBlock;     // runs each round when P is true
    std::unique_ptr<Statement> elseBlock;     // runs each round when P is false (optional)
    int limit;                                 // max iterations (default 1000)

    PinocchioStatement(std::unique_ptr<Expression> cond,
                       std::unique_ptr<Statement> thenB,
                       std::unique_ptr<Statement> elseB,
                       int lim,
                       const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::PINOCCHIO_STATEMENT, loc),
          condition(std::move(cond)), thenBlock(std::move(thenB)),
          elseBlock(std::move(elseB)), limit(lim) {}

    std::string toString() const override {
        std::string result = "pinocchio (" + (condition ? condition->toString() : "") + ") { ... }";
        if (elseBlock) result += " else { ... }";
        result += " limit: " + std::to_string(limit);
        return result;
    }
};

// EXP `dual`: split the existence of a variable into two independent selves.
// `dual x = expr` (or `dual x` on an already-existing variable) turns x into a
// bifurcated variable: from the split instant on, x has TWO independent
// existences — the original self x[0] (also reachable as plain `x`) and the
// second self x[1] — both born with the same value (one entity became two
// identical selves). Afterwards each self evolves on its own: `x = v` or
// `x[0] = v` touches only the original self, `x[1] = v` only the second self.
// The two selves are separate storage slots that diverge freely (this is NOT
// if/else, NOT thread/fork, NOT a copy into another name — it is a semantic
// "one -> two" birth, implemented purely by the LLVM backend).
class DualStatement : public Statement {
public:
    std::string name;
    std::unique_ptr<Expression> value; // optional `= expr`; null -> split current value

    DualStatement(const std::string& n, std::unique_ptr<Expression> v,
                  const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::DUAL_STATEMENT, loc), name(n), value(std::move(v)) {}

    std::string toString() const override {
        if (value) return "dual " + name + " = " + value->toString();
        return "dual " + name;
    }
};

// EXP `dual`: write to one of the two selves (`x[0] = v` / `x[1] = v`).
// Only dual variables can be targeted, and only constant indices 0 (original
// self) and 1 (second self) are legal.
class IndexedAssignmentStatement : public Statement {
public:
    std::string name;
    std::unique_ptr<Expression> index;
    std::unique_ptr<Expression> value;

    IndexedAssignmentStatement(const std::string& n, std::unique_ptr<Expression> idx,
                               std::unique_ptr<Expression> v,
                               const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::INDEXED_ASSIGNMENT_STATEMENT, loc), name(n),
          index(std::move(idx)), value(std::move(v)) {}

    std::string toString() const override {
        return name + "[" + (index ? index->toString() : "") + "] = " +
               (value ? value->toString() : "");
    }
};

// EXP `disposable`: a one-time (or N-time) store. `disposable a = v` pushes a
// layer that yields `v` exactly once; `disposable[5] a = v` yields it up to 5
// times. Every read consumes one use of the newest live layer and, once all
// layers are spent, reads fall back to the base value (the plain variable, if
// one existed) or report the variable as undefined/unavailable. A normal
// assignment to `a` clears all disposable layers and re-establishes a plain
// value. `disposable fn` (one-shot functions) is parsed as a plain
// FunctionDeclarationStatement with `func->disposable` set instead.
class DisposableStatement : public Statement {
public:
    std::string name;
    int64_t count = 1;       // remaining reads for this layer (>= 1)
    bool hasCount = false;   // true when the `disposable[n]` form is used
    std::unique_ptr<Expression> value;

    DisposableStatement(const std::string& n, int64_t cnt, bool hc,
                        std::unique_ptr<Expression> v,
                        const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::DISPOSABLE_STATEMENT, loc), name(n),
          count(cnt), hasCount(hc), value(std::move(v)) {}

    std::string toString() const override {
        if (hasCount) {
            return "disposable[" + std::to_string(count) + "] " + name + " = " +
                   (value ? value->toString() : "");
        }
        return "disposable " + name + " = " + (value ? value->toString() : "");
    }
};

// EXP `interest`: 利息变量。 `¥[0.1] a = v` (simple interest) / `$[0.1] a = v`
// (compound interest) define `a` as a FLOAT variable and attach an interest
// rule to it. Every READ of `a` returns its current value to the expression
// first, then applies every attached rule in definition order:
//   - ¥ simple:   add `principal * rate` where principal is a's value at the
//                 moment THIS rule was created (fixed, like interest on a loan).
//   - $ compound: add `current * rate` (interest on interest).
// Two reads inside one expression therefore see two different growing values.
// With no `[rate]` the rate defaults to 0.0001.
class InterestStatement : public Statement {
public:
    bool isCompound = false; // false = ¥ simple, true = $ compound
    bool hasRate = false;    // `¥[0.1] a = v` vs bare `¥a = v`
    double rate = 0.0001;    // per-read interest rate (as a fraction)
    std::string name;
    std::unique_ptr<Expression> value;

    InterestStatement(bool compound, bool hr, double r, const std::string& n,
                      std::unique_ptr<Expression> v,
                      const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::INTEREST_STATEMENT, loc), isCompound(compound),
          hasRate(hr), rate(r), name(n), value(std::move(v)) {}

    std::string toString() const override {
        std::string prefix = isCompound ? "$" : "¥";
        if (hasRate) prefix += "[" + std::to_string(rate) + "]";
        return prefix + " " + name + " = " + (value ? value->toString() : "");
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

// EXP `deja`: sense a variable's first constant future. `deja x` looks forward
// (straight-line code only) for x's FIRST assignment; if its RHS is a
// compile-time constant expression, the transform rewrites `deja x` into an
// implicit `x = <that constant>` so reads between the deja and the real
// assignment see the future value. Otherwise deja is dropped and a warning is
// emitted (normal semantics preserved). The real future assignment still runs.
class DejaStatement : public Statement {
public:
    std::string name;

    DejaStatement(const std::string& n, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::DEJA_STATEMENT, loc), name(n) {}

    std::string toString() const override {
        return "deja " + name;
    }
};

// EXP `fate`: give a variable a destiny value. `fate x = 100` records 100 as
// x's destiny (and sets x to it). x may still be assigned ANY value afterwards
// (`x = 20` is not an error), but each such deviation immediately triggers an
// IMPERFECT recovery: the stored value is pulled back toward the destiny value
// (midpoint halving), never set straight to it. Every recovery result becomes
// the persistent floor (底数) for later recoveries if it is the highest so far,
// so the variable can never be cleanly restored below its own recovery history
// — "you can resist fate, but your own traces keep fate from ever restoring
// you perfectly." Implemented purely in LLVM codegen (never transformed away).
class FateStatement : public Statement {
public:
    std::string name;
    std::unique_ptr<Expression> value;

    FateStatement(const std::string& n, std::unique_ptr<Expression> v,
                  const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::FATE_STATEMENT, loc), name(n), value(std::move(v)) {}

    std::string toString() const override {
        return "fate " + name + " = " + (value ? value->toString() : "");
    }
};

// EXP `envy`: the jealous variable `receiver` discovers that `target` is
// better (higher) on a comparable dimension and tries to close the gap.
// Never a plain `if a < b: a = b`. The reaction is chosen by how big the
// perceived disparity is (path selection at runtime, straight-line selects):
//   1. 超越 (surpass)   – small gap (gap <= self):  self jumps to target + 1.
//   2. 成为 (become)    – middling gap (self < gap <= 2*self): self becomes target.
//   3. 摧毁 (destroy)   – hopeless gap (gap > 2*self): target dragged below self
//                          (target = self - 1), self untouched — extreme envy.
// If self is already not worse (self >= target) nothing happens at all.
// Only same-typed integer/float variable pairs take part; any other combination
// (strings, bools, arrays, mixed types) is an incomparable dimension → no-op
// and a warning. Implemented purely in LLVM codegen (never transformed away).
class EnvyStatement : public Statement {
public:
    std::string receiver;
    std::string target;

    EnvyStatement(const std::string& recv, const std::string& tgt,
                  const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::ENVY_STATEMENT, loc), receiver(recv), target(tgt) {}

    std::string toString() const override {
        return "envy " + receiver + " " + target;
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
    // EXP `valuable`: the raw `{ ... }` tokens of the body. A code value passed
    // as an argument is substituted into these tokens and the result is compiled
    // as a fresh function, so a function has to be able to re-emit its own source.
    std::vector<Token> bodyTokens;
    SourceLocation location;

    // EXP `env`: the values this function silently carries, filled in by the
    // env pass (empty when the function is in no carrying set), in block order.
    // Each is the function's parameter of that type (structs read as pointers),
    // and `env <name> = v` re-points it. A function may sit in several blocks.
    std::vector<std::pair<std::string, std::string>> envCarried; // (paramName, typeName)
    
    // EXP `drift`: random recursive parameters. When enabled, every self call
    // inside the body substitutes its written argument values with runtime
    // random values drawn from a per-function range; the depth counter caps
    // how many randomized self calls can chain before returning 0.
    struct DriftConfig {
        bool enabled = false;
        bool hasRange = false;
        long long rangeLo = 0;      // int/long domain lower bound
        long long rangeHi = 0;      // int/long domain upper bound
        double rangeLoF = 0.0;      // float domain lower bound
        double rangeHiF = 0.0;      // float domain upper bound
        bool rangeIsFloat = false;  // user gave float literal bounds
        long long depthLimit = 0;   // 0 -> built-in default
    } drift;

    // EXP `disposable`: a function that can be called only once. The first call
    // runs normally; every later call reports an "undefined / unavailable"
    // runtime error and terminates. For `disposable fn foo() {}`.
    bool disposable = false;
    
    Function(const std::string& n, std::vector<std::unique_ptr<VariableDeclaration>> p,
             std::unique_ptr<BlockStatement> b, const SourceLocation& loc = SourceLocation())
        : name(n), ns(""), blockName(""), params(std::move(p)), body(std::move(b)), location(loc) {}
    
    Function(const std::string& n, const std::string& ns_name, std::vector<std::unique_ptr<VariableDeclaration>> p,
             std::unique_ptr<BlockStatement> b, const SourceLocation& loc = SourceLocation())
        : name(n), ns(ns_name), blockName(""), params(std::move(p)), body(std::move(b)), location(loc) {}
    
    std::string toString() const {
        std::string result = drift.enabled ? "drift fn " : (disposable ? "disposable fn " : "fn ");
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
    // EXP structs: module-level `type 名字 { ... }` declarations.
    std::vector<std::unique_ptr<StructDeclaration>> structs;
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

// EXP `value`: a = value expr  — asserts expr produces a value (not void)
class ValueExpression : public Expression {
public:
    std::unique_ptr<Expression> inner;
    ValueExpression(std::unique_ptr<Expression> e, const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::VALUE_EXPRESSION, loc), inner(std::move(e)) {}
    std::string toString() const override {
        return "value " + (inner ? inner->toString() : "<null>");
    }
};

// EXP `kill[x]`: invalidate line x at runtime
class KillStatement : public Statement {
public:
    int64_t targetLine;
    KillStatement(int64_t line, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::KILL_STATEMENT, loc), targetLine(line) {}
    std::string toString() const override {
        return "kill[" + std::to_string(targetLine) + "]";
    }
};

// EXP `censer[x]`: terminate if program prints exactly text x
class CenserStatement : public Statement {
public:
    std::string text;
    CenserStatement(const std::string& t, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::CENSER_STATEMENT, loc), text(t) {}
    std::string toString() const override {
        return "censer[" + text + "]";
    }
};

// EXP `noclip a`: variable enters backroom state — reads become non-deterministic
class NoclipStatement : public Statement {
public:
    std::string variableName;
    NoclipStatement(const std::string& name, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::NOCLIP_STATEMENT, loc), variableName(name) {}
    std::string toString() const override {
        return "noclip " + variableName;
    }
};

// EXP `shuffleback`: randomly reorder values of all backroom variables
class ShufflebackStatement : public Statement {
public:
    ShufflebackStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::SHUFFLEBACK_STATEMENT, loc) {}
    std::string toString() const override {
        return "shuffleback";
    }
};

// EXP `zombie a`: variable a becomes a plague. Any arithmetic operation that
// touches it (or touches another variable in the same operation) collapses to
// a's value instead of the real result, and every participating variable is
// overwritten with that value and infected in turn. Permanent, no cure.
class ZombieStatement : public Statement {
public:
    std::string variableName;

    ZombieStatement(const std::string& name, const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::ZOMBIE_STATEMENT, loc), variableName(name) {}

    std::string toString() const override {
        return "zombie " + variableName;
    }
};

// EXP `valuable { ... }`: a first-class code value. The braces hold a raw,
// uncompiled token slice which may be syntactically INCOMPLETE (`{ for item }`,
// `{ in arr }`); it only has to parse once it is spliced into a whole program.
class ValuableFragmentExpression : public Expression {
public:
    std::vector<Token> tokens; // inner tokens, without the enclosing braces

    ValuableFragmentExpression(std::vector<Token> t, const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::VALUABLE_FRAGMENT_EXPRESSION, loc), tokens(std::move(t)) {}

    std::string toString() const override {
        return "valuable { ... }";
    }
};

// EXP `call valuable A for x`: run A once at the current point and hand back
// the value x ended with. Parsed and compiled fresh at every use point.
class ValuableCallExpression : public Expression {
public:
    std::unique_ptr<Expression> base; // code-valued expression
    std::string targetName;           // the variable read back after the run

    ValuableCallExpression(std::unique_ptr<Expression> b, const std::string& t,
                           const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::VALUABLE_CALL_EXPRESSION, loc),
          base(std::move(b)), targetName(t) {}

    std::string toString() const override {
        return "call valuable " + (base ? base->toString() : "<null>") + " for " + targetName;
    }
};

// EXP `inject y valuable A for x`: bind A's free variable x to y and hand back
// the resulting code value. Purely a template/token substitution: nothing runs.
class ValuableInjectExpression : public Expression {
public:
    std::unique_ptr<Expression> target; // y: a variable or a literal
    std::unique_ptr<Expression> base;   // code-valued expression
    std::string freeName;                // x: must be a free name of the fragment

    ValuableInjectExpression(std::unique_ptr<Expression> t, std::unique_ptr<Expression> b,
                             const std::string& f, const SourceLocation& loc = SourceLocation())
        : Expression(NodeType::VALUABLE_INJECT_EXPRESSION, loc),
          target(std::move(t)), base(std::move(b)), freeName(f) {}

    std::string toString() const override {
        return "inject " + (target ? target->toString() : "<null>") + " valuable " +
               (base ? base->toString() : "<null>") + " for " + freeName;
    }
};

// EXP `valuable` in statement position: `A B { ... }` juxtaposes code values
// and splices them, left to right, with the raw `{ ... }` block appended. The
// merged fragment executes as one piece of code.
class ValuableUseStatement : public Statement {
public:
    std::vector<std::unique_ptr<Expression>> parts; // code values, source order
    std::vector<Token> blockTokens;                 // raw `{ ... }` tokens (with braces)

    ValuableUseStatement(const SourceLocation& loc = SourceLocation())
        : Statement(NodeType::VALUABLE_USE_STATEMENT, loc) {}

    std::string toString() const override {
        return "valuable-use(" + std::to_string(parts.size()) + " parts)";
    }
};

class Program {
public:
    std::vector<std::unique_ptr<Module>> modules;
    std::vector<std::unique_ptr<ImportStatement>> imports;
    // EXP structs: program-level `type 名字 { ... }` declarations.
    std::vector<std::unique_ptr<StructDeclaration>> structs;
    
    void addModule(std::unique_ptr<Module> mod) {
        modules.push_back(std::move(mod));
    }
    
    void addImport(std::unique_ptr<ImportStatement> imp) {
        imports.push_back(std::move(imp));
    }

    void addStruct(std::unique_ptr<StructDeclaration> sd) {
        structs.push_back(std::move(sd));
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
