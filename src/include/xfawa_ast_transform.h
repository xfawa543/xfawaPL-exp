#ifndef XFAWA_AST_TRANSFORM_H
#define XFAWA_AST_TRANSFORM_H

#include "xfawa_ast.h"
#include "xfawa_error.h"
#include <memory>
#include <vector>
#include <functional>

namespace xfawa {

class ASTTransformPass {
public:
    virtual ~ASTTransformPass() = default;
    virtual std::string getName() const = 0;
    virtual std::string getDescription() const = 0;
    
    virtual bool transform(Program* program) {
        return true;
    }
    
    virtual bool transform(Module* mod) {
        return true;
    }
    
    virtual bool transform(Function* func) {
        return true;
    }
    
    virtual bool transform(Statement* stmt) {
        return true;
    }
    
    virtual bool transform(Expression* expr) {
        return true;
    }
    
    virtual bool isEnabled() const { return enabled; }
    virtual void setEnabled(bool enable) { enabled = enable; }
    
    void setErrorSystem(ErrorSystem* es) {
        errorSystem = es;
    }
    
protected:
    bool enabled = true;
    ErrorSystem* errorSystem = nullptr;
};

class ASTTransformer {
private:
    std::vector<std::unique_ptr<ASTTransformPass>> passes;
    ErrorSystem* errorSystem;
    
public:
    ASTTransformer() : errorSystem(nullptr) {}
    
    void setErrorSystem(ErrorSystem* es) {
        errorSystem = es;
        for (auto& pass : passes) {
            pass->setErrorSystem(es);
        }
    }
    
    void addPass(std::unique_ptr<ASTTransformPass> pass) {
        pass->setErrorSystem(errorSystem);
        passes.push_back(std::move(pass));
    }
    
    bool transform(Program* program) {
        for (auto& pass : passes) {
            if (!pass->isEnabled()) continue;
            
            if (!pass->transform(program)) {
                return false;
            }
            
            for (auto& mod : program->modules) {
                if (!transformModule(pass.get(), mod.get())) {
                    return false;
                }
            }
        }
        return true;
    }
    
    size_t getPassCount() const {
        return passes.size();
    }
    
    void clear() {
        passes.clear();
    }
    
private:
    bool transformModule(ASTTransformPass* pass, Module* mod) {
        if (!pass->transform(mod)) {
            return false;
        }
        
        for (auto& func : mod->functions) {
            if (!transformFunction(pass, func.get())) {
                return false;
            }
        }
        
        return true;
    }
    
    bool transformFunction(ASTTransformPass* pass, Function* func) {
        if (!pass->transform(func)) {
            return false;
        }
        
        if (func->body) {
            if (!transformBlock(pass, func->body.get())) {
                return false;
            }
        }
        
        return true;
    }
    
    bool transformBlock(ASTTransformPass* pass, BlockStatement* block) {
        for (auto& stmt : block->statements) {
            if (!transformStatement(pass, stmt.get())) {
                return false;
            }
        }
        return true;
    }
    
    bool transformStatement(ASTTransformPass* pass, Statement* stmt) {
        if (!pass->transform(stmt)) {
            return false;
        }
        
        switch (stmt->getNodeType()) {
            case NodeType::BLOCK_STATEMENT: {
                auto* block = dynamic_cast<BlockStatement*>(stmt);
                return transformBlock(pass, block);
            }
            case NodeType::IF_STATEMENT: {
                auto* ifStmt = dynamic_cast<IfStatement*>(stmt);
                if (ifStmt->thenBranch) {
                    if (!transformStatement(pass, ifStmt->thenBranch.get())) return false;
                }
                for (auto& elseIf : ifStmt->elseIfBranches) {
                    if (!transformStatement(pass, elseIf.second.get())) return false;
                }
                if (ifStmt->elseBranch) {
                    if (!transformStatement(pass, ifStmt->elseBranch.get())) return false;
                }
                break;
            }
            case NodeType::WHILE_STATEMENT: {
                auto* whileStmt = dynamic_cast<WhileStatement*>(stmt);
                if (whileStmt->body) {
                    if (!transformStatement(pass, whileStmt->body.get())) return false;
                }
                break;
            }
            default:
                break;
        }
        
        return true;
    }
};

class NoOpTransformPass : public ASTTransformPass {
public:
    std::string getName() const override { return "NoOp"; }
    std::string getDescription() const override { return "No-operation pass for testing"; }
    
    bool transform(Program* program) override {
        return true;
    }
};

}

#endif
