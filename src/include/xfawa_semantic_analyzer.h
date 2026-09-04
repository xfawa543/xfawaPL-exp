#ifndef XFAWA_SEMANTIC_ANALYZER_H
#define XFAWA_SEMANTIC_ANALYZER_H

#include "xfawa_ast.h"
#include "xfawa_namespace_policy.h"
#include "xfawa_error.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace xfawa {

struct FunctionInfo {
    std::string name;
    std::string ns;
    std::string moduleName;
    int paramCount;
    bool isPublic;
    SourceLocation location;
};

class SemanticAnalyzer {
private:
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::unordered_map<std::string, FunctionInfo> publicFunctions;
    std::unordered_map<std::string, FunctionInfo> privateFunctions;
    std::unordered_map<std::string, FunctionInfo> allFunctions;
    std::string currentModule;
    
public:
    bool analyze(Program* program) {
        if (!program) return false;
        
        for (const auto& mod : program->modules) {
            collectModuleFunctions(mod.get());
        }
        
        for (const auto& mod : program->modules) {
            if (!analyzeModule(mod.get())) {
                return false;
            }
        }
        
        return !hasErrors();
    }
    
    bool hasErrors() const { return !errors.empty(); }
    bool hasWarnings() const { return !warnings.empty(); }
    const std::vector<std::string>& getErrors() const { return errors; }
    const std::vector<std::string>& getWarnings() const { return warnings; }
    
private:
    void collectModuleFunctions(Module* mod) {
        if (!mod) return;
        
        for (const auto& func : mod->functions) {
            std::string key = func->ns.empty() ? 
                (mod->name + ":" + func->name) : 
                (func->ns + ":" + func->name);
            
            FunctionInfo info;
            info.name = func->name;
            info.ns = func->ns;
            info.moduleName = mod->name;
            info.paramCount = static_cast<int>(func->params.size());
            info.isPublic = !func->ns.empty();
            info.location = func->location;
            
            allFunctions[key] = info;
            
            if (info.isPublic) {
                publicFunctions[key] = info;
            } else {
                privateFunctions[key] = info;
            }
        }
    }
    bool analyzeModule(Module* mod) {
        if (!mod) return false;
        
        currentModule = mod->name;
        
        for (const auto& func : mod->functions) {
            if (!analyzeFunction(func.get())) {
                return false;
            }
        }
        
        return true;
    }
    
    bool analyzeFunction(Function* func) {
        if (!func) return false;
        
        if (!func->ns.empty()) {
            if (NamespacePolicy::isReserved(func->ns)) {
                errors.push_back(NamespacePolicy::getReservedNamespacesError(func->ns) + 
                    " at line " + std::to_string(func->location.line));
                return false;
            }
            
            if (!NamespacePolicy::isValidUserNamespace(func->ns)) {
                errors.push_back(NamespacePolicy::getInvalidNamespaceError(func->ns) + 
                    " at line " + std::to_string(func->location.line));
                return false;
            }
        }
        
        std::string key = func->ns.empty() ? 
            (currentModule + ":" + func->name) : 
            (func->ns + ":" + func->name);
        
        FunctionInfo info;
        info.name = func->name;
        info.ns = func->ns;
        info.moduleName = currentModule;
        info.paramCount = static_cast<int>(func->params.size());
        info.isPublic = !func->ns.empty();
        info.location = func->location;
        
        if (info.isPublic) {
            if (allFunctions.find(key) == allFunctions.end()) {
                publicFunctions[key] = info;
            }
        } else {
            if (allFunctions.find(key) == allFunctions.end()) {
                privateFunctions[key] = info;
            }
        }
        
        if (func->body) {
            if (!analyzeBlock(func->body.get())) {
                return false;
            }
        }
        
        return true;
    }
    
    bool analyzeBlock(BlockStatement* block) {
        if (!block) return true;
        
        for (const auto& stmt : block->statements) {
            if (!analyzeStatement(stmt.get())) {
                return false;
            }
        }
        
        return true;
    }
    
    bool analyzeStatement(Statement* stmt) {
        if (!stmt) return true;
        
        switch (stmt->getNodeType()) {
            case NodeType::PRINT_STATEMENT: {
                auto* printStmt = dynamic_cast<PrintStatement*>(stmt);
                return analyzeExpression(printStmt->expr.get());
            }
            case NodeType::EXPRESSION_STATEMENT: {
                auto* exprStmt = dynamic_cast<ExpressionStatement*>(stmt);
                return analyzeExpression(exprStmt->expr.get());
            }
            case NodeType::ASSIGNMENT_STATEMENT: {
                auto* assignStmt = dynamic_cast<AssignmentStatement*>(stmt);
                return analyzeExpression(assignStmt->value.get());
            }
            case NodeType::RETURN_STATEMENT: {
                auto* retStmt = dynamic_cast<ReturnStatement*>(stmt);
                return retStmt->value ? analyzeExpression(retStmt->value.get()) : true;
            }
            case NodeType::BLOCK_STATEMENT: {
                auto* blockStmt = dynamic_cast<BlockStatement*>(stmt);
                return analyzeBlock(blockStmt);
            }
            case NodeType::LIE_STATEMENT: {
                auto* lieStmt = dynamic_cast<LieStatement*>(stmt);
                if (lieStmt && lieStmt->body) return analyzeBlock(lieStmt->body.get());
                return true;
            }
            case NodeType::WHILE_STATEMENT: {
                auto* whileStmt = dynamic_cast<WhileStatement*>(stmt);
                if (!analyzeExpression(whileStmt->condition.get())) return false;
                return analyzeStatement(whileStmt->body.get());
            }
            case NodeType::IF_STATEMENT: {
                auto* ifStmt = dynamic_cast<IfStatement*>(stmt);
                if (!analyzeExpression(ifStmt->condition.get())) return false;
                if (!analyzeStatement(ifStmt->thenBranch.get())) return false;
                for (const auto& elseIf : ifStmt->elseIfBranches) {
                    if (!analyzeExpression(elseIf.first.get())) return false;
                    if (!analyzeStatement(elseIf.second.get())) return false;
                }
                if (ifStmt->elseBranch && !analyzeStatement(ifStmt->elseBranch.get())) return false;
                return true;
            }
            case NodeType::FUNCTION_DECLARATION: {
                auto* funcDecl = dynamic_cast<FunctionDeclarationStatement*>(stmt);
                if (funcDecl->func) {
                    return analyzeFunction(funcDecl->func.get());
                }
                return true;
            }
            case NodeType::WINDOW_STATEMENT: {
                return true;
            }
            default:
                return true;
        }
    }
    
