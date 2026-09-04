#ifndef XFAWA_TRANSPILER_H
#define XFAWA_TRANSPILER_H

#include "xfawa_ast.h"
#include <string>
#include <sstream>

namespace xfawa {

enum class TargetLanguage {
    C,
    CPP
};

class Transpiler {
private:
    TargetLanguage targetLang;
    std::stringstream output;
    int indentLevel;
    
    std::string getIndent() const;
    std::string transpileExpression(const Expression* expr);
    std::string transpileStatement(const Statement* stmt);
    std::string transpileBlock(const BlockStatement* block);
    std::string transpileFunction(const Function* func);
    std::string transpileModule(const Module* mod);
    
    // 特定语法的转译
    std::string transpilePrint(const PrintStatement* stmt);
    std::string transpileIf(const IfStatement* stmt);
    std::string transpileWhile(const WhileStatement* stmt);
    std::string transpileForIn(const ForInStatement* stmt);
    std::string transpileAssignment(const AssignmentStatement* stmt);
    std::string transpileReturn(const ReturnStatement* stmt);
    std::string transpileBreak(const BreakStatement* stmt);
    
    // 表达式转译
    std::string transpileBinaryOp(const BinaryOp* op);
    std::string transpileUnaryOp(const UnaryOp* op);
    std::string transpileCall(const CallExpression* call);
    std::string transpileVariable(const VariableExpression* var);
    std::string transpileNumber(const NumberLiteral* num);
    std::string transpileFloat(const FloatLiteral* flt);
    std::string transpileString(const StringLiteral* str);
    std::string transpileBoolean(const BooleanLiteral* boolLit);
    std::string transpileArrayLiteral(const ArrayLiteral* arr);
    std::string transpileArrayIndex(const ArrayIndexExpression* arrIdx);
    
    // 类型映射
    std::string mapType(VarType type) const;
    
public:
    explicit Transpiler(TargetLanguage lang);
    
    std::string transpile(const Program* program);
    std::string transpileModuleOnly(const Module* mod);
    
    void setTargetLanguage(TargetLanguage lang);
    TargetLanguage getTargetLanguage() const;
    
    // 获取必要的头文件
    std::string getHeaders() const;
};

}

#endif