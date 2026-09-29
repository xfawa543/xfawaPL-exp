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
#include <unordered_set>
#include <set>
#include <vector>
#include <string>
#include <climits>

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

    // EXP `fate`: per-fated-variable destiny + persistent recovery floor.
    // Both are i64 allocas created once (in the entry block) when the `fate`
    // statement executes; the floor is updated monotonically by each recovery
    // so every future recovery result stays >= the highest value fate has ever
    // pulled the variable up to. Cleared together with `locals` per function.
    struct FateSlot {
        llvm::AllocaInst* destiny = nullptr; // the destiny value (i64)
        llvm::AllocaInst* floor = nullptr;   // highest recovery result so far (i64)
    };
    std::map<std::string, FateSlot> fateSlots;
    llvm::Function* fateRecoverFn = nullptr; // shared i64 fate recovery helper
    llvm::Function* getFateRecoverFunction();

    // EXP `dual`: names of variables whose existence was split into two selves,
    // mapped to the SECOND self's storage slot. The original self keeps living
    // in `locals[name]` (and is what a plain read of `x` sees). `x[0]` reads
    // the original self, `x[1]` the second. Cleared together with `locals`
    // per function so a split from one function never leaks into another.
    std::map<std::string, llvm::AllocaInst*> dualEchoVars;

    // EXP `disposable`: per-variable runtime layer stack. `disposable a = v`
    // pushes a layer (its remaining reads stored in `counts`, its value in
    // `values`); a plain read of `a` scans the stack from the top down for the
    // first live layer and consumes it AFTER the value was read. Once the stack
    // is empty the read falls back to the base alloca (`locals[name]`) if one
    // exists, otherwise it is a runtime "undefined / unavailable" error. A
    // normal assignment stores -1 into `top` to clear every layer.
    enum { kDisposableMaxLayers = 64 };
    struct DisposableState {
        llvm::AllocaInst* top = nullptr;          // i64 stack pointer, -1 = empty
        llvm::AllocaInst* counts = nullptr;       // [kDisposableMaxLayers x i64]
        llvm::AllocaInst* values = nullptr;       // [kDisposableMaxLayers x elemTy]
        llvm::Type* elemTy = nullptr;             // uniform layer value type
    };
    std::map<std::string, DisposableState> disposableVars;
    llvm::Value* codegenDisposableRead(const std::string& name, llvm::AllocaInst* baseAlloca);
    void emitRuntimeError(const std::string& message);
    void emitDisposableGuard(const std::string& funcName);

    // EXP `interest`: per-variable interest rules. Each `¥`/`$` statement births
    // the variable as a FLOAT alloca in `locals` and appends one rule. Every
    // read loads the current float value, returns it to the expression, then
    // applies all rules in definition order and stores the grown value back.
    //   - simple (¥) rules carry a fixed per-read amount = value * rate,
    //     computed (in IR, at runtime) when the rule was created.
    //   - compound ($) rules carry only the rate; each read does a += a * rate.
    struct InterestRule {
        bool isCompound = false;
        double rate = 0.0;
        llvm::Value* amount = nullptr; // simple-interest only: fixed per-read addend
    };
    struct InterestState {
        std::vector<InterestRule> rules;
    };
    std::map<std::string, InterestState> interestVars;
    llvm::Value* codegenInterestRead(const std::string& name, llvm::AllocaInst* baseAlloca);

    // EXP `kill[x]`: runtime invalidation of a source line. Each killed line's
    // statement is skipped on every later execution. Backing store is a single
    // byte-per-line table (internal global) indexed by 1-based source line.
    llvm::GlobalVariable* killFlags = nullptr;
    llvm::Value* getKillFlagsGlobal();

    // EXP `censer[x]`: texts whose exact print output terminates the program
    // (compared against the console output of each print statement).
    std::vector<std::string> censoredTexts;

    // EXP `noclip`: names of variables that have fallen into the backrooms.
    // A variable keeps its value but its reads become non-deterministic until
    // it returns to reality. `shuffleback` reshuffles their values.
    std::set<std::string> backroomVars;
    llvm::Value* codegenBackroomRead(const std::string& name, llvm::AllocaInst* alloca);

    // EXP `wrong`: one runtime guard per `wrong` statement. When the wrong
    // executes and its condition is true — or any later assignment to a watched
    // variable makes it true — the guard perturbs the participant (root leaf)
    // variables by the minimal amount and recomputes the derived chain until
    // the forbidden result no longer exists. Per-function state, except the
    // name counter which is module-wide (guards live on forever, like kill).
    struct WrongGuardData {
        llvm::Function* guard = nullptr;      // void guard(<varTy>* per closure var)
        std::vector<std::string> closure;     // involved variables (source order)
        xfawa::Expression* condExpr = nullptr; // condition to re-check after writes
    };
    std::vector<WrongGuardData> wrongSlots;
    std::map<std::string, std::vector<int>> wrongWatchedNames; // var -> slot indices
    std::map<std::string, xfawa::AssignmentStatement*> functionProducers; // name -> last scalar producer
    std::set<std::string> currentFuncParams;  // roots must not be function parameters
    int wrongGuardCounter = 0;                // module-wide guard name counter

    llvm::Value* codegen(WrongStatement* stmt);
    void emitWrongGuard(xfawa::WrongStatement* stmt,
                        const std::vector<std::string>& closure,
                        const std::vector<std::string>& roots,
                        const std::vector<std::string>& derived,
                        const std::map<std::string, xfawa::Expression*>& producerOf);
    void collectWrongProducers(xfawa::Statement* stmt);

    llvm::Value* codegen(NoclipStatement* stmt);
    llvm::Value* codegen(ShufflebackStatement* stmt);

    // EXP `zombie`: names of variables carrying the plague. Static, linear state
    // (like fate/noclip): `zombie a` marks a, and every later arithmetic step
    // that touches a — directly, as an operand — collapses to a's value. Each
    // variable that takes part in such a step is overwritten with that value and
    // marked in turn. Cleared together with `locals` per function.
    std::set<std::string> zombieVars;
    bool zombieAnnihilates(Expression* expr);
    std::string zombieSourceName(Expression* expr);
    llvm::Value* codegen(ZombieStatement* stmt);

    // EXP `valuable`: first-class code. A code value is a raw token slice kept
    // in `fragmentStore`; `codeVarFragments` maps a variable name to its slice.
    // Nothing is compiled or run until the value is executed at a use point, and
    // every use point gets its own inner function, so the same code can behave
    // differently at different points. `fragmentStore` is module-wide; the
    // variable map is per function.
    std::map<std::string, int> codeVarFragments;
    std::vector<std::vector<Token>> fragmentStore;
    int valuableCounter = 0;

    int internFragment(std::vector<Token> toks);
    bool isCodeExpr(Expression* expr);
    bool looksLikeCodeExpr(Expression* expr);
    int resolveCodeFragment(Expression* expr, bool& ok);
    std::vector<Token> expandCodeRefs(const std::vector<Token>& toks, int depth);
    std::vector<std::pair<std::string, bool>> collectFragmentRefs(const std::vector<Token>& toks);
    std::vector<Token> expressionToTokens(Expression* expr);
    VarType inferFragmentTargetType(const std::vector<std::unique_ptr<Statement>>& stmts,
                                    const std::string& name);
    std::unique_ptr<Statement> parseFragment(const std::vector<Token>& toks, int& fragId);
    llvm::Value* emitValuableRun(int fragId, const std::string& targetName, bool wantResult);
    llvm::Value* codegen(ValuableCallExpression* expr);
    int buildInjectFragment(ValuableInjectExpression* expr, bool& ok);
    llvm::Value* codegen(ValuableUseStatement* stmt);

    // EXP `valuable`: code values across a function boundary. A code value has no
    // runtime form, so it can only travel by being compiled into the callee:
    //   - as an argument: the call site builds a fresh copy of the callee with the
    //     code spliced into its body tokens, and calls that copy (a normal call).
    //     A variable the code mentions is not visible inside the callee, so the
    //     call site also passes a copy of it and stores the result back after the
    //     call, exactly like a fragment run does;
    //   - as a return value: a function whose whole body is `return <code>` is a
    //     code factory, and calling it inlines the code it returns.
    // `funcAsts` maps a function name to its declaration, which is where the body
    // tokens and the parameter list come from.
    std::map<std::string, Function*> funcAsts;
    // Parameters that some call site hands a code value to. Such a function is
    // only ever called with code, so the function itself is never emitted.
    std::map<std::string, std::set<int>> codeParamIndices;
    // A variable of the use point that a specialized copy works on a copy of.
    struct CodeBinding {
        std::string name;
        VarType type;
        int64_t arrayLen;
    };
    std::map<std::string, std::vector<CodeBinding>> codeArgBindings;
    // Guards the "is this function a code factory" question against a factory that
    // reaches itself.
    std::set<std::string> codeFactoryAnalysisInProgress;
    std::vector<std::string> codeFactoriesInProgress;
    std::set<std::string> codeSpecializationsInProgress;
    void collectFunctionAsts(Program* program);
    void collectCodeParamIndices(Program* program);
    void scanStmtsForCodeParams(Statement* stmt, std::set<std::string>& codeNames);
    void scanExprForCodeParams(Expression* expr, std::set<std::string>& codeNames);
    bool isCodeFactoryFunction(Function* func);
    bool isSelfRecursiveFactoryCandidate(Function* func);
    bool codeishCountingSelfCall(Expression* expr, const std::set<std::string>& codeNames,
                                 int depth, const std::string& selfName, bool* sawSelfCall);
    bool functionTakesCode(Function* func);
    bool staticIsCodeValue(Expression* expr, const std::set<std::string>& codeNames, int depth);
    void emitCodeBindingWriteBack(const std::string& funcName);
    llvm::Value* finishCallResult(llvm::CallInst* callInst, const std::string& typeKey);
    std::string functionQualifiedName(Function* func);
    bool substituteCodeParams(Function* func, const std::vector<int>& codeArgFragments,
                              std::vector<Token>& outTokens, bool& ok);
    llvm::Function* specializeCodeFunction(Function* func, const std::vector<int>& codeArgFragments,
                                           const std::vector<CodeBinding>& bindings, bool& ok);
    llvm::Value* codegenCodeArgumentCall(CallExpression* expr, llvm::Function* callee,
                                         const std::string& funcName,
                                         const std::vector<bool>& argIsCode);
    int resolveFactoryCall(CallExpression* expr, bool& ok);

    // EXP `value`: functions known to have no return statement (void). Used to
    // reject `a = value foo()` at compile time. Collected lazily.
    std::unordered_set<std::string> voidFuncNames;
    bool voidFuncsCollected = false;
    void collectVoidFunctions(Program* program);

    llvm::Value* codegen(KillStatement* stmt);
    llvm::Value* codegen(CenserStatement* stmt);
    llvm::Value* codegen(ValueExpression* expr);
    llvm::Value* codegenFuK(xfawa::BinaryOp* expr);

    llvm::AllocaInst* createAllocaInEntry(llvm::Type* type, const std::string& name);
    llvm::AllocaInst* birthLocalAlloca(const std::string& name, llvm::Value* value);
    void storeValueNormalized(llvm::Value* value, llvm::AllocaInst* alloca);
    void emitFateRecovery(const FateSlot& slot, llvm::AllocaInst* varAlloca);
    llvm::Value* codegen(FateStatement* stmt);
    llvm::Value* codegen(EnvyStatement* stmt);
    llvm::Value* codegenRandomBinaryOp(xfawa::BinaryOp* expr);
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool hasMainFunction;
    bool hasWindowStatements;
    bool usesRandomBuiltin;
    bool hasBsod;
    bool hasEllipsisRandSeeded = false;   // only seed `rand()` once per program

    // EXP `...`: each `...` picks one safe function and REALLY calls it with
    // arguments generated from its actual signature. This block holds the
    // candidate list (collected once, right after the function declarations
    // are emitted) plus the per-trampoline runtime machinery.
    enum { kRandomCallMaxDepth = 32 };
    struct RandomCallCandidate {
        std::string name;      // LLVM symbol name, for diagnostics
        llvm::Function* callee;
        bool nullPtrArg = false;  // builtins like time() expect a NULL pointer
    };
    std::vector<RandomCallCandidate> randomCallCandidates;
    bool randomCallTableEmitted = false;
    llvm::GlobalVariable* randomCallDepth = nullptr;
    llvm::GlobalVariable* randomCallTable = nullptr;

    void collectRandomCallCandidates(Program* program);
    bool bodyIsDangerous(const Function* func) const;
    llvm::Function* getRandFunction();
    void emitRandomCallSeedOnce();
    llvm::Value* emitDriftRandomValue(VarType type, llvm::Type* paramType,
                                      const Function::DriftConfig& cfg, int index);
    llvm::Value* codegenDriftedSelfCall(llvm::Function* callee, CallExpression* expr,
                                        const std::string& funcName);
    void emitRandomStringFill(llvm::GlobalVariable* buffer, int index);
    llvm::Function* createRandomCallTrampoline(const RandomCallCandidate& cand, int index);
    llvm::Value* emitRandomCallDispatch();

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
    bool unSleepDisabled = false;

    // EXP `believe`: normalized left-expression key -> believed result.
    std::unordered_map<std::string, int64_t> beliefMap;

    // EXP `deny`: the stored value is untouched, but the program no longer
    // acknowledges the fact. Keyed "name=value"; a matching `x == v` reads false
    // and a matching `x != v` reads true from then on.
    std::set<std::string> denyFacts;

    // EXP `deny` / `regret`: propositions the program refuses to believe any
    // more. `deny "a op b = r"` erases the `believe` rule, `regret "..."` does the
    // same with a word that means admitting the mistake, and `regret all` empties
    // the table.
    llvm::Value* codegen(DenyStatement* stmt);
    llvm::Value* codegen(RegretStatement* stmt);
    bool eraseBelief(const std::string& raw, const char* keyword);
    // True when `x == v` / `x != v` hits a denied fact.
    bool denyFactMatches(const std::string& name, int64_t value) const;

    // EXP `doubt` is resolved by the lie pass before codegen; a `doubt` that
    // survives to here had no lie to break.
    llvm::Value* codegen(DoubtStatement* stmt);

    // EXP `env`: functions listed in an `env` block carry one extra value that
    // calls between them pass on by themselves. The pass below runs on the
    // program before codegen; at codegen time an env function's carried value is
    // an ordinary first parameter and `env X = v` is an assignment to it.
    llvm::Value* codegen(EnvBlockStatement* stmt);
    llvm::Value* codegen(EnvAssignStatement* stmt);
    llvm::Value* codegen(ReactionStatement* stmt);
    // EXP structs / member access / compound assignment.
    llvm::Value* codegen(StructDeclaration* stmt);
    llvm::Value* codegen(StructCreationStatement* stmt);
    llvm::Value* codegen(MemberExpression* expr);
    llvm::Value* codegen(MemberAssignmentStatement* stmt);
    bool reactionNumeric(VarType t);
    // EXP structs / member access / compound assignment helpers.
    llvm::Value* applyAssignOp(llvm::Value* cur, llvm::Value* v, llvm::Type* ty, AssignOp op);
    llvm::Value* codegenFieldPtr(const std::string& structName, llvm::Value* basePtr,
                                 const std::string& field, llvm::Type** fieldTy);
    // EXP `env`: the values the function being generated carries, in block order.
    std::vector<std::pair<std::string, std::string>> currentEnvCarried;
    // EXP structs: struct name -> LLVM type, and its fields in order.
    std::map<std::string, llvm::StructType*> structTypes;
    std::map<std::string, std::vector<std::pair<std::string, llvm::Type*>>> structFields;
    // EXP structs: which local/param slot holds which struct (STRUCT-typed vars).
    std::map<std::string, std::string> localStructOf;

    // EXP `do`: statements already "hoisted" to run unconditionally (escape
    // from enclosing conditional/loop branches). Pointers recorded here are
    // skipped when their original (in-branch) position is generated later.
    std::unordered_set<const Statement*> hoistedDo;

    // Emit the inner statement of a `do`, bypassing any `un` disabling.
    void emitDoInner(DoStatement* stmt);
    // Recursively find `do` statements inside a condition/loop body and emit
    // them unconditionally now, so they ignore the surrounding branch.
    void hoistDoFromBranch(Statement* stmt);

    // EXP `come`: a reverse goto. `come` records a landing position; when the
    // statement on its target physical source line executes, control jumps back
    // to the come's landing. Data collected per xfawa function:
    struct ComeRecord {
        int comeLine = 0;      // physical line of the come statement
        int targetLine = 0;    // physical line that triggers the jump
        ComeStatement* stmt = nullptr; // for the optional `if(cond)` condition
    };
    // Same-unit statement lines (never generated): try block, loop/button/
    // nested-fn bodies are recorded as foreign and cannot be come targets.
    struct ComeScan {
        std::set<int> validLines;            // statement start lines in this unit
        std::map<int, NodeType> lineNode;    // start line -> node type (terminator reject)
        std::vector<ComeRecord> comes;       // same-unit comes
        std::set<int> comeLines;             // come statement lines (not valid targets)
        std::vector<std::string> errors;     // ill-placed come errors (e.g. inside loop)
        int minLine = INT_MAX;
        int maxLine = 0;
    };

    std::vector<ComeRecord> functionComes;   // per-function (current codegen(Function*))
    std::unordered_map<int, llvm::BasicBlock*> comeLandingBlocks; // comeLine -> landing
    std::unordered_map<int, int> comeTargetPick;      // targetLine -> comeLine (smallest wins)
    std::unordered_map<int, bool> comeTargetIntercepted; // targetLine -> jump emitted
    std::unordered_set<int> comePlacementDone;        // comeLine already wired up

    struct FunctionSpan { std::string name; int start; int end; };
    std::vector<FunctionSpan> functionLineSpans; // all functions, for cross-function errors

    // EXP `drift`: random recursive parameters.
    std::string currentFuncName;  // LLVM name of the function being compiled
    // funcName -> per-param set of VarTypes observed at every call site.
    std::map<std::string, std::vector<std::set<VarType>>> driftSeenParamTypes;
    // funcName -> config of the drift function currently being compiled.
    std::map<std::string, Function::DriftConfig> driftConfigs;
    llvm::GlobalVariable* driftDepthGlobal = nullptr; // lazily created counter

    void scanFunctionBody(const Statement* stmt, ComeScan& scan, bool inForeignUnit) const;
    void placeComeLanding(int comeLine);
    void maybeEmitComeJump(Statement* stmt);
    
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
    llvm::Value* codegen(OLiteralExpression* expr);
    llvm::Value* codegen(ParadoxExpression* expr);
    llvm::Value* codegen(GhostExpression* expr);
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
    llvm::Value* codegen(PleaseNoticeStatement* stmt);
    llvm::Value* codegen(ShutupStatement* stmt);
    llvm::Value* codegen(EllipsisStatement* stmt);
    llvm::Value* codegen(SleepStatement* stmt);
    llvm::Value* codegen(WrathStatement* stmt);
    llvm::Value* codegen(ParadoxStatement* stmt);
    llvm::Value* codegen(DejaStatement* stmt);
    llvm::Value* codegen(PinocchioStatement* stmt);
    llvm::Value* codegen(DualStatement* stmt);
    llvm::Value* codegen(IndexedAssignmentStatement* stmt);
    llvm::Value* codegen(DisposableStatement* stmt);
    llvm::Value* codegen(InterestStatement* stmt);
    llvm::Value* codegen(TryExpectStatement* stmt);
    llvm::Value* codegen(SorryStatement* stmt);
    llvm::Value* codegen(ComeStatement* stmt);
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
