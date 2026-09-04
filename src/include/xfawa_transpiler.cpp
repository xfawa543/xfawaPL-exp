#include "xfawa_transpiler.h"
#include <iostream>

namespace xfawa {

Transpiler::Transpiler(TargetLanguage lang) 
    : targetLang(lang), indentLevel(0) {}

std::string Transpiler::getIndent() const {
    return std::string(indentLevel * 2, ' ');
}

std::string Transpiler::getHeaders() const {
    if (targetLang == TargetLanguage::C) {
        return "#include <stdio.h>\n#include <stdlib.h>\n\n";
    } else {
        return "#include <iostream>\n#include <vector>\n\n";
    }
}

std::string Transpiler::mapType(VarType type) const {
    switch (type) {
        case VarType::INT:
            return "int";
        case VarType::FLOAT:
            return targetLang == TargetLanguage::C ? "float" : "double";
        case VarType::BOOL:
            return "bool";
        case VarType::STRING:
            return targetLang == TargetLanguage::C ? "char*" : "std::string";
        case VarType::ARRAY_INT:
            return targetLang == TargetLanguage::C ? "int*" : "std::vector<int>";
        case VarType::ARRAY_LONG:
            return targetLang == TargetLanguage::C ? "long*" : "std::vector<long>";
        case VarType::ARRAY_FLOAT:
            return targetLang == TargetLanguage::C ? "float*" : "std::vector<float>";
        case VarType::ARRAY_BOOL:
            return targetLang == TargetLanguage::C ? "bool*" : "std::vector<bool>";
        case VarType::ARRAY_STRING:
            return targetLang == TargetLanguage::C ? "char**" : "std::vector<std::string>";
        default:
            return "int";
    }
}

void Transpiler::setTargetLanguage(TargetLanguage lang) {
    targetLang = lang;
}

TargetLanguage Transpiler::getTargetLanguage() const {
    return targetLang;
}

std::string Transpiler::transpileExpression(const Expression* expr) {
    if (!expr) return "";
    
    switch (expr->nodeType) {
        case NodeType::NUMBER_LITERAL:
            return transpileNumber(static_cast<const NumberLiteral*>(expr));
        case NodeType::FLOAT_LITERAL:
            return transpileFloat(static_cast<const FloatLiteral*>(expr));
        case NodeType::STRING_LITERAL:
            return transpileString(static_cast<const StringLiteral*>(expr));
        case NodeType::BOOLEAN_LITERAL:
            return transpileBoolean(static_cast<const BooleanLiteral*>(expr));
        case NodeType::VARIABLE_EXPRESSION:
            return transpileVariable(static_cast<const VariableExpression*>(expr));
        case NodeType::BINARY_OP:
            return transpileBinaryOp(static_cast<const BinaryOp*>(expr));
        case NodeType::UNARY_OP:
            return transpileUnaryOp(static_cast<const UnaryOp*>(expr));
        case NodeType::CALL_EXPRESSION:
            return transpileCall(static_cast<const CallExpression*>(expr));
        case NodeType::ARRAY_LITERAL:
            return transpileArrayLiteral(static_cast<const ArrayLiteral*>(expr));
        case NodeType::ARRAY_INDEX_EXPRESSION:
            return transpileArrayIndex(static_cast<const ArrayIndexExpression*>(expr));
        default:
            return expr->toString();
    }
}

std::string Transpiler::transpileNumber(const NumberLiteral* num) {
    return std::to_string(num->value);
}

std::string Transpiler::transpileFloat(const FloatLiteral* flt) {
    return std::to_string(flt->value);
}

std::string Transpiler::transpileString(const StringLiteral* str) {
    return "\"" + str->value + "\"";
}

std::string Transpiler::transpileBoolean(const BooleanLiteral* boolLit) {
    return boolLit->value ? "true" : "false";
}

std::string Transpiler::transpileVariable(const VariableExpression* var) {
    return var->name;
}

std::string Transpiler::transpileBinaryOp(const BinaryOp* op) {
    std::string left = transpileExpression(op->left.get());
    std::string right = transpileExpression(op->right.get());
    
    std::string opStr;
    switch (op->op) {
        case BinaryOpType::ADD: opStr = "+"; break;
        case BinaryOpType::SUB: opStr = "-"; break;
        case BinaryOpType::MUL: opStr = "*"; break;
        case BinaryOpType::DIV: opStr = "/"; break;
        case BinaryOpType::MOD: opStr = "%"; break;
        case BinaryOpType::EQUAL: opStr = "=="; break;
        case BinaryOpType::NOT_EQUAL: opStr = "!="; break;
        case BinaryOpType::LESS: opStr = "<"; break;
        case BinaryOpType::LESS_EQUAL: opStr = "<="; break;
        case BinaryOpType::GREATER: opStr = ">"; break;
        case BinaryOpType::GREATER_EQUAL: opStr = ">="; break;
        case BinaryOpType::AND: opStr = "&&"; break;
        case BinaryOpType::OR: opStr = "||"; break;
        default: opStr = "?"; break;
    }
    
    return "(" + left + " " + opStr + " " + right + ")";
}

std::string Transpiler::transpileUnaryOp(const UnaryOp* op) {
    std::string expr = transpileExpression(op->expr.get());
    std::string opStr;
    
    switch (op->op) {
        case UnaryOpType::NEGATE: opStr = "-"; break;
        case UnaryOpType::NOT: opStr = "!"; break;
        default: opStr = "?"; break;
    }
    
    return opStr + "(" + expr + ")";
}

std::string Transpiler::transpileCall(const CallExpression* call) {
    std::string funcName;
    if (!call->ns.empty()) {
        funcName = call->ns + "_" + call->name;
    } else {
        funcName = call->name;
    }
    
    std::string args;
    for (size_t i = 0; i < call->args.size(); i++) {
        args += transpileExpression(call->args[i].get());
        if (i < call->args.size() - 1) {
            args += ", ";
        }
    }
    
    return funcName + "(" + args + ")";
}

std::string Transpiler::transpileArrayLiteral(const ArrayLiteral* arr) {
    if (arr->isRange) {
        // Range array [1...10]
        if (targetLang == TargetLanguage::C) {
            // C: requires dynamic array generation
            return "/* range array - requires dynamic allocation */";
        } else {
            // C++: use std::vector
            std::string start = transpileExpression(arr->rangeStart.get());
            std::string end = transpileExpression(arr->rangeEnd.get());
            return "/* range array from " + start + " to " + end + " */";
        }
    } else {
        // 显式元素数组 [1, 2, 3]
        if (targetLang == TargetLanguage::C) {
            std::string result = "{";
            for (size_t i = 0; i < arr->elements.size(); i++) {
                result += transpileExpression(arr->elements[i].get());
                if (i < arr->elements.size() - 1) {
                    result += ", ";
                }
            }
            result += "}";
            return result;
        } else {
            std::string result = "{";
            for (size_t i = 0; i < arr->elements.size(); i++) {
                result += transpileExpression(arr->elements[i].get());
                if (i < arr->elements.size() - 1) {
                    result += ", ";
                }
            }
            result += "}";
            return result;
        }
    }
}

std::string Transpiler::transpileArrayIndex(const ArrayIndexExpression* arrIdx) {
    std::string array = transpileExpression(arrIdx->array.get());
    std::string index = transpileExpression(arrIdx->index.get());
    return array + "[" + index + "]";
}

std::string Transpiler::transpileStatement(const Statement* stmt) {
    if (!stmt) return "";
    
    switch (stmt->nodeType) {
        case NodeType::PRINT_STATEMENT:
            return transpilePrint(static_cast<const PrintStatement*>(stmt));
        case NodeType::IF_STATEMENT:
            return transpileIf(static_cast<const IfStatement*>(stmt));
        case NodeType::WHILE_STATEMENT:
            return transpileWhile(static_cast<const WhileStatement*>(stmt));
        case NodeType::FOR_IN_STATEMENT:
            return transpileForIn(static_cast<const ForInStatement*>(stmt));
        case NodeType::ASSIGNMENT_STATEMENT:
            return transpileAssignment(static_cast<const AssignmentStatement*>(stmt));
        case NodeType::RETURN_STATEMENT:
            return transpileReturn(static_cast<const ReturnStatement*>(stmt));
        case NodeType::BREAK_STATEMENT:
            return transpileBreak(static_cast<const BreakStatement*>(stmt));
        case NodeType::BLOCK_STATEMENT:
            return transpileBlock(static_cast<const BlockStatement*>(stmt));
        case NodeType::EXPRESSION_STATEMENT:
            return getIndent() + transpileExpression(static_cast<const ExpressionStatement*>(stmt)->expr.get()) + 
                   (targetLang == TargetLanguage::C ? ";" : ";") + "\n";
        default:
            return getIndent() + stmt->toString() + "\n";
    }
}

std::string Transpiler::transpilePrint(const PrintStatement* stmt) {
    std::string exprStr = transpileExpression(stmt->expr.get());
    
    if (targetLang == TargetLanguage::C) {
        // C: printf
        // Need to determine expression type
        if (stmt->expr->nodeType == NodeType::STRING_LITERAL) {
            return getIndent() + "printf(\"%s\\n\", " + exprStr + ");\n";
        } else if (stmt->expr->nodeType == NodeType::NUMBER_LITERAL) {
            return getIndent() + "printf(\"%d\\n\", " + exprStr + ");\n";
        } else if (stmt->expr->nodeType == NodeType::FLOAT_LITERAL) {
            return getIndent() + "printf(\"%f\\n\", " + exprStr + ");\n";
        } else {
            // General case
            return getIndent() + "printf(\"%d\\n\", " + exprStr + ");\n";
        }
    } else {
        // C++: std::cout
        return getIndent() + "std::cout << " + exprStr + " << std::endl;\n";
    }
}

std::string Transpiler::transpileIf(const IfStatement* stmt) {
    std::string result;
    
    // if 部分
    std::string condition = transpileExpression(stmt->condition.get());
    result += getIndent() + "if (" + condition + ") ";
    
    if (stmt->thenBranch->nodeType == NodeType::BLOCK_STATEMENT) {
        result += transpileBlock(static_cast<const BlockStatement*>(stmt->thenBranch.get()));
    } else {
        indentLevel++;
        result += "\n" + transpileStatement(stmt->thenBranch.get());
        indentLevel--;
    }
    
    // else if 部分
    for (const auto& elseIf : stmt->elseIfBranches) {
        std::string elseIfCondition = transpileExpression(elseIf.first.get());
        result += getIndent() + "else if (" + elseIfCondition + ") ";
        
        if (elseIf.second->nodeType == NodeType::BLOCK_STATEMENT) {
            result += transpileBlock(static_cast<const BlockStatement*>(elseIf.second.get()));
        } else {
            indentLevel++;
            result += "\n" + transpileStatement(elseIf.second.get());
            indentLevel--;
        }
    }
    
    // else 部分
    if (stmt->elseBranch) {
        result += getIndent() + "else ";
        
        if (stmt->elseBranch->nodeType == NodeType::BLOCK_STATEMENT) {
            result += transpileBlock(static_cast<const BlockStatement*>(stmt->elseBranch.get()));
        } else {
            indentLevel++;
            result += "\n" + transpileStatement(stmt->elseBranch.get());
            indentLevel--;
        }
    }
    
    return result;
}

std::string Transpiler::transpileWhile(const WhileStatement* stmt) {
    std::string condition = transpileExpression(stmt->condition.get());
    std::string result = getIndent() + "while (" + condition + ") ";
    
    if (stmt->body->nodeType == NodeType::BLOCK_STATEMENT) {
        result += transpileBlock(static_cast<const BlockStatement*>(stmt->body.get()));
    } else {
        indentLevel++;
        result += "\n" + transpileStatement(stmt->body.get());
        indentLevel--;
    }
    
    return result;
}

std::string Transpiler::transpileForIn(const ForInStatement* stmt) {
    // xfawa for-in needs to be converted to C/C++ loop
    std::string varName = stmt->varName;
    std::string iterable = transpileExpression(stmt->iterable.get());
    
    if (stmt->iterable->nodeType == NodeType::ARRAY_LITERAL) {
        const ArrayLiteral* arr = static_cast<const ArrayLiteral*>(stmt->iterable.get());
        
        if (arr->isRange) {
            // 范围数组 [1...10]
            std::string start = transpileExpression(arr->rangeStart.get());
            std::string end = transpileExpression(arr->rangeEnd.get());
            
            std::string result = getIndent() + "for (int " + varName + " = " + start + "; " + 
                                 varName + " <= " + end + "; " + varName + "++) ";
            
            if (stmt->body->nodeType == NodeType::BLOCK_STATEMENT) {
                result += transpileBlock(static_cast<const BlockStatement*>(stmt->body.get()));
            } else {
                indentLevel++;
                result += "\n" + transpileStatement(stmt->body.get());
                indentLevel--;
            }
            
            return result;
        } else {
            // 显式元素数组 [1, 2, 3]
            if (targetLang == TargetLanguage::C) {
                // C: 使用索引循环
                std::string result = getIndent() + "int arr_" + varName + "[] = " + 
                                     transpileArrayLiteral(arr) + ";\n";
                result += getIndent() + "int arr_" + varName + "_size = " + 
                          std::to_string(arr->elements.size()) + ";\n";
                result += getIndent() + "for (int i = 0; i < arr_" + varName + "_size; i++) {\n";
                indentLevel++;
                result += getIndent() + "int " + varName + " = arr_" + varName + "[i];\n";
                result += transpileStatement(stmt->body.get());
                indentLevel--;
                result += getIndent() + "}\n";
                return result;
            } else {
                // C++: 使用 std::vector 和迭代器
                std::string result = getIndent() + "std::vector<int> arr_" + varName + " = " + 
                                     transpileArrayLiteral(arr) + ";\n";
                result += getIndent() + "for (int " + varName + " : arr_" + varName + ") ";
                
                if (stmt->body->nodeType == NodeType::BLOCK_STATEMENT) {
                    result += transpileBlock(static_cast<const BlockStatement*>(stmt->body.get()));
                } else {
                    indentLevel++;
                    result += "\n" + transpileStatement(stmt->body.get());
                    indentLevel--;
                }
                
                return result;
            }
        }
    } else {
        // 其他可迭代对象（变量等）
        if (targetLang == TargetLanguage::C) {
            std::string result = getIndent() + "/* for-in on variable: " + iterable + " */\n";
            result += getIndent() + "for (int i = 0; i < /* size */; i++) {\n";
            indentLevel++;
            result += getIndent() + "int " + varName + " = " + iterable + "[i];\n";
            result += transpileStatement(stmt->body.get());
            indentLevel--;
            result += getIndent() + "}\n";
            return result;
        } else {
            std::string result = getIndent() + "for (int " + varName + " : " + iterable + ") ";
            
            if (stmt->body->nodeType == NodeType::BLOCK_STATEMENT) {
                result += transpileBlock(static_cast<const BlockStatement*>(stmt->body.get()));
            } else {
                indentLevel++;
                result += "\n" + transpileStatement(stmt->body.get());
                indentLevel--;
            }
            
            return result;
        }
    }
}

std::string Transpiler::transpileAssignment(const AssignmentStatement* stmt) {
    std::string value = transpileExpression(stmt->value.get());
    
    if (stmt->isReassignment) {
        return getIndent() + stmt->name + " = " + value + ";\n";
    } else {
        std::string typeStr = stmt->hasExplicitType ? mapType(stmt->declaredType) : "auto";
        return getIndent() + typeStr + " " + stmt->name + " = " + value + ";\n";
    }
}

std::string Transpiler::transpileReturn(const ReturnStatement* stmt) {
    std::string value = transpileExpression(stmt->value.get());
    return getIndent() + "return " + value + ";\n";
}

std::string Transpiler::transpileBreak(const BreakStatement* stmt) {
    return getIndent() + "break;\n";
}

std::string Transpiler::transpileBlock(const BlockStatement* block) {
    std::string result = "{\n";
    indentLevel++;
    
    for (const auto& stmt : block->statements) {
        result += transpileStatement(stmt.get());
    }
    
    indentLevel--;
    result += getIndent() + "}\n";
    return result;
}

std::string Transpiler::transpileFunction(const Function* func) {
    std::string funcName;
    if (!func->ns.empty()) {
        funcName = func->ns + "_" + func->name;
    } else if (!func->blockName.empty()) {
        funcName = func->blockName + "_" + func->name;
    } else {
        funcName = func->name;
    }
    
    std::string result;
    
    // Function signature (without body)
    result += "// Function: " + func->name + "\n";
    result += "void " + funcName + "(";
    
    // 参数
    for (size_t i = 0; i < func->params.size(); i++) {
        result += "int " + func->params[i]->name;
        if (i < func->params.size() - 1) {
            result += ", ";
        }
    }
    
    result += ") ";
    
    // Function body
    result += transpileBlock(func->body.get());
    
    return result;
}

std::string Transpiler::transpileModule(const Module* mod) {
    std::string result;
    
    result += "// Module: " + mod->name + "\n";
    result += "// Block system is not supported in C/C++\n\n";
    
    for (const auto& func : mod->functions) {
        result += transpileFunction(func.get());
        result += "\n";
    }
    
    return result;
}

std::string Transpiler::transpile(const Program* program) {
    output.str("");
    output.clear();
    
    // Headers
    output << getHeaders();
    
    // Import statements (as comments)
    if (!program->imports.empty()) {
        output << "// Imports:\n";
        for (const auto& imp : program->imports) {
            output << "// " << imp->toString() << "\n";
        }
        output << "\n";
    }
    
    // 模块
    for (const auto& mod : program->modules) {
        output << transpileModule(mod.get());
    }
    
    // main function (if exists)
    bool hasMain = false;
    std::string mainModuleName = "";
    for (const auto& mod : program->modules) {
        for (const auto& func : mod->functions) {
            if (func->name == "main") {
                hasMain = true;
                mainModuleName = mod->name;
                break;
            }
        }
        if (hasMain) break;
    }
    
    if (hasMain) {
        std::string mainFuncName = mainModuleName.empty() ? "main" : mainModuleName + "_main";
        if (targetLang == TargetLanguage::C) {
            output << "\nint main() {\n";
            output << "    // Call xfawa main function\n";
            output << "    " << mainFuncName << "();\n";
            output << "    return 0;\n";
            output << "}\n";
        } else {
            output << "\nint main() {\n";
            output << "    // Call xfawa main function\n";
            output << "    " << mainFuncName << "();\n";
            output << "    return 0;\n";
            output << "}\n";
        }
    }
    
    return output.str();
}

std::string Transpiler::transpileModuleOnly(const Module* mod) {
    output.str("");
    output.clear();
    
    output << getHeaders();
    output << transpileModule(mod);
    
    return output.str();
}

}