    bool analyzeExpression(Expression* expr) {
        if (!expr) return true;
        
        switch (expr->getNodeType()) {
            case NodeType::CALL_EXPRESSION: {
                auto* callExpr = dynamic_cast<CallExpression*>(expr);
                return analyzeCallExpression(callExpr);
            }
            case NodeType::BINARY_OP: {
                auto* binOp = dynamic_cast<BinaryOp*>(expr);
                if (!analyzeExpression(binOp->left.get())) return false;
                return analyzeExpression(binOp->right.get());
            }
            case NodeType::UNARY_OP: {
                auto* unaryOp = dynamic_cast<UnaryOp*>(expr);
                return analyzeExpression(unaryOp->expr.get());
            }
            default:
                return true;
        }
    }
    
    bool analyzeCallExpression(CallExpression* call) {
        if (!call) return true;
        
        if (call->name == "rnd") {
            if (call->args.size() != 1 && call->args.size() != 2) {
                errors.push_back("rnd() requires 1 (array) or 2 (min, max) arguments at line " + 
                    std::to_string(call->location.line));
                return false;
            }
            for (const auto& arg : call->args) {
                if (!analyzeExpression(arg.get())) {
                    return false;
                }
            }
            return true;
        }
        
        if (call->name == "input") {
            if (call->args.size() != 0) {
                errors.push_back("input() takes no arguments at line " + 
                    std::to_string(call->location.line));
                return false;
            }
            return true;
        }
        
        if (!call->ns.empty()) {
            // Check for reserved namespace
            if (NamespacePolicy::isReserved(call->ns)) {
                errors.push_back(NamespacePolicy::getReservedNamespacesError(call->ns) + 
                    " at line " + std::to_string(call->location.line));
                return false;
            }
            
            if (!NamespacePolicy::isValidUserNamespace(call->ns)) {
                errors.push_back(NamespacePolicy::getInvalidNamespaceError(call->ns) + 
                    " at line " + std::to_string(call->location.line));
                return false;
            }
            
            // Alpha17: Support block.function() syntax
            // First, try to find as a block function (block:function)
            std::string blockKey = call->ns + ":" + call->name;
            
            // Check if it's a public function (old ns:function syntax)
            if (publicFunctions.find(blockKey) != publicFunctions.end()) {
                // Found as public function
            }
            // Check if it's a block function (any function in the block)
            else if (allFunctions.find(blockKey) != allFunctions.end()) {
                // Found as block function
            }
            else {
                // Function not found, provide suggestions
                std::vector<std::string> candidates;
                for (const auto& pair : allFunctions) {
                    // Extract function name from key (block:function format)
                    size_t colonPos = pair.first.find(':');
                    if (colonPos != std::string::npos) {
                        std::string funcBlock = pair.first.substr(0, colonPos);
                        std::string funcName = pair.first.substr(colonPos + 1);
                        // Check if it's in the same block or has matching name
                        if (funcBlock == call->ns || funcName == call->name) {
                            candidates.push_back(funcBlock + "." + funcName);
                        }
                    }
                }
                
                // Add xfw library functions to candidates
                std::vector<std::string> suggestions = findSuggestions(call->name, 
                    std::vector<std::string>(candidates.begin(), candidates.end()), 3, 3);
                
                std::string errorMsg = "Undefined function: " + call->ns + "." + call->name + 
                    " at line " + std::to_string(call->location.line);
                
                if (!suggestions.empty()) {
                    errorMsg += "\n  Did you mean:\n";
                    for (size_t i = 0; i < suggestions.size(); i++) {
                        errorMsg += "    " + std::to_string(i + 1) + ". " + suggestions[i] + "\n";
                    }
                }
                
                errors.push_back(errorMsg);
                return false;
            }
        } else {
            std::string key = currentModule + ":" + call->name;
            if (privateFunctions.find(key) == privateFunctions.end()) {
                bool found = false;
                for (const auto& pair : allFunctions) {
                    if (pair.second.name == call->name) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // Provide suggestions for similar function names
                    std::vector<std::string> candidates;
                    for (const auto& pair : allFunctions) {
                        candidates.push_back(pair.second.name);
                    }
                    
                    std::vector<std::string> suggestions = findSuggestions(call->name, candidates, 3, 3);
                    
                    std::string errorMsg = "Undefined function: " + call->name + 
                        " at line " + std::to_string(call->location.line);
                    
                    if (!suggestions.empty()) {
                        errorMsg += "\n  Did you mean:\n";
                        for (size_t i = 0; i < suggestions.size(); i++) {
                            errorMsg += "    " + std::to_string(i + 1) + ". " + suggestions[i] + "\n";
                        }
                    }
                    
                    errors.push_back(errorMsg);
                    return false;
                }
            }
        }
        
        for (const auto& arg : call->args) {
            if (!analyzeExpression(arg.get())) {
                return false;
            }
        }
        
        return true;
    }
};

}

#endif
