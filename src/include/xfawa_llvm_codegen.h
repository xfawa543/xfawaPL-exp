#ifndef XFAWA_LLVM_CODEGEN_H
#define XFAWA_LLVM_CODEGEN_H

#include "xfawa_ast.h"
#include "xfawa_config.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Object/ELFObjectFile.h>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>

namespace xfawa {

class LLVMCodegen {
private:
    llvm::LLVMContext& context;
    llvm::Module* module;
    llvm::IRBuilder<> builder;
    std::map<std::string, llvm::AllocaInst*> locals;
    std::map<std::string, VarType> localTypes;
    std::map<std::string, int64_t> arrayLengths;
    std::map<std::string, std::vector<VarType>> callArgTypes;
    std::map<std::string, VarType> funcReturnTypes;
    std::map<std::string, llvm::GlobalVariable*> windowInputGlobals;
    std::map<std::string, VarType> windowInputTypes;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool hasMainFunction;
    bool hasWindowStatements;
    bool usesRandomBuiltin;
    bool hasBsod;
    bool hasEllipsisRandSeeded = false;   // only seed `rand()` once per program
    llvm::BasicBlock* loopEndBB;
    OptimizationLevel optLevel;
    int generatedWindowCount;
    int activeWindowId;
    bool xraphicsLogEnabled;
    // EXP-001: maps a statement pointer to how many times it must be emitted
    // (driven by an executable comment like `// repeat: 3`).
    std::unordered_map<const Statement*, int> repeatMap;

    // EXP `un`: disable statement keywords globally (first version).
    bool unPrintDisabled = false;
    bool unBoomDisabled = false;
    bool unBsodDisabled = false;

    // EXP `believe`: normalized left-expression key -> believed result.
    std::unordered_map<std::string, int64_t> beliefMap;
    
public:
    LLVMCodegen(llvm::LLVMContext& ctx, llvm::Module* mod);
    LLVMCodegen(llvm::LLVMContext& ctx, llvm::Module* mod, OptimizationLevel opt);

    void setXraphicsLogEnabled(bool enabled) { xraphicsLogEnabled = enabled; }

    void setRepeatMap(const std::unordered_map<const Statement*, int>& m) { repeatMap = m; }
    
    static void initializeTargets();
    
    bool codegenProgram(Program* program);
    
    bool emitObjectFile(const std::string& filename);
    bool emitObjectFile(const std::string& filename, bool keepLL, bool emitAsm, 
                        const std::string& llOutputPath, const std::string& asmOutputPath);
    bool linkExecutable(const std::string& objFile, const std::string& outFile);
    bool verifyModule();
    
    const std::vector<std::string>& getErrors() const { return errors; }
    const std::vector<std::string>& getWarnings() const { return warnings; }
    bool hasWarnings() const { return !warnings.empty(); }
    
private:
    void initBuiltins();
    void addError(const std::string& message) { errors.push_back(message); }
    void addWarning(const std::string& message) { warnings.push_back(message); }
    
    void runOptimizations();
    void runO0Optimizations();
    void runO1Optimizations();
    void runO2Optimizations();
    void runO3Optimizations();
    
    llvm::Type* getLLVMType(VarType type);
    llvm::Type* getArrayElementType(VarType type);
    
    void collectCallArgTypes(Program* program);
    void collectCallArgTypes(Statement* stmt);
    void collectCallArgTypes(Expression* expr);
    VarType getExpressionType(Expression* expr);
    
    VarType inferParamTypeFromBody(Statement* stmt, const std::string& paramName);
    VarType inferParamTypeFromExpr(Expression* expr, const std::string& paramName);
    
    llvm::Value* codegen(Expression* expr);
    bool codegen(Statement* stmt);
    bool codegenOnce(Statement* stmt);
    bool codegen(Module* mod);
    bool codegen(Function* func);
    bool codegen(ImportStatement* stmt);
    llvm::Function* createWindowProc(WindowStatement* windowStmt, int windowId, const std::vector<llvm::Function*>& buttonHandlers);
    llvm::Function* createWindowRuntime(WindowStatement* windowStmt, int windowId, llvm::Function* wndProc);
    llvm::Function* createButtonHandler(ButtonStatement* buttonStmt, int windowId, int buttonId, int printWindowId);
    llvm::GlobalVariable* getWindowCountGlobal();
    llvm::GlobalVariable* getWindowHandleGlobal(int windowId);
    uint32_t resolveWindowColor(const std::string& colorName) const;
    
    llvm::Value* codegen(NumberLiteral* expr);
    llvm::Value* codegen(FloatLiteral* expr);
    llvm::Value* codegen(BooleanLiteral* expr);
    llvm::Value* codegen(StringLiteral* expr);
    llvm::Value* codegen(VariableExpression* expr);
    llvm::Value* codegen(UnaryOp* expr);
    llvm::Value* codegen(BinaryOp* expr);
    llvm::Value* codegen(CallExpression* expr);
    llvm::Value* codegen(ArrayRangeExpression* expr);
    llvm::Value* codegen(ArrayLiteral* expr);
    llvm::Value* codegen(ArrayIndexExpression* expr);
    llvm::Value* codegen(ExpressionStatement* stmt);
    llvm::Value* codegen(AssignmentStatement* stmt);
    llvm::Value* codegen(PrintStatement* stmt);
    llvm::Value* codegen(ReturnStatement* stmt);
    llvm::Value* codegen(BreakStatement* stmt);
    llvm::Value* codegen(BoomStatement* stmt);
    llvm::Value* codegen(BsodStatement* stmt);
    llvm::Value* codegen(BelieveStatement* stmt);
    llvm::Value* codegen(LieStatement* stmt);
    llvm::Value* codegen(UnStatement* stmt);
    llvm::Value* codegen(IgnoreStatement* stmt);
    llvm::Value* codegen(DoStatement* stmt);
    llvm::Value* codegen(PleaseStatement* stmt);
    llvm::Value* codegen(ShutupStatement* stmt);
    llvm::Value* codegen(EllipsisStatement* stmt);
    llvm::Value* codegen(BlockStatement* stmt);
    llvm::Value* codegen(IfStatement* stmt);
    llvm::Value* codegen(WhileStatement* stmt);
    llvm::Value* codegen(ForInStatement* stmt);
    bool codegen(LoopStatement* stmt);
    llvm::Value* codegen(WindowStatement* stmt);
    llvm::Value* codegen(ButtonStatement* stmt);
    llvm::Value* codegen(TextStatement* stmt);
    llvm::Value* codegen(BoxStatement* stmt);
    llvm::Value* codegen(InputStatement* stmt);

private:
    // Xraphics object rendering helpers (for class blocks inside window)
    // Returns int value from a NumberLiteral/FloatLiteral expression, or defaultValue on failure
    static int evalIntExpr(Expression* expr, int defaultValue = 0);
    // Returns unsigned int color value (0xRRGGBB) from a ColorLiteral expression, or defaultValue on failure
    static unsigned int evalColorExpr(Expression* expr, unsigned int defaultValue = 0xFFFFFF);
    // Returns float value from a NumberLiteral/FloatLiteral expression, or defaultValue on failure
    static float evalFloatExpr(Expression* expr, float defaultValue = 0.0f);
    // Render a single Xraphics object (x3d.* / x2d.*) as a 2D projection
    void codegenXraphicsObject(XraphicsObjectStatement* obj, int windowId, int index, bool emitCamera);
};

}

#endif
