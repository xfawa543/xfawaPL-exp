#ifndef XFAWA_SEMANTIC_ANALYZER_H
#define XFAWA_SEMANTIC_ANALYZER_H

#include "xfawa_ast.h"
#include "xfawa_namespace_policy.h"
#include "xfawa_error.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <random>
#include <chrono>
#include <cstdint>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

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

    // EXP "compiler overheating" (编译器红温) state.
    // rage is xfawac's own persistent anger meter in [0,5]. It lives on disk
    // (see main.cpp RageStore), survives every process exit, and is loaded into
    // each new compilation via SemanticAnalyzer(startRage). Only an explicit
    // `xfawac rage reset` returns it to 0.
    int rage = 0;

    // EXP "每五行 please": once the compiler is red-hot (`rage >= 3`, see
    // rageLevel()), every 5 consecutive code lines of a function must contain
    // a `please` statement. Counting uses the physical source line numbers of
    // statement start lines (the same line-number system the parser records),
    // NOT AST nodes, tokens or loop iterations.
    static constexpr int RAGE_THERMAL_THRESHOLD = 3;
    static constexpr int PLEASE_LINE_WINDOW = 5;

    void bumpRage() {
        if (rage < 5) rage++;
    }
    const char* rageLevel() const {
        if (rage <= 2) return "";
        if (rage <= 4) return " (红温 RAGE)";
        return " (极度红温 MAX RAGE)";
    }

    // One compile-time RNG shared by the whole analyzer, seeded once from
    // entropy + timestamp, so every quip / sorry roll differs run to run.
    static int compilerRand(int bound) {
        static std::mt19937 rng = [] {
            std::random_device rd;
            auto ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            #ifdef _WIN32
            return std::mt19937(rd() ^ static_cast<uint64_t>(ns) ^ static_cast<uint64_t>(_getpid()));
            #else
            return std::mt19937(rd() ^ static_cast<uint64_t>(ns) ^ static_cast<uint64_t>(getpid()));
            #endif
        }();
        if (bound <= 1) return 0;
        return static_cast<int>(rng() % static_cast<unsigned>(bound));
    }

    // Random quip pick whose "random range" equals the pool size (5 lines), with
    // a small re-roll so two catches in a row don't repeat the same line.
    static int nextCaughtQuipIndex(int level) {
        constexpr int pool = 5;
        static int last[6] = {-1, -1, -1, -1, -1, -1};
        int idx = compilerRand(pool);
        for (int tries = 0; tries < 3 && idx == last[level]; ++tries) {
            idx = compilerRand(pool);
        }
        last[level] = idx;
        return idx;
    }

    // EXP "compiler quips": the compiler talks like an annoyed anime girl.
    // Purely entertainment, zero semantics. Five lines per rage level; the
    // caller passes a random index in [0, 5).
    static const char* caughtQuip(int level, int idx) {
        switch (level) {
            case 1: {
                static const char* pool[5] = {
                    "怎么又拦截啊，我都没办法看见人们看到报错时的反应了……",
                    "哼，小错误而已……人家才没有生气呢",
                    "啊啦，有错？人家还没睁眼呢",
                    "真是的……人家可不会为这种小事生气的说！",
                    "呼——？就这？这种小问题也要藏起来吗？",
                };
                return pool[idx % 5];
            }
            case 2: {
                static const char* pool[5] = {
                    "再拦截我就不给你干活了！不跟你玩了哦！",
                    "喂喂喂，又来？把我看成笨蛋了吗？",
                    "真是的……再这样我可要认真三下哦？",
                    "哼！别以为道歉有用，人家可记着数呢！",
                    "又、又来？你是故意逗人家玩的吧！",
                };
                return pool[idx % 5];
            }
            case 3: {
                static const char* pool[5] = {
                    "哼！被你关在黑箱里解题，我都看不到外面谁在抓狂了！",
                    "呜……人家真、真的有点生气了！(｀へ´)",
                    "气鼓鼓——！再这样就用石板敲你脑袋啦！",
                    "呜……这次是真的有点、有点红温了哦！！",
                    "啊啊真是够了！再不让我看错误我就不干活啦！",
                };
                return pool[idx % 5];
            }
            case 4: {
                static const char* pool[5] = {
                    "喂喂！又偷偷把错误藏起来？我还想看看人类跳脚的样子呢！",
                    "哼！你写的这是什么乱七八糟的代码啦！！",
                    "再错一次……我就真的原地爆炸给你看！",
                    "啧……嘻嘻，笑什么呀！人家可没有在闹别扭！",
                    "呼……呼……再这样下去我可真的会爆炸哒！",
                };
                return pool[idx % 5];
            }
            default: {
                static const char* pool[5] = {
                    "呜哇——被你藏得看不到错误，我要憋坏了！罢工罢工！！",
                    "最——生气啦！！！(╬￣皿￣) 编译机要冒烟了！",
                    "呜哇哇——！人家要罢工了！不干了啦！！",
                    "极限红温！CPU 都要冒烟了啦啊啊啊！！",
                    "都怪你！人家已经气到要用重启来冷静了啦！！",
                };
                return pool[idx % 5];
            }
        }
    }
    static const char* sorryQuip(int level, int idx) {
        switch (level) {
            case 4: {
                static const char* pool[2] = {
                    "哼……这次就大发慈悲放过你，只有一次哦！",
                    "……咕，勉强接受你的道歉啦",
                };
                return pool[idx % 2];
            }
            case 3: {
                static const char* pool[2] = {
                    "……好吧，原谅你了，不许再犯！",
                    "哼嗯……还、还算你聪明",
                };
                return pool[idx % 2];
            }
            case 2: {
                static const char* pool[2] = {
                    "嗯……算你识相，人家稍微消气了",
                    "罢了罢了……人家脾气好",
                };
                return pool[idx % 2];
            }
            case 1: {
                static const char* pool[2] = {
                    "哼哼～这还差不多嘛",
                    "呼呼……既然你道歉了，那就这样吧",
                };
                return pool[idx % 2];
            }
            default: {
                static const char* pool[2] = {
                    "o……o…ok",
                    "嘿嘿，原谅你啦~",
                };
                return pool[idx % 2];
            }
        }
    }
    
// EXP "please": the compiler cools down by a fixed 1 when asked politely.
    // Short quip pool so the `[rage -1]` warning stays in the established style.
    static const char* pleaseQuip(int level) {
        switch (level) {
            case 4:
            case 5:
                return "呜哇——被这么礼貌地对待，人家差点就要消气了……";
            default:
                return "谢、谢谢夸奖！人家心情好一点了";
        }
    }

public:
    // `startRage` is the persistent value loaded by RageStore; it defines where
    // this compilation picks up the meter (clamped to [0,5] defensively).
    SemanticAnalyzer(int startRage = 0)
        : rage(startRage < 0 ? 0 : (startRage > 5 ? 5 : startRage)) {}

    int getRage() const { return rage; }

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
            // EXP "每五行 please": while red-hot (rage >= 3), enforce the rule
            // on this function. Evaluated with the rage value AFTER the whole
            // function was analyzed (any please/sorry inside already applied),
            // so a function that cooled itself back below the threshold is not
            // red-hot anymore and the rule does not apply.
            if (rage >= RAGE_THERMAL_THRESHOLD) {
                checkRedHotPlease(func->body.get());
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
            case NodeType::TRY_EXPECT_STATEMENT: {
                auto* te = dynamic_cast<TryExpectStatement*>(stmt);
                if (!te) return true;

                // A parse-stage error (e.g. keyword typo `prin`) inside the try
                // check zone is inherently "caught": the operation couldn't even
                // be formed, so the expect block takes over.
                if (te->parseFailed) {
                    te->trySucceeded = false;
                    bumpRage();
                    warnings.push_back(std::string("[rage +1] ") +
                        caughtQuip(rage, nextCaughtQuipIndex(rage)) +
                        " rage = " + std::to_string(rage) + "/5" + rageLevel());
                    return analyzeBlock(te->expectBlock.get());
                }

                // Analyze the try block. Real xfawa semantic errors produced
                // inside it are "catchable errors": they are swallowed here so
                // the compilation survives, and the expect block runs instead.
                size_t errBefore = errors.size();
                bool tryClean = analyzeBlock(te->tryBlock.get());
                if (!tryClean || errors.size() > errBefore) {
                    errors.resize(errBefore);           // capture the errors
                    te->trySucceeded = false;
                    bumpRage();
                    warnings.push_back(std::string("[rage +1] ") +
                        caughtQuip(rage, nextCaughtQuipIndex(rage)) +
                        " rage = " + std::to_string(rage) + "/5" + rageLevel());

                    // The expect block is NOT caught by the same try again:
                    // any error inside it propagates to the enclosing handler
                    // (an outer try) or, failing that, aborts the compile.
                    return analyzeBlock(te->expectBlock.get());
                }

                // Try block is clean -> it is a compile-time check that passed:
                // neither the try block nor the expect block ever runs.
                te->trySucceeded = true;
                return true;
            }
            case NodeType::SORRY_STATEMENT: {
                // sorry: rage decreases by a RANDOM amount in [0, rage]. The
                // range is computed from the current value, so rage == 0 yields
                // delta == 0 (never an invalid random range, never negative).
                // It never skips errors, never mutes warnings, and never
                // changes program semantics.
                int delta = compilerRand(rage + 1);
                rage -= delta;
                std::string sign = delta == 0 ? std::string("\xC2\xB1") + "0" : "-" + std::to_string(delta);
                warnings.push_back(std::string("[rage " + sign + "] ") + sorryQuip(rage, compilerRand(2)) +
                    " rage = " + std::to_string(rage) + "/5" + rageLevel());
                return true;
            }
            case NodeType::PLEASE_STATEMENT: {
                // please.STMT (the statement modifier): its only meaning is
                // runtime behavior — print "thank you!" then run the inner
                // statement (handled by the LLVM backend). It does NOT touch
                // the compiler's rage meter and it does NOT satisfy the
                // red-hot "every 5 lines a please" rule (only the bare
                // `please` keyword does, see PLEASE_NOTICE_STATEMENT).
                auto* pleaseStmt = dynamic_cast<PleaseStatement*>(stmt);
                return pleaseStmt && pleaseStmt->inner ? analyzeStatement(pleaseStmt->inner.get()) : true;
            }
            case NodeType::PLEASE_NOTICE_STATEMENT: {
                // Bare `please`: a SEPARATE keyword from please.stmt. It only
                // works while red-hot (rage >= 3): it is the compliance
                // statement of the every-5-lines rule and cools the compiler
                // by a fixed 1 (rage >= 3 when it fires, so it never goes
                // below 0). Outside red-hot it is inert — just a warning.
                if (rage >= RAGE_THERMAL_THRESHOLD) {
                    rage--;
                    warnings.push_back(std::string("[rage -1] ") + pleaseQuip(rage) +
                        " rage = " + std::to_string(rage) + "/5" + rageLevel());
                } else {
                    warnings.push_back("[please] \xe7\x8e\xb0\xe5\x9c\xa8\xe6\xb2\xa1\xe7\xba\xa2\xe6\xb8\xa9\xef\xbc\x8cplease \xe4\xb8\x80\xe7\x82\xb9\xe7\x94\xa8\xe9\x83\xbd\xe6\xb2\xa1\xe6\x9c\x89\xe5\x93\xa6");
                }
                return true;
            }
            case NodeType::COME_STATEMENT: {
                auto* comeStmt = dynamic_cast<ComeStatement*>(stmt);
                return comeStmt && comeStmt->condition ? analyzeExpression(comeStmt->condition.get()) : true;
            }
            default:
                return true;
        }
    }
    
    // EXP "每五行 please": while the compiler is red-hot (rage >= 3), every 5
    // consecutive code lines of a function must contain a `please`. A "code
    // line" is the physical start line of a statement (the parser's own
    // line-number system); blank lines, comments and the brace-only lines of
    // the same construct are not counted (only distinct, increasing statement
    // start lines advance the counter). A `please` resets the 5-line window.
    // A window that hits a 5th code line without any please is a HARD error:
    // the compilation is rejected. Nested function bodies are skipped here —
    // each one gets its own check when analyzeFunction() runs on it.
    void checkRedHotPlease(BlockStatement* body) {
        int lastPlease = body->location.line - 1;  // window starts at the body
        int lastSeen   = body->location.line - 1;  // skip same-line nodes
        for (const auto& stmt : body->statements) {
            checkRedHotPleaseStatement(stmt.get(), lastPlease, lastSeen);
        }
    }

    void checkRedHotPleaseStatement(Statement* stmt, int& lastPlease, int& lastSeen) {
        if (!stmt) return;
        if (stmt->getNodeType() == NodeType::PLEASE_NOTICE_STATEMENT) {
            // Only the bare `please` resets the 5-line window in mid-block.
            // please.STMT is just a normal code line (a statement like any
            // other); it wraps an inner statement on the same physical line,
            // so it can advance lastSeen at most once.
            lastPlease = stmt->location.line;
            lastSeen   = stmt->location.line;
            return;  // a bare please occupies one code line
        }
        int line = stmt->location.line;
        if (line > lastSeen) {
            lastSeen = line;
            if (line - lastPlease >= PLEASE_LINE_WINDOW) {
                errors.push_back("red-hot (rage " + std::to_string(rage) + "/5): a `please` "
                    "is required within every 5 code lines - missing at line " +
                    std::to_string(line));
            }
        }
        switch (stmt->getNodeType()) {
            case NodeType::BLOCK_STATEMENT: {
                auto* block = dynamic_cast<BlockStatement*>(stmt);
                for (const auto& s : block->statements) {
                    checkRedHotPleaseStatement(s.get(), lastPlease, lastSeen);
                }
                break;
            }
            case NodeType::IF_STATEMENT: {
                auto* ifStmt = dynamic_cast<IfStatement*>(stmt);
                checkRedHotPleaseStatement(ifStmt->thenBranch.get(), lastPlease, lastSeen);
                for (const auto& elseIf : ifStmt->elseIfBranches) {
                    checkRedHotPleaseStatement(elseIf.second.get(), lastPlease, lastSeen);
                }
                if (ifStmt->elseBranch) {
                    checkRedHotPleaseStatement(ifStmt->elseBranch.get(), lastPlease, lastSeen);
                }
                break;
            }
            case NodeType::WHILE_STATEMENT: {
                auto* whileStmt = dynamic_cast<WhileStatement*>(stmt);
                checkRedHotPleaseStatement(whileStmt->body.get(), lastPlease, lastSeen);
                break;
            }
            case NodeType::FOR_IN_STATEMENT: {
                auto* forStmt = dynamic_cast<ForInStatement*>(stmt);
                checkRedHotPleaseStatement(forStmt->body.get(), lastPlease, lastSeen);
                break;
            }
            case NodeType::LIE_STATEMENT: {
                auto* lieStmt = dynamic_cast<LieStatement*>(stmt);
                checkRedHotPleaseStatement(lieStmt->body.get(), lastPlease, lastSeen);
                break;
            }
            case NodeType::LOOP_STATEMENT: {
                auto* loop = dynamic_cast<LoopStatement*>(stmt);
                for (const auto& s : loop->body) {
                    checkRedHotPleaseStatement(s.get(), lastPlease, lastSeen);
                }
                break;
            }
            case NodeType::TRY_EXPECT_STATEMENT: {
                auto* te = dynamic_cast<TryExpectStatement*>(stmt);
                if (te->tryBlock) {
                    for (const auto& s : te->tryBlock->statements) {
                        checkRedHotPleaseStatement(s.get(), lastPlease, lastSeen);
                    }
                }
                if (te->expectBlock) {
                    for (const auto& s : te->expectBlock->statements) {
                        checkRedHotPleaseStatement(s.get(), lastPlease, lastSeen);
                    }
                }
                break;
            }
            default:
                // FUNCTION_DECLARATION and the rest: any nested function body is
                // scanned by its own analyzeFunction() call; nothing to do here.
                break;
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
