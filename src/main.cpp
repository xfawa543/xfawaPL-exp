#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <functional>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <process.h>
#define mkdir(path) _mkdir(path)
#else
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "xfawa_types.h"
#include "xfawa_ast.h"
#include "xfawa_lexer.h"
#include "xfawa_parser.h"
#include "xfawa_llvm_codegen.h"
#include "xfawa_config.h"
#include "xfawa_mods_system.h"
#include "xfawa_error.h"
#include "xfawa_ast_transform.h"
#include "xfawa_semantic_analyzer.h"
#include "xfawa_transpiler.h"

static int g_debug = 0;
static bool g_keep_temp = false;
static bool g_emit_llvm = false;
static bool g_emit_asm = false;
static bool g_use_config = true;
static xfawa::OptimizationLevel g_opt_level = xfawa::OptimizationLevel::O2;
static bool g_opt_level_set = false;
static bool g_transpile_c = false;
static bool g_transpile_cpp = false;
static bool g_annotate = false;

namespace xfawa {
    int g_debug_global = 0;
}

const char* COMPILER_VERSION = "1.0.0-a.19";
const char* MODS_KERNEL_VERSION = "mods-a-1.0.3";

static xfawa::LogLanguage g_log_language = xfawa::LogLanguage::EN;

static const char* utf8(const char8_t* text) {
    return reinterpret_cast<const char*>(text);
}

xfawa::ErrorSystem* xfawa::ErrorReporter::instance = nullptr;

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (g_log_language == xfawa::LogLanguage::ZH) {
            std::cerr << utf8(u8"\u9519\u8bef\uff1a\u65e0\u6cd5\u6253\u5f00\u6587\u4ef6 ") << path << std::endl;
        } else {
            std::cerr << "Error: Could not open file " << path << std::endl;
        }
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool ensureDirectoryExists(const std::string& dirPath) {
#ifdef _WIN32
    int wchars_num = MultiByteToWideChar(CP_UTF8, 0, dirPath.c_str(), -1, NULL, 0);
    if (wchars_num <= 0) return false;
    
    std::wstring wdirPath(wchars_num, 0);
    MultiByteToWideChar(CP_UTF8, 0, dirPath.c_str(), -1, &wdirPath[0], wchars_num);
    
    std::wstring::size_type pos = 0;
    while ((pos = wdirPath.find(L'\\', pos + 1)) != std::wstring::npos) {
        std::wstring subdir = wdirPath.substr(0, pos);
        if (!subdir.empty() && subdir != L":") {
            CreateDirectoryW(subdir.c_str(), NULL);
        }
    }
    
    return CreateDirectoryW(wdirPath.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    struct stat st;
    if (stat(dirPath.c_str(), &st) == -1) {
        if (mkdir(dirPath.c_str(), 0755) == -1) {
            return false;
        }
    }
    return true;
#endif
}

void printUsage(const char* programName) {
    if (g_log_language == xfawa::LogLanguage::ZH) {
        printf("%s%s%s", utf8(u8"\u7528\u6cd5\uff1a"), programName, utf8(u8" [\u9009\u9879] <\u8f93\u5165\u6587\u4ef6>\n"));
        printf("%s", utf8(u8"\u9009\u9879\uff1a\n"));
        printf("%s", utf8(u8"  -o, --output <file>    \u6307\u5b9a\u8f93\u51fa\u6587\u4ef6\u540d\n"));
        printf("%s", utf8(u8"  -d, --debug            \u542f\u7528\u8c03\u8bd5\u8f93\u51fa\n"));
        printf("%s", utf8(u8"  -k, --keep             \u4fdd\u7559\u4e34\u65f6\u6587\u4ef6\n"));
        printf("%s", utf8(u8"  --emit-llvm            \u8f93\u51fa LLVM IR (.ll \u6587\u4ef6)\n"));
        printf("%s", utf8(u8"  --emit-asm             \u8f93\u51fa\u6c47\u7f16\u6587\u4ef6 (.asm \u6587\u4ef6)\n"));
        printf("%s", utf8(u8"  --transpile-c          \u8f6c\u8bd1\u4e3a C \u4ee3\u7801 (\u5b9e\u9a8c\u6027)\n"));
        printf("%s", utf8(u8"  --transpile-cpp        \u8f6c\u8bd1\u4e3a C++ \u4ee3\u7801 (\u5b9e\u9a8c\u6027)\n"));
        printf("%s", utf8(u8"  --annotate             \u7f16\u8bd1\u5668\u81ea\u52a8\u4e3a\u4ee3\u7801\u6dfb\u52a0\u6ce8\u91ca (\u5b9e\u9a8c)\n"));
        printf("%s", utf8(u8"  -O0                    \u5173\u95ed\u4f18\u5316\uff08\u7528\u4e8e\u8c03\u8bd5\uff09\n"));
        printf("%s", utf8(u8"  -O1                    \u542f\u7528\u57fa\u7840\u4f18\u5316\n"));
        printf("%s", utf8(u8"  -O2                    \u542f\u7528\u6807\u51c6\u4f18\u5316\uff08\u9ed8\u8ba4\uff09\n"));
        printf("%s", utf8(u8"  -O3                    \u542f\u7528\u6fc0\u8fdb\u4f18\u5316\n"));
        printf("%s", utf8(u8"  -c, --config <file>    \u6307\u5b9a\u914d\u7f6e\u6587\u4ef6\n"));
        printf("%s", utf8(u8"  -n, --no-config        \u4e0d\u4f7f\u7528\u914d\u7f6e\u6587\u4ef6\n"));
        printf("%s", utf8(u8"  -v, --version          \u663e\u793a\u7f16\u8bd1\u5668\u7248\u672c\n"));
        printf("%s", utf8(u8"  -h, --help             \u663e\u793a\u5e2e\u52a9\u4fe1\u606f\n"));
        printf("%s", utf8(u8"  rage                   \u67e5\u770b xfawac \u6301\u4e45\u5316\u7684\u7ea2\u6e29\u503c\uff08\u4f8b\uff1arage: 3/5\uff09\n"));
        printf("%s", utf8(u8"  rage reset             \u5c06\u7ea2\u6e29\u503c\u91cd\u7f6e\u4e3a 0\n"));
    } else {
        printf("Usage: %s [options] <input_file>\n", programName);
        printf("Options:\n");
        printf("  -o, --output <file>    Specify output file name\n");
        printf("  -d, --debug            Enable debug output\n");
        printf("  -k, --keep             Keep temporary files\n");
        printf("  --emit-llvm            Emit LLVM IR (.ll file)\n");
        printf("  --emit-asm             Emit assembly (.asm file)\n");
        printf("  --transpile-c          Transpile to C code (experimental)\n");
        printf("  --transpile-cpp        Transpile to C++ code (experimental)\n");
        printf("  --annotate             Have the compiler annotate the code (experimental)\n");
        printf("  -O0                    Disable optimization (for debugging)\n");
        printf("  -O1                    Enable basic optimization\n");
        printf("  -O2                    Enable standard optimization (default)\n");
        printf("  -O3                    Enable aggressive optimization\n");
        printf("  -c, --config <file>    Specify config file\n");
        printf("  -n, --no-config        Don't use config file\n");
        printf("  -v, --version          Show compiler version\n");
        printf("  -h, --help             Show this help message\n");
        printf("  rage                   Show the persistent rage meter (e.g. rage: 3/5)\n");
        printf("  rage reset             Reset the persistent rage meter to 0\n");
    }
    printf("\n");
}

void printVersion() {
    if (g_log_language == xfawa::LogLanguage::ZH) {
        printf("%s\n", utf8(u8"xfawaPL \u7f16\u8bd1\u5668"));
        printf("%s%s\n", utf8(u8"\u7248\u672c\uff1a"), COMPILER_VERSION);
        printf("%s%s\n", utf8(u8"Mods \u5185\u6838\uff1a"), MODS_KERNEL_VERSION);
        printf("%s\n", utf8(u8"\u540e\u7aef\uff1aLLVM 21.1.8"));
    } else {
        printf("xfawaPL Compiler\n");
        printf("Version: %s\n", COMPILER_VERSION);
        printf("Mods Kernel: %s\n", MODS_KERNEL_VERSION);
        printf("Backend: LLVM 21.1.8\n");
    }
}

void printConfig(const xfawa::CompilerConfig& config) {
    if (config.log_language == xfawa::LogLanguage::ZH) {
        std::cout << utf8(u8"\u5f53\u524d\u914d\u7f6e\uff1a") << std::endl;
        std::cout << utf8(u8"  \u8c03\u8bd5\u4fe1\u606f\uff1a") << (config.debug_info ? utf8(u8"\u662f") : utf8(u8"\u5426")) << std::endl;
        std::cout << utf8(u8"  \u8b66\u544a\uff1a") << (config.warnings ? utf8(u8"\u662f") : utf8(u8"\u5426")) << std::endl;
        std::cout << utf8(u8"  \u663e\u793a\u8b66\u544a\u7c7b\u578b\uff1a") << (config.show_warning_types ? utf8(u8"\u662f") : utf8(u8"\u5426")) << std::endl;
        std::cout << utf8(u8"  Xraphics \u65e5\u5fd7\uff1a") << (config.xraphics_log ? utf8(u8"\u662f") : utf8(u8"\u5426")) << std::endl;
        std::cout << utf8(u8"  \u8f93\u51fa LLVM IR\uff1a") << (config.emit_ll ? utf8(u8"\u662f") : utf8(u8"\u5426")) << std::endl;
        std::cout << utf8(u8"  \u8f93\u51fa\u6c47\u7f16\uff1a") << (config.emit_asm ? utf8(u8"\u662f") : utf8(u8"\u5426")) << std::endl;
        std::cout << utf8(u8"  \u4f18\u5316\u7b49\u7ea7\uff1a") << "O" << static_cast<int>(config.opt_level) << std::endl;
        std::cout << utf8(u8"  \u65e5\u5fd7\u8bed\u8a00\uff1a") << utf8(u8"\u4e2d\u6587") << std::endl;
        std::cout << utf8(u8"  \u8f93\u51fa\u76ee\u5f55\uff1a") << config.output_dir << std::endl;
        std::cout << utf8(u8"  \u4e2d\u95f4\u6587\u4ef6\u76ee\u5f55\uff1a") << config.intermediate_dir << std::endl;
    } else {
        std::cout << "Configuration:" << std::endl;
        std::cout << "  Debug info: " << (config.debug_info ? "yes" : "no") << std::endl;
        std::cout << "  Warnings: " << (config.warnings ? "yes" : "no") << std::endl;
        std::cout << "  Show warning types: " << (config.show_warning_types ? "yes" : "no") << std::endl;
        std::cout << "  Xraphics log: " << (config.xraphics_log ? "yes" : "no") << std::endl;
        std::cout << "  Emit LLVM IR: " << (config.emit_ll ? "yes" : "no") << std::endl;
        std::cout << "  Emit ASM: " << (config.emit_asm ? "yes" : "no") << std::endl;
        std::cout << "  Optimization level: O" << static_cast<int>(config.opt_level) << std::endl;
        std::cout << "  Log language: " << "English" << std::endl;
        std::cout << "  Output directory: " << config.output_dir << std::endl;
        std::cout << "  Intermediate directory: " << config.intermediate_dir << std::endl;
    }
}

std::vector<std::string> extractModImports(const std::string& source) {
    std::vector<std::string> mods;
    // Support %import "modname" and %import "modname.xfmod" (exclude .xfw files)
    std::regex importRegex("%import\\s+\"([^\"]+)\"");
    
    std::sregex_iterator it(source.begin(), source.end(), importRegex);
    std::sregex_iterator end;
    
    while (it != end) {
        std::string modName = (*it)[1].str();
        // Skip .xfw files - they are handled by extractXfwImports
        if (modName.size() >= 4 && modName.substr(modName.size() - 4) == ".xfw") {
            ++it;
            continue;
        }
        mods.push_back(modName);
        ++it;
    }
    
    return mods;
}

std::string removeImportStatements(const std::string& source) {
    std::regex importRegex("%import\\s+\"[^\"]+\"\\s*\\n?");
    return std::regex_replace(source, importRegex, "");
}

// Remove %import statements for mods (.xfmod) but keep .xfw library imports,
// which are handled separately by processXfw.
#ifdef XFAWA_MODS_DISABLED
std::string removeModImportStatements(const std::string& source) {
    std::regex importRegex("%import\\s+\"([^\"]+)\"\\s*\\n?");
    std::string result;
    std::sregex_iterator it(source.begin(), source.end(), importRegex);
    std::sregex_iterator end;
    size_t last = 0;
    for (; it != end; ++it) {
        std::string name = (*it)[1].str();
        bool isXfw = name.size() >= 4 && name.substr(name.size() - 4) == ".xfw";
        size_t start = static_cast<size_t>(it->position());
        size_t len = static_cast<size_t>(it->length());
        if (isXfw) {
            result += source.substr(last, start + len - last);
        }
        last = start + len;
    }
    result += source.substr(last);
    return result;
}
#endif

bool processMods(xfawa::ModsSystem& modsSystem, const std::string& source, std::string& processedSource) {
    // The mods system is disabled at the code level (see XFAWA_MODS_DISABLED in
    // xfawa_mods_system.h). The mods source is preserved and still compiled into
    // the compiler, but the feature provides no functionality. Mod imports
    // (%import "name") are still recognized and stripped so existing sources
    // remain compilable; mod loading, syntax modification, syntax expansion,
    // and public-function injection are all compiled out. Undefine
    // XFAWA_MODS_DISABLED to re-enable the system.
    std::vector<std::string> modImports = extractModImports(source);

#ifdef XFAWA_MODS_DISABLED
    processedSource = removeModImportStatements(source);
    if (!modImports.empty()) {
        std::cerr << "xfawa: note: the mods system is disabled in this build; "
                     "ignoring mod import(s): ";
        for (size_t i = 0; i < modImports.size(); ++i) {
            if (i > 0) {
                std::cerr << ", ";
            }
            std::cerr << modImports[i];
        }
        std::cerr << std::endl;
    }
    return true;
#else
    if (modImports.empty()) {
        processedSource = source;
        return true;
    }
    
    if (g_debug) {
        std::cout << "[debug] Found " << modImports.size() << " mod import(s)" << std::endl;
        for (const auto& mod : modImports) {
            std::cout << "[debug]   - " << mod << std::endl;
        }
    }
    
    for (const auto& modName : modImports) {
        if (!modsSystem.loadMod(modName)) {
            xfawa::ErrorReporter::get().addModError(0, 0, "Failed to load mod: " + modName);
            for (const auto& err : modsSystem.getErrors()) {
                xfawa::ErrorReporter::get().addModError(0, 0, err);
            }
            return false;
        }
        
        if (g_debug) {
            std::cout << "[debug] Loaded mod: " << modName << std::endl;
            std::cout << "[debug]   Modifications: " << modsSystem.getModifications().size() << std::endl;
            std::cout << "[debug]   Added syntaxes: " << modsSystem.getAddedSyntaxes().size() << std::endl;
            std::cout << "[debug]   Public functions: " << modsSystem.getPublicFunctions().size() << std::endl;
        }
    }
    
    processedSource = removeImportStatements(source);
    
    if (modsSystem.hasModifications()) {
        processedSource = modsSystem.applyModifications(processedSource);
        if (g_debug) {
            std::cout << "[debug] Applied syntax modifications" << std::endl;
        }
    }
    
    if (modsSystem.hasAddedSyntaxes()) {
        processedSource = modsSystem.expandSyntax(processedSource);
        if (g_debug) {
            std::cout << "[debug] Expanded added syntaxes" << std::endl;
        }
    }
    
    if (modsSystem.hasPublicFunctions()) {
        std::string publicFuncsCode;
        publicFuncsCode += "\n#_pub_funcs {\n";
        
        for (const auto& pubFunc : modsSystem.getPublicFunctions()) {
            std::string params;
            for (size_t i = 0; i < pubFunc.params.size(); i++) {
                params += pubFunc.params[i];
                if (i < pubFunc.params.size() - 1) {
                    params += ", ";
                }
            }
            if (pubFunc.ns.empty()) {
                std::string funcCode = "    fn " + pubFunc.name + "(" + params + ") { " + pubFunc.body + " }\n";
                publicFuncsCode += funcCode;
                if (g_debug) {
                    std::cout << "[debug] Injecting internal function: " << pubFunc.name << std::endl;
                }
            }
        }
        
        for (const auto& pubFunc : modsSystem.getPublicFunctions()) {
            if (!pubFunc.ns.empty()) {
                std::string params;
                for (size_t i = 0; i < pubFunc.params.size(); i++) {
                    params += pubFunc.params[i];
                    if (i < pubFunc.params.size() - 1) {
                        params += ", ";
                    }
                }
                std::string funcCode = "    fn " + pubFunc.ns + ":" + pubFunc.name + "(" + params + ") { " + pubFunc.body + " }\n";
                publicFuncsCode += funcCode;
                if (g_debug) {
                    std::cout << "[debug] Injecting public function: " << pubFunc.ns << ":" << pubFunc.name << std::endl;
                    std::cout << "[debug]   Body: " << pubFunc.body << std::endl;
                }
            }
        }
        
        publicFuncsCode += "}\n";
        processedSource = publicFuncsCode + processedSource;
        if (g_debug) {
            std::cout << "[debug] Injected functions" << std::endl;
        }
    }
    
    return true;
#endif
}

// ---- EXP-001 "executable comments" helpers ---------------------------------

static std::string trimWhitespace(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

// Collect statements in source order (used to map a comment to the statement
// that immediately follows it).
static void collectStatementsRec(xfawa::Statement* stmt, std::vector<xfawa::Statement*>& out) {
    if (!stmt) return;
    out.push_back(stmt);
    switch (stmt->getNodeType()) {
        case xfawa::NodeType::BLOCK_STATEMENT: {
            auto* b = static_cast<xfawa::BlockStatement*>(stmt);
            for (auto& s : b->statements) collectStatementsRec(s.get(), out);
            break;
        }
        case xfawa::NodeType::IF_STATEMENT: {
            auto* i = static_cast<xfawa::IfStatement*>(stmt);
            if (i->thenBranch) collectStatementsRec(i->thenBranch.get(), out);
            for (auto& ei : i->elseIfBranches) collectStatementsRec(ei.second.get(), out);
            if (i->elseBranch) collectStatementsRec(i->elseBranch.get(), out);
            break;
        }
        case xfawa::NodeType::WHILE_STATEMENT: {
            auto* w = static_cast<xfawa::WhileStatement*>(stmt);
            if (w->body) collectStatementsRec(w->body.get(), out);
            break;
        }
        case xfawa::NodeType::FOR_IN_STATEMENT: {
            auto* f = static_cast<xfawa::ForInStatement*>(stmt);
            if (f->body) collectStatementsRec(f->body.get(), out);
            break;
        }
        case xfawa::NodeType::LOOP_STATEMENT: {
            auto* l = static_cast<xfawa::LoopStatement*>(stmt);
            for (auto& s : l->body) collectStatementsRec(s.get(), out);
            break;
        }
        default:
            break;
    }
}

// ---- EXP `lie`: AST transform ----------------------------------------------
// Rewrites variable reads to their "lie" value within the scope where a
// `lie x = v` statement appears. This keeps the real value untouched (a
// "virtual" change) and respects control flow: a lie inside `if (false) { ... }`
// only rewrites reads inside that block, so it cannot leak outside.

// Replace any `VariableExpression` whose name is in `lies` with its constant.
static std::unique_ptr<xfawa::Expression> lieRewriteExpr(
    std::unique_ptr<xfawa::Expression> e,
    const std::unordered_map<std::string, int64_t>& lies)
{
    if (!e) return nullptr;

    if (auto* v = dynamic_cast<xfawa::VariableExpression*>(e.get())) {
        auto it = lies.find(v->name);
        if (it != lies.end()) {
            return std::make_unique<xfawa::NumberLiteral>(it->second, v->location);
        }
        return e;
    }
    if (auto* b = dynamic_cast<xfawa::BinaryOp*>(e.get())) {
        b->left = lieRewriteExpr(std::move(b->left), lies);
        b->right = lieRewriteExpr(std::move(b->right), lies);
        return e;
    }
    if (auto* u = dynamic_cast<xfawa::UnaryOp*>(e.get())) {
        u->expr = lieRewriteExpr(std::move(u->expr), lies);
        return e;
    }
    if (auto* c = dynamic_cast<xfawa::CallExpression*>(e.get())) {
        for (auto& a : c->args) a = lieRewriteExpr(std::move(a), lies);
        return e;
    }
    if (auto* idx = dynamic_cast<xfawa::ArrayIndexExpression*>(e.get())) {
        idx->array = lieRewriteExpr(std::move(idx->array), lies);
        idx->index = lieRewriteExpr(std::move(idx->index), lies);
        return e;
    }
    if (auto* r = dynamic_cast<xfawa::ArrayRangeExpression*>(e.get())) {
        if (r->array) r->array = lieRewriteExpr(std::move(r->array), lies);
        r->start = lieRewriteExpr(std::move(r->start), lies);
        r->end = lieRewriteExpr(std::move(r->end), lies);
        return e;
    }
    if (auto* a = dynamic_cast<xfawa::ArrayLiteral*>(e.get())) {
        if (a->isRange) {
            a->rangeStart = lieRewriteExpr(std::move(a->rangeStart), lies);
            a->rangeEnd = lieRewriteExpr(std::move(a->rangeEnd), lies);
        } else {
            for (auto& el : a->elements) el = lieRewriteExpr(std::move(el), lies);
        }
        return e;
    }
    return e;
}

static void lieRewriteStmt(xfawa::Statement* stmt, std::unordered_map<std::string, int64_t> lies);

// Rewrite expressions inside a single leaf statement (assignment RHS, print arg,
// if/while condition, return value).
static void lieRewriteLeaf(xfawa::Statement* stmt, const std::unordered_map<std::string, int64_t>& lies) {
    if (auto* a = dynamic_cast<xfawa::AssignmentStatement*>(stmt)) {
        if (a->value) a->value = lieRewriteExpr(std::move(a->value), lies);
    } else if (auto* p = dynamic_cast<xfawa::PrintStatement*>(stmt)) {
        if (p->expr) p->expr = lieRewriteExpr(std::move(p->expr), lies);
    } else if (auto* r = dynamic_cast<xfawa::ReturnStatement*>(stmt)) {
        if (r->value) r->value = lieRewriteExpr(std::move(r->value), lies);
    } else if (auto* i = dynamic_cast<xfawa::IfStatement*>(stmt)) {
        if (i->condition) i->condition = lieRewriteExpr(std::move(i->condition), lies);
    } else if (auto* w = dynamic_cast<xfawa::WhileStatement*>(stmt)) {
        if (w->condition) w->condition = lieRewriteExpr(std::move(w->condition), lies);
    } else if (auto* f = dynamic_cast<xfawa::ForInStatement*>(stmt)) {
        if (f->iterable) f->iterable = lieRewriteExpr(std::move(f->iterable), lies);
    } else if (auto* es = dynamic_cast<xfawa::ExpressionStatement*>(stmt)) {
        if (es->expr) es->expr = lieRewriteExpr(std::move(es->expr), lies);
    }
}

static void lieRewriteBlock(xfawa::BlockStatement* block, std::unordered_map<std::string, int64_t> lies) {
    for (auto& s : block->statements) {
        if (auto* lie = dynamic_cast<xfawa::LieStatement*>(s.get())) {
            // Block-scoped lie: only reads inside `body` observe the falsified
            // value. The outer scope (and anything after the `}`) is unaffected.
            if (auto* num = dynamic_cast<xfawa::NumberLiteral*>(lie->value.get())) {
                std::unordered_map<std::string, int64_t> inner = lies;
                inner[lie->name] = num->value;
                if (lie->body) lieRewriteBlock(lie->body.get(), std::move(inner));
            }
        } else if (auto* assign = dynamic_cast<xfawa::AssignmentStatement*>(s.get())) {
            lieRewriteLeaf(s.get(), lies);
        } else if (auto* b = dynamic_cast<xfawa::BlockStatement*>(s.get())) {
            // Nested block inherits current lies; leave its own scope separate.
            lieRewriteBlock(b, lies);
        } else if (auto* i = dynamic_cast<xfawa::IfStatement*>(s.get())) {
            lieRewriteLeaf(s.get(), lies); // condition
            // Each branch is a fresh scope but starts from current lies.
            std::unordered_map<std::string, int64_t> thenLies = lies, elseLies = lies;
            if (i->thenBranch) {
                if (auto* b2 = dynamic_cast<xfawa::BlockStatement*>(i->thenBranch.get())) lieRewriteBlock(b2, thenLies);
                else lieRewriteStmt(i->thenBranch.get(), thenLies);
            }
            for (auto& ei : i->elseIfBranches) {
                std::unordered_map<std::string, int64_t> eiLies = lies;
                if (auto* b2 = dynamic_cast<xfawa::BlockStatement*>(ei.second.get())) lieRewriteBlock(b2, eiLies);
                else lieRewriteStmt(ei.second.get(), eiLies);
            }
            if (i->elseBranch) {
                if (auto* b2 = dynamic_cast<xfawa::BlockStatement*>(i->elseBranch.get())) lieRewriteBlock(b2, elseLies);
                else lieRewriteStmt(i->elseBranch.get(), elseLies);
            }
        } else if (auto* w = dynamic_cast<xfawa::WhileStatement*>(s.get())) {
            lieRewriteLeaf(s.get(), lies); // condition
            if (w->body) {
                if (auto* b2 = dynamic_cast<xfawa::BlockStatement*>(w->body.get())) lieRewriteBlock(b2, lies);
                else lieRewriteStmt(w->body.get(), lies);
            }
        } else if (auto* f = dynamic_cast<xfawa::ForInStatement*>(s.get())) {
            lieRewriteLeaf(s.get(), lies); // iterable
            if (f->body) {
                if (auto* b2 = dynamic_cast<xfawa::BlockStatement*>(f->body.get())) lieRewriteBlock(b2, lies);
                else lieRewriteStmt(f->body.get(), lies);
            }
        } else if (auto* lp = dynamic_cast<xfawa::LoopStatement*>(s.get())) {
            for (auto& inner : lp->body) {
                if (auto* b2 = dynamic_cast<xfawa::BlockStatement*>(inner.get())) lieRewriteBlock(b2, lies);
                else lieRewriteStmt(inner.get(), lies);
            }
        } else {
            lieRewriteLeaf(s.get(), lies);
        }
    }
}

static void lieRewriteStmt(xfawa::Statement* stmt, std::unordered_map<std::string, int64_t> lies) {
    if (auto* b = dynamic_cast<xfawa::BlockStatement*>(stmt)) {
        lieRewriteBlock(b, std::move(lies));
    } else {
        lieRewriteLeaf(stmt, lies);
    }
}

static void applyLieTransform(xfawa::Program* program) {
    for (auto& mod : program->modules) {
        for (auto& fn : mod->functions) {
            if (fn->body) lieRewriteBlock(fn->body.get(), {});
        }
    }
}

// ---- EXP `wrath` / `paradox`: retroactive history + causal paradox ---------
// These are implemented as a source-level AST rewrite (like `lie`), so they
// reuse the existing variable/print/if semantics and never touch the LLVM
// variable system.

// Deep-copy an expression tree (needed to re-emit a dependent assignment's RHS).
static std::unique_ptr<xfawa::Expression> cloneExpr(const xfawa::Expression* e) {
    if (!e) return nullptr;
    if (auto* n = dynamic_cast<const xfawa::NumberLiteral*>(e))
        return std::make_unique<xfawa::NumberLiteral>(n->value, n->location);
    if (auto* n = dynamic_cast<const xfawa::FloatLiteral*>(e))
        return std::make_unique<xfawa::FloatLiteral>(n->value, n->location);
    if (auto* n = dynamic_cast<const xfawa::BooleanLiteral*>(e))
        return std::make_unique<xfawa::BooleanLiteral>(n->value, n->location);
    if (auto* n = dynamic_cast<const xfawa::StringLiteral*>(e))
        return std::make_unique<xfawa::StringLiteral>(n->value, n->location);
    if (auto* n = dynamic_cast<const xfawa::VariableExpression*>(e))
        return std::make_unique<xfawa::VariableExpression>(n->name, n->location);
    if (auto* n = dynamic_cast<const xfawa::OLiteralExpression*>(e))
        return std::make_unique<xfawa::OLiteralExpression>(n->raw, n->location);
    if (auto* n = dynamic_cast<const xfawa::ParadoxExpression*>(e))
        return std::make_unique<xfawa::ParadoxExpression>(n->location);
    if (auto* n = dynamic_cast<const xfawa::GhostExpression*>(e))
        return std::make_unique<xfawa::GhostExpression>(n->name, n->location);
    if (auto* n = dynamic_cast<const xfawa::UnaryOp*>(e))
        return std::make_unique<xfawa::UnaryOp>(n->op, cloneExpr(n->expr.get()), n->location);
    if (auto* n = dynamic_cast<const xfawa::BinaryOp*>(e))
        return std::make_unique<xfawa::BinaryOp>(n->op, cloneExpr(n->left.get()), cloneExpr(n->right.get()), n->location);
    if (auto* n = dynamic_cast<const xfawa::CallExpression*>(e)) {
        std::vector<std::unique_ptr<xfawa::Expression>> args;
        for (const auto& a : n->args) args.push_back(cloneExpr(a.get()));
        if (!n->ns.empty())
            return std::make_unique<xfawa::CallExpression>(n->name, n->ns, std::move(args), n->location);
        return std::make_unique<xfawa::CallExpression>(n->name, std::move(args), n->location);
    }
    if (auto* n = dynamic_cast<const xfawa::ArrayLiteral*>(e)) {
        if (n->isRange)
            return std::make_unique<xfawa::ArrayLiteral>(cloneExpr(n->rangeStart.get()), cloneExpr(n->rangeEnd.get()), n->location);
        std::vector<std::unique_ptr<xfawa::Expression>> elems;
        for (const auto& el : n->elements) elems.push_back(cloneExpr(el.get()));
        return std::make_unique<xfawa::ArrayLiteral>(std::move(elems), n->location);
    }
    if (auto* n = dynamic_cast<const xfawa::ArrayIndexExpression*>(e))
        return std::make_unique<xfawa::ArrayIndexExpression>(cloneExpr(n->array.get()), cloneExpr(n->index.get()), n->location);
    if (auto* n = dynamic_cast<const xfawa::ArrayRangeExpression*>(e)) {
        if (n->isSlice && n->array)
            return std::make_unique<xfawa::ArrayRangeExpression>(cloneExpr(n->array.get()), cloneExpr(n->start.get()), cloneExpr(n->end.get()), n->location);
        return std::make_unique<xfawa::ArrayRangeExpression>(n->accessType, cloneExpr(n->start.get()), cloneExpr(n->end.get()), n->location);
    }
    return nullptr;
}

// Collect the set of variable names referenced anywhere in an expression tree.
static void collectVarRefs(const xfawa::Expression* e, std::unordered_set<std::string>& out) {
    if (!e) return;
    if (auto* v = dynamic_cast<const xfawa::VariableExpression*>(e)) { out.insert(v->name); return; }
    if (auto* v = dynamic_cast<const xfawa::GhostExpression*>(e)) { out.insert(v->name); return; }
    if (auto* v = dynamic_cast<const xfawa::UnaryOp*>(e)) { collectVarRefs(v->expr.get(), out); return; }
    if (auto* v = dynamic_cast<const xfawa::BinaryOp*>(e)) { collectVarRefs(v->left.get(), out); collectVarRefs(v->right.get(), out); return; }
    if (auto* v = dynamic_cast<const xfawa::CallExpression*>(e)) { for (const auto& a : v->args) collectVarRefs(a.get(), out); return; }
    if (auto* v = dynamic_cast<const xfawa::ArrayLiteral*>(e)) {
        if (v->isRange) { if (v->rangeStart) collectVarRefs(v->rangeStart.get(), out); if (v->rangeEnd) collectVarRefs(v->rangeEnd.get(), out); }
        else for (const auto& el : v->elements) collectVarRefs(el.get(), out);
        return;
    }
    if (auto* v = dynamic_cast<const xfawa::ArrayIndexExpression*>(e)) { collectVarRefs(v->array.get(), out); collectVarRefs(v->index.get(), out); return; }
    if (auto* v = dynamic_cast<const xfawa::ArrayRangeExpression*>(e)) {
        if (v->array) collectVarRefs(v->array.get(), out);
        if (v->start) collectVarRefs(v->start.get(), out);
        if (v->end) collectVarRefs(v->end.get(), out);
        return;
    }
}

// Report a reachable diagnostic (best-effort: no source location, so use line 0).
static void wrathParadoxError(xfawa::ErrorSystem& es, const std::string& msg) {
    es.addSyntaxError(0, 0, msg);
}

// True when the expression contains a function call anywhere in its tree.
// Used to stop ghost propagation at unknown function boundaries: the insides of
// `f(...)` are closed to the compiler, so a ghost argument must not leak its
// causal status through the call's return value.
static bool exprHasCall(const xfawa::Expression* e) {
    if (!e) return false;
    if (auto* c = dynamic_cast<const xfawa::CallExpression*>(e)) {
        for (const auto& a : c->args) if (exprHasCall(a.get())) return true;
        return true;
    }
    if (auto* u = dynamic_cast<const xfawa::UnaryOp*>(e)) return exprHasCall(u->expr.get());
    if (auto* b = dynamic_cast<const xfawa::BinaryOp*>(e)) return exprHasCall(b->left.get()) || exprHasCall(b->right.get());
    if (auto* al = dynamic_cast<const xfawa::ArrayLiteral*>(e)) {
        if (al->isRange) {
            if (al->rangeStart && exprHasCall(al->rangeStart.get())) return true;
            return al->rangeEnd && exprHasCall(al->rangeEnd.get());
        }
        for (const auto& el : al->elements) if (exprHasCall(el.get())) return true;
        return false;
    }
    if (auto* ai = dynamic_cast<const xfawa::ArrayIndexExpression*>(e)) return exprHasCall(ai->array.get()) || exprHasCall(ai->index.get());
    if (auto* ar = dynamic_cast<const xfawa::ArrayRangeExpression*>(e)) {
        if (ar->array && exprHasCall(ar->array.get())) return true;
        if (ar->start && exprHasCall(ar->start.get())) return true;
        return ar->end && exprHasCall(ar->end.get());
    }
    return false;
}

// Rewrite `wrath` / `paradox` statements inside a statement list.
//
// Wrath (`wrath x = R`):
//   - replace with a plain assignment `x = R`
//   - re-emit (in source order) a plain assignment for every later-or-equal
//     variable that transitively depends on `x`, so its stored value refreshes.
//   Already-emitted side effects (prints, etc.) are not touched.
//
// Paradox (`paradox x`) — two contiguous stages:
//   1. `重合论` (recoincidence): the past changes, so x and the whole
//      downstream causal closure are re-validated at their birth points. In
//      straight-line code a destroyed causal root can never re-birth, so every
//      member of the closure fails revalidation. No code is ever re-executed.
//   2. `幽灵论` (ghost): the failed re-birth cannot make the already-existing
//      stored value disappear either, so each member keeps its value but loses
//      its causal origin → becomes a GHOST. Schema B propagation is used: a
//      later *pure* assignment that reads a ghost variable is itself born as a
//      ghost (the causal status flows on, never a special numeric value). An
//      explicit re-assignment gives the variable a new causal origin (NORMAL).
//      Function calls are boundaries: ghost status never crosses a call.
//   Repeated `paradox` on already-ghost variables is idempotent.
//
// Both handle only straight-line code inside a block; control-flow bodies are
// recursed into with a fresh, empty history (documented limitation).
static void wrathParadoxBlock(std::vector<std::unique_ptr<xfawa::Statement>>& stmts,
                              xfawa::ErrorSystem& rep) {
    // name -> RHS expression currently defining that variable (points into the AST)
    std::unordered_map<std::string, const xfawa::Expression*> defining;
    // name -> set of variables it depends on (from its current defining RHS)
    std::unordered_map<std::string, std::unordered_set<std::string>> deps;
    // assignment order, for topological re-emission
    std::unordered_map<std::string, int> order;
    int counter = 0;
    // variables currently in GHOST state (value kept, causal origin lost).
    // Everything not in this set is NORMAL; DESTROYED no longer exists as a
    // reachable transform state (PureParadox reads were replaced by ghosts).
    std::unordered_set<std::string> ghost;

    for (size_t i = 0; i < stmts.size(); i++) {
        auto& up = stmts[i];
        xfawa::Statement* s = up.get();

        // Recurse into nested compound statements with a fresh history.
        if (auto* b = dynamic_cast<xfawa::BlockStatement*>(s)) { wrathParadoxBlock(b->statements, rep); continue; }
        if (auto* iff = dynamic_cast<xfawa::IfStatement*>(s)) {
            if (auto* bb = dynamic_cast<xfawa::BlockStatement*>(iff->thenBranch.get())) wrathParadoxBlock(bb->statements, rep);
            if (auto* bb = dynamic_cast<xfawa::BlockStatement*>(iff->elseBranch.get())) wrathParadoxBlock(bb->statements, rep);
            for (auto& ei : iff->elseIfBranches) if (auto* bb = dynamic_cast<xfawa::BlockStatement*>(ei.second.get())) wrathParadoxBlock(bb->statements, rep);
            continue;
        }
        if (auto* wh = dynamic_cast<xfawa::WhileStatement*>(s)) {
if (auto* bb = dynamic_cast<xfawa::BlockStatement*>(wh->body.get())) wrathParadoxBlock(bb->statements, rep);
            continue;
        }
        if (auto* fi = dynamic_cast<xfawa::ForInStatement*>(s)) {
            continue;
        }

        if (auto* w = dynamic_cast<xfawa::WrathStatement*>(s)) {
            std::string name = w->name;
            auto value = std::move(w->value);  // owned RHS

            // Build the downstream set: name + all transitive dependents.
            std::unordered_set<std::string> downstream;
            std::vector<std::string> stack{name};
            while (!stack.empty()) {
                std::string cur = stack.back(); stack.pop_back();
                if (downstream.count(cur)) continue;
                downstream.insert(cur);
                for (const auto& kv : deps) {
                    if (kv.second.count(cur)) stack.push_back(kv.first);
                }
            }

            // Replace the wrath statement with a plain assignment.
            auto assign = std::make_unique<xfawa::AssignmentStatement>(name, std::move(value), w->location);
            assign->isReassignment = true;
            if (ghost.count(name)) { ghost.erase(name); }  // fresh causal origin
            up = std::move(assign);
            defining[name] = nullptr; // will be reset below
            // (re)record x's own defining expression AFTER the assignment node is built
            {
                auto* a = static_cast<xfawa::AssignmentStatement*>(up.get());
                defining[name] = a->value.get();
                std::unordered_set<std::string> r; collectVarRefs(a->value.get(), r);
                deps[name] = r;
                order[name] = counter++;
            }

            // Re-emit dependents in dependency (source) order.
            std::vector<std::string> dependents;
            for (const auto& kv : order) {
                // kv.first is a var, skip name itself and anything not downstream
                if (kv.first != name && downstream.count(kv.first)) dependents.push_back(kv.first);
            }
            std::sort(dependents.begin(), dependents.end(), [&](const std::string& a, const std::string& b) {
                return order[a] < order[b];
            });

            std::vector<std::unique_ptr<xfawa::Statement>> inserted;
            for (const auto& d : dependents) {
                auto it = defining.find(d);
                if (it == defining.end() || it->second == nullptr) continue;
                auto reval = std::make_unique<xfawa::AssignmentStatement>(d, cloneExpr(it->second), w->location);
                reval->isReassignment = true;
                defining[d] = reval->value.get();
                order[d] = counter++;
                inserted.push_back(std::move(reval));
            }
            // insert right after index i
            stmts.insert(stmts.begin() + i + 1, std::make_move_iterator(inserted.begin()),
                         std::make_move_iterator(inserted.end()));
            i += inserted.size();
            continue;
        }

        if (auto* p = dynamic_cast<xfawa::ParadoxStatement*>(s)) {
            std::string name = p->name;
            if (defining.find(name) == defining.end()) {
                wrathParadoxError(rep, "paradox on unknown variable '" + name + "'");
                up = nullptr; // drop
                continue;
            }
            // downstream = name + transitive dependents (revalidated at birth
            // order; in straight-line code every one of them fails re-birth).
            std::unordered_set<std::string> downstream;
            std::vector<std::string> stack{name};
            while (!stack.empty()) {
                std::string cur = stack.back(); stack.pop_back();
                if (downstream.count(cur)) continue;
                downstream.insert(cur);
                for (const auto& kv : deps) if (kv.second.count(cur)) stack.push_back(kv.first);
            }
            for (const auto& dd : downstream) ghost.insert(dd);  // idempotent
            up = nullptr; // drop the paradox statement
            continue;
        }

        // Ordinary assignment: (re)birth of a variable.
        if (auto* a = dynamic_cast<xfawa::AssignmentStatement*>(s)) {
            std::unordered_set<std::string> r; collectVarRefs(a->value.get(), r);
            bool refsGhost = false;
            if (!exprHasCall(a->value.get())) {  // calls are ghost boundaries
                for (const auto& v : r) {
                    if (ghost.count(v)) { refsGhost = true; break; }
                }
            }
            // Schema B: a pure read of a ghost gives birth to a ghost.
            if (refsGhost) ghost.insert(a->name);
            else ghost.erase(a->name);  // new causal origin → NORMAL
            defining[a->name] = a->value.get();
            deps[a->name] = r;
            order[a->name] = counter++;
            continue;
        }

        // Any other statement (print / return / etc.): reads of ghost variables
        // keep their real values untouched. Only a *direct* print of a ghost
        // variable is marked -- the value stays, "#" is appended.
        if (auto* pr = dynamic_cast<xfawa::PrintStatement*>(s)) {
            if (auto* v = dynamic_cast<xfawa::VariableExpression*>(pr->expr.get())) {
                if (ghost.count(v->name)) {
                    pr->expr = std::make_unique<xfawa::GhostExpression>(v->name, v->location);
                }
            }
            continue;
        }
        if (auto* r = dynamic_cast<xfawa::ReturnStatement*>(s)) { continue; }
        if (auto* e = dynamic_cast<xfawa::ExpressionStatement*>(s)) { continue; }
    }

    // Remove any statements that were dropped (paradox / unknown-name errors).
    stmts.erase(std::remove_if(stmts.begin(), stmts.end(),
                               [](const std::unique_ptr<xfawa::Statement>& p) { return p == nullptr; }),
                stmts.end());
}

static void applyWrathParadoxTransform(xfawa::Program* program, xfawa::ErrorSystem& rep) {
    for (auto& mod : program->modules) {
        for (auto& fn : mod->functions) {
            if (fn->body) wrathParadoxBlock(fn->body->statements, rep);
        }
    }
}

// Map each `repeat: N` directive to the first statement whose source line is
// strictly greater than the directive's line.
static void buildRepeatMap(
    xfawa::Program* program,
    const std::vector<std::pair<int, int>>& directives,
    std::unordered_map<const xfawa::Statement*, int>& out)
{

    std::vector<xfawa::Statement*> statements;
    for (auto& mod : program->modules) {
        for (auto& fn : mod->functions) {
            if (fn->body) {
                collectStatementsRec(fn->body.get(), statements);
            }
        }
    }

    std::stable_sort(statements.begin(), statements.end(),
        [](const xfawa::Statement* a, const xfawa::Statement* b) {
            return a->location < b->location;
        });

    size_t si = 0;
    for (const auto& d : directives) {
        int line = d.first;
        int count = d.second;
        while (si < statements.size() && statements[si]->location.line <= line) {
            si++;
        }
        if (si < statements.size()) {
            out[statements[si]] = count;
        }
    }
}

// ---------------------------------------------------------------------------
// Executable comments: if the text after `//` parses as a complete xfawa
// statement, it is compiled and inserted into the AST at the comment position.
// Non-parseable comments stay ordinary comments. `repeat:` comments handled above.
// ---------------------------------------------------------------------------

// Try to parse comment text as one or more xfawa statements.
// Returns an empty vector if parsing fails (treated as plain comment).
static std::vector<std::unique_ptr<xfawa::Statement>> tryParseExecutableComment(const std::string& text) {
    std::string wrapped = "#__execcmt {\nfn __exec_tmp() {\n" + text + "\n}\n}";
    xfawa::Lexer lex(wrapped);
    std::vector<xfawa::Token> toks = lex.tokenize();
    if (lex.hasErrors()) return {};
    xfawa::Parser parser(toks);
    std::unique_ptr<xfawa::Program> prog = parser.parseProgram();
    if (parser.hasErrors() || !prog) return {};
    if (prog->modules.empty()) return {};
    auto& mod = prog->modules[0];
    if (mod->functions.empty()) return {};
    auto& fn = mod->functions[0];
    if (!fn->body) return {};
    std::vector<std::unique_ptr<xfawa::Statement>> out;
    for (auto& s : fn->body->statements) {
        out.push_back(std::move(s));
    }
    return out;
}

// Compute the {min,max} line range covered by a statement subtree.
static void stmtLineRange(xfawa::Statement* stmt, int& mn, int& mx) {
    std::vector<xfawa::Statement*> all;
    collectStatementsRec(stmt, all);
    mn = INT32_MAX; mx = INT32_MIN;
    for (auto* s : all) {
        mn = std::min(mn, s->location.line);
        mx = std::max(mx, s->location.line);
    }
}

static bool insertIntoCompound(xfawa::Statement* s, int line,
                               std::vector<std::unique_ptr<xfawa::Statement>>& stmts);

static bool insertIntoStmtList(std::vector<std::unique_ptr<xfawa::Statement>>& list,
                               int line,
                               std::vector<std::unique_ptr<xfawa::Statement>>& stmts) {
    // Descend into the compound child whose line range contains the comment.
    for (auto& up : list) {
        xfawa::Statement* s = up.get();
        int mn, mx;
        stmtLineRange(s, mn, mx);
        if (line >= mn && line <= mx) {
            if (insertIntoCompound(s, line, stmts)) return true;
        }
    }
    // Insert before the first statement that starts after the comment line;
    // if none (comment at end of block), append to the end of the block.
    size_t idx = list.size();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i]->location.line > line) { idx = i; break; }
    }
    for (auto& st : stmts) {
        list.insert(list.begin() + idx, std::move(st));
        idx++;
    }
    return true;
}

static bool insertIntoCompound(xfawa::Statement* s, int line,
                               std::vector<std::unique_ptr<xfawa::Statement>>& stmts) {
    if (auto* b = dynamic_cast<xfawa::BlockStatement*>(s)) {
        return insertIntoStmtList(b->statements, line, stmts);
    }
    if (auto* i = dynamic_cast<xfawa::IfStatement*>(s)) {
        auto tryBranch = [&](xfawa::Statement* br) -> bool {
            if (!br) return false;
            int mn, mx; stmtLineRange(br, mn, mx);
            if (line >= mn && line <= mx) return insertIntoCompound(br, line, stmts);
            return false;
        };
        if (tryBranch(i->thenBranch.get())) return true;
        for (auto& ei : i->elseIfBranches) if (tryBranch(ei.second.get())) return true;
        if (tryBranch(i->elseBranch.get())) return true;
        return false;
    }
    if (auto* w = dynamic_cast<xfawa::WhileStatement*>(s)) {
        if (w->body) return insertIntoCompound(w->body.get(), line, stmts);
        return false;
    }
    if (auto* f = dynamic_cast<xfawa::ForInStatement*>(s)) {
        if (f->body) return insertIntoCompound(f->body.get(), line, stmts);
        return false;
    }
    if (auto* lp = dynamic_cast<xfawa::LoopStatement*>(s)) {
        return insertIntoStmtList(lp->body, line, stmts);
    }
    return false;
}

// Insert parsed executable-comment statements into the AST at the comment's
// line position (inside the first function block that spans that line).
static bool applyExecutableComment(xfawa::Program* program, int line,
                                   std::vector<std::unique_ptr<xfawa::Statement>> stmts) {
    for (auto& mod : program->modules) {
        for (auto& fn : mod->functions) {
            if (fn->body) {
                if (insertIntoCompound(fn->body.get(), line, stmts)) return true;
            }
        }
    }
    return false;
}

// ---- EXP: compiler-written annotations -------------------------------------

// Register annotation text for every EXP statement type at a single point.
// Adding a new EXP syntax in the future only requires one line here.
static std::string expAnnotationDescription(xfawa::NodeType t) {
    switch (t) {
        case xfawa::NodeType::BOOM_STATEMENT:      return "// EXP: boom —— 让程序播放爆炸效果后正常退出";
        case xfawa::NodeType::BSOD_STATEMENT:      return "// EXP: bsod —— 显示一个受控假蓝屏后继续执行";
        case xfawa::NodeType::BELIEVE_STATEMENT:   return "// EXP: believe —— 让程序相信一个原本错误的事实";
        case xfawa::NodeType::LIE_STATEMENT:        return "// EXP: lie —— 修改变量的被观察值（真实值不变）";
        case xfawa::NodeType::UN_STATEMENT:         return "// EXP: un —— 禁用某个语言行为";
        case xfawa::NodeType::IGNORE_STATEMENT:     return "// EXP: ignore —— 忽略该语句，不执行";
        case xfawa::NodeType::DO_STATEMENT:         return "// EXP: do —— 强制执行该语句，不受 un 影响";
        case xfawa::NodeType::PLEASE_STATEMENT:     return "// EXP: please —— 先输出 thank you! 再执行内部语句（不改变 rage，也不算红温时的五行请字）";
        case xfawa::NodeType::PLEASE_NOTICE_STATEMENT: return "// EXP: please —— 裸请字：仅红温（rage>=3）时有效；满足每五行一次 please 并固定 rage -1，其余情况什么也不做";
        case xfawa::NodeType::SHUTUP_STATEMENT:     return "// EXP: shutup —— 立即压制后续所有 warning（荒诞恐吓）";
        case xfawa::NodeType::ELLIPSIS_STATEMENT:   return "// EXP: ... —— 随机执行一个允许调用的安全动作";
        case xfawa::NodeType::SLEEP_STATEMENT:      return "// EXP: sleep —— 让程序暂停指定的秒数";
        default: return "";
    }
}

// Produce a comment for a statement. For EXP syntax it uses the unified
// registry above; for ordinary statements it uses a simple natural-language
// description (kept so `--annotate` still helps on normal code too).
static std::string describeStatement(xfawa::Statement* stmt) {
    std::string exp = expAnnotationDescription(stmt->getNodeType());
    if (!exp.empty()) return exp;

    if (auto* a = dynamic_cast<xfawa::AssignmentStatement*>(stmt)) {
        return "// 将 " + a->name + " 设置为 " + (a->value ? a->value->toString() : "");
    }
    if (auto* p = dynamic_cast<xfawa::PrintStatement*>(stmt)) {
        return "// 输出 " + (p->expr ? p->expr->toString() : "");
    }
    if (auto* r = dynamic_cast<xfawa::ReturnStatement*>(stmt)) {
        return "// 返回 " + (r->value ? r->value->toString() : "");
    }
    return "";
}

// Walk the AST in source order and print each leaf statement as:
//   // <description>
//   <original source line>
static void annotateProgram(xfawa::Program* program, const std::string& source, const std::string& inputFile) {
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : source) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur += c;
        }
        lines.push_back(cur);
    }

    std::vector<xfawa::Statement*> statements;
    for (auto& mod : program->modules) {
        for (auto& fn : mod->functions) {
            if (fn->body) collectStatementsRec(fn->body.get(), statements);
        }
    }
    std::stable_sort(statements.begin(), statements.end(),
        [](const xfawa::Statement* a, const xfawa::Statement* b) {
            return a->location < b->location;
        });

    std::vector<std::pair<int, std::string>> annotations;
    for (auto* s : statements) {
        std::string desc = describeStatement(s);
        if (desc.empty()) continue;
        annotations.push_back({s->location.line, desc});
    }
    // Annotate from bottom to top so earlier line insertions don't shift indices.
    std::stable_sort(annotations.begin(), annotations.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    for (auto& [ln, desc] : annotations) {
        if (ln >= 1 && ln <= static_cast<int>(lines.size())) {
            std::string indent;
            const std::string& orig = lines[ln - 1];
            for (char c : orig) {
                if (c == ' ' || c == '\t') indent += c; else break;
            }
            lines.insert(lines.begin() + (ln - 1), indent + desc);
        }
    }

    // Write back to the source file as UTF-8 (with a final newline).
    std::ofstream outFile(inputFile, std::ios::binary | std::ios::trunc);
    if (!outFile.is_open()) {
        xfawa::ErrorReporter::get().addSyntaxError(0, 0, "Failed to write annotated source file");
        return;
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        outFile << lines[i];
        if (i + 1 < lines.size()) outFile << '\n';
    }
    outFile << '\n';
    outFile.close();

    for (const auto& s : lines) {
        std::cout << s << "\n";
    }
}

// ---- EXP: persistent "rage" state ------------------------------------------
// rage is xfawac's own accumulated anger meter. It is NOT a per-compilation
// variable: it lives as a plain integer NEXT TO THE COMPILER ITSELF and is
// reloaded on every launch, so it survives any number of independent xfawac
// process invocations. Only an explicit `xfawac rage reset` clears it. Every
// copy of xfawac.exe keeps its own rage in its own directory.

#ifdef _WIN32
static std::string wideToUtf8(const wchar_t* w) {
    if (!w || !*w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring ws(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &ws[0], n);
    return ws;
}
#else
#ifndef PATH_MAX
#include <limits.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

// Directory that contains the running xfawac executable itself.
//   Windows: GetModuleFileNameW(NULL)
//   Linux:   /proc/self/exe
//   macOS:   _NSGetExecutablePath
static std::string compilerDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "";
    std::string full = wideToUtf8(buf);
    size_t pos = full.find_last_of("/\\");
    if (pos == std::string::npos) return "";
    return full.substr(0, pos);
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t sz = static_cast<uint32_t>(sizeof(buf));
    if (_NSGetExecutablePath(buf, &sz) == 0) {
        std::string p(buf);
        size_t pos = p.find_last_of('/');
        if (pos != std::string::npos) return p.substr(0, pos);
    }
    return "";
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string p(buf);
        size_t pos = p.find_last_of('/');
        if (pos != std::string::npos) return p.substr(0, pos);
    }
    return "";
#endif
}

// State directory: the compiler's own directory. The rage file sits right next
// to xfawac.exe (e.g. build/Release/rage), never in the user's project or the
// current working directory.
static std::string rageStateDir() {
    std::string dir = compilerDir();
    return dir.empty() ? "." : dir;
}

static std::string rageStateFilePath() {
#ifdef _WIN32
    return rageStateDir() + "\\rage";
#else
    return rageStateDir() + "/rage";
#endif
}

// Recovery policy: a missing, unreadable, malformed or out-of-range state file
// is never fatal — the compiler simply falls back to 0.
static int loadRageState() {
    std::ifstream f(rageStateFilePath(), std::ios::binary);
    if (!f.is_open()) return 0;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t a = content.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return 0;
    size_t b = content.find_last_not_of(" \t\r\n");
    content = content.substr(a, b - a + 1);
    int v = 0;
    try {
        size_t used = 0;
        v = std::stoi(content, &used);
        if (used != content.size()) return 0; // trailing garbage
    } catch (...) {
        return 0; // corrupt -> safe default
    }
    if (v < 0 || v > 5) return 0; // out of range -> safe default
    return v;
}

// Atomic-ish save: write a pid-unique temp file, flush, close, then replace the
// real state file (MoveFileExW on Windows, rename on POSIX). The transient
// state file is never truncated in place, so an interrupted write cannot leave
// a half-written "rage".
static bool saveRageState(int value) {
    int v = value < 0 ? 0 : (value > 5 ? 5 : value);
    std::string dir = rageStateDir();
    if (!ensureDirectoryExists(dir)) return false;
    std::string finalPath = rageStateFilePath();
#ifdef _WIN32
    std::string tmpPath = dir + "\\rage.tmp." + std::to_string(static_cast<long long>(_getpid()));
#else
    std::string tmpPath = dir + "/rage.tmp." + std::to_string(static_cast<long long>(getpid()));
#endif
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f << v;
        f.flush();
        f.close();
    }
#ifdef _WIN32
    std::wstring tmpW = utf8ToWide(tmpPath);
    std::wstring finW = utf8ToWide(finalPath);
    if (tmpW.empty() || finW.empty()) return false;
    return MoveFileExW(tmpW.c_str(), finW.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(tmpPath.c_str(), finalPath.c_str()) == 0;
#endif
}

// RAII: after semantic analysis has run, persist rage if it changed. Installed
// only once the analyzer exists; all earlier exit paths cannot have changed
// rage (try-catch bumps happen only during analysis).
struct RagePersist {
    int initial;
    const xfawa::SemanticAnalyzer* analyzer;
    RagePersist(int i, const xfawa::SemanticAnalyzer* a) : initial(i), analyzer(a) {}
    ~RagePersist() {
        if (analyzer) {
            int cur = analyzer->getRage();
            if (cur != initial) {
                saveRageState(cur);
            }
        }
    }
};

int main(int argc, char** argv) {
    xfawa::ErrorReporter::initialize();

    std::string inputFile;
    std::string outputFile = "";
    std::string configFile;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                outputFile = argv[i + 1];
                i++;
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            g_debug = 1;
            xfawa::g_debug_global = 1;
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep") == 0) {
            g_keep_temp = true;
        } else if (strcmp(argv[i], "--emit-llvm") == 0 || strcmp(argv[i], "--emit-ll") == 0) {
            g_emit_llvm = true;
        } else if (strcmp(argv[i], "--emit-asm") == 0) {
            g_emit_asm = true;
        } else if (strcmp(argv[i], "-O0") == 0) {
            g_opt_level = xfawa::OptimizationLevel::O0;
            g_opt_level_set = true;
        } else if (strcmp(argv[i], "-O1") == 0) {
            g_opt_level = xfawa::OptimizationLevel::O1;
            g_opt_level_set = true;
        } else if (strcmp(argv[i], "-O2") == 0) {
            g_opt_level = xfawa::OptimizationLevel::O2;
            g_opt_level_set = true;
        } else if (strcmp(argv[i], "-O3") == 0) {
            g_opt_level = xfawa::OptimizationLevel::O3;
            g_opt_level_set = true;
        } else if (strcmp(argv[i], "--transpile-c") == 0) {
            g_transpile_c = true;
        } else if (strcmp(argv[i], "--transpile-cpp") == 0) {
            g_transpile_cpp = true;
        } else if (strcmp(argv[i], "--annotate") == 0) {
            g_annotate = true;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                configFile = argv[i + 1];
                i++;
            }
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-config") == 0) {
            g_use_config = false;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printVersion();
            xfawa::ErrorReporter::cleanup();
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            xfawa::ErrorReporter::cleanup();
            return 0;
        } else if (strcmp(argv[i], "rage") == 0) {
            // EXP subcommands for the persistent rage meter.
            bool reset = false;
            if (i + 1 < argc && strcmp(argv[i + 1], "reset") == 0) {
                reset = true;
                i++;
            }
            if (reset) {
                saveRageState(0);
                printf("rage reset: 0/5\n");
            } else {
                printf("rage: %d/5\n", loadRageState());
            }
            xfawa::ErrorReporter::cleanup();
            return 0;
        } else if (argv[i][0] != '-') {
            inputFile = argv[i];
        }
    }
    
    if (inputFile.empty()) {
        if (g_log_language == xfawa::LogLanguage::ZH) {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, utf8(u8"\u672a\u6307\u5b9a\u8f93\u5165\u6587\u4ef6"));
        } else {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, "No input file specified");
        }
        printUsage(argv[0]);
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    xfawa::ErrorReporter::get().setCurrentFile(inputFile);
    
    xfawa::CompilerConfig config;
    if (g_use_config) {
        if (!configFile.empty()) {
            config = xfawa::ConfigLoader::loadFromFile(configFile);
        } else {
            config = xfawa::ConfigLoader::load(inputFile);
        }
        
        if (config.debug_info) {
            g_debug = 1;
            xfawa::g_debug_global = 1;
        }
        if (config.emit_ll) {
            g_emit_llvm = true;
        }
        if (config.emit_asm) {
            g_emit_asm = true;
        }
        g_log_language = config.log_language;
        
        if (g_opt_level_set) {
            config.opt_level = g_opt_level;
        }
        
        xfawa::ErrorReporter::get().setShowWarningTypes(config.show_warning_types);
        xfawa::ErrorReporter::get().setWarningsEnabled(config.warnings);
    } else {
        config.opt_level = g_opt_level;
    }
    
    if (g_debug) {
        printConfig(config);
    }
    
    ensureDirectoryExists(config.output_dir);
    ensureDirectoryExists(config.intermediate_dir);
    
    std::string source = readFile(inputFile);
    
    if (source.empty()) {
        if (g_log_language == xfawa::LogLanguage::ZH) {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, utf8(u8"输入文件为空或无法读取"));
        } else {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, "Input file is empty or could not be read");
        }
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    xfawa::ModsSystem modsSystem;
    std::string processedSource;
    
    if (!processMods(modsSystem, source, processedSource)) {
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    if (g_debug && modsSystem.hasModifications()) {
        std::cout << "[debug] Source after mod processing:" << std::endl;
        std::cout << processedSource << std::endl;
    }
    
    if (g_debug && modsSystem.hasAddedSyntaxes()) {
        std::cout << "[debug] Added syntaxes details:" << std::endl;
        for (const auto& syntax : modsSystem.getAddedSyntaxes()) {
            std::cout << "  Name: " << syntax.name << std::endl;
            std::cout << "  Pattern: " << syntax.syntaxPattern << std::endl;
            std::cout << "  Logic: " << syntax.logicCode << std::endl;
            std::cout << "  Parameters: ";
            for (const auto& param : syntax.parameterOrder) {
                std::cout << param << " ";
            }
            std::cout << std::endl;
        }
        std::cout << "[debug] Source after syntax expansion:" << std::endl;
        std::cout << processedSource << std::endl;
    }
    
    xfawa::Lexer lexer(processedSource);
    std::vector<xfawa::Token> tokens = lexer.tokenize();
    
    // ---- EXP-001: make the compiler observe comments -------------------
    const auto& commentList = lexer.getComments();
    std::vector<std::pair<int, int>> repeatDirectives;
    std::vector<std::pair<int, std::vector<std::unique_ptr<xfawa::Statement>>>> execComments;
    bool repeatError = false;
    constexpr int MAX_REPEAT = 1000;
    for (const auto& cmt : commentList) {
        // Phase 1: the compiler sees the comment and reports it.
        std::cout << "[xfawa-exp] comment: " << cmt.text << std::endl;

        // Phase 2: interpret a minimal `repeat: N` directive.
        std::string text = trimWhitespace(cmt.text);
        if (startsWith(text, "repeat:")) {
            std::string numPart = trimWhitespace(text.substr(7));
            try {
                int n = std::stoi(numPart);
                if (n < 0) {
                    xfawa::ErrorReporter::get().addSyntaxError(cmt.line, cmt.column,
                        "Invalid repeat value in comment: must be non-negative");
                    repeatError = true;
                } else if (n > MAX_REPEAT) {
                    xfawa::ErrorReporter::get().addSyntaxError(cmt.line, cmt.column,
                        "repeat value exceeds maximum allowed (" + std::to_string(MAX_REPEAT) + ")");
                    repeatError = true;
                } else {
                    repeatDirectives.push_back({cmt.line, n});
                }
            } catch (...) {
                xfawa::ErrorReporter::get().addSyntaxError(cmt.line, cmt.column,
                    "Invalid repeat value in comment");
                repeatError = true;
            }
        } else {
            // Phase 3: try to interpret the comment as an executable statement.
            std::vector<std::unique_ptr<xfawa::Statement>> stmts = tryParseExecutableComment(text);
            if (!stmts.empty()) {
                execComments.emplace_back(cmt.line, std::move(stmts));
            }
        }
    }
    if (repeatError) {
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    if (lexer.hasErrors()) {
        for (const auto& error : lexer.getErrors()) {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, error);
        }
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    if (g_debug) {
        std::cout << "[debug] Tokens:" << std::endl;
        for (const auto& token : tokens) {
            std::cout << "  " << token.toString() << std::endl;
        }
    }

    xfawa::Parser parser(tokens);
    std::unique_ptr<xfawa::Program> program = parser.parseProgram();
    
    if (g_debug) {
        std::cout << "[debug] Parser errors: " << parser.getErrors().size() << std::endl;
        for (const auto& error : parser.getErrors()) {
            std::cout << "[debug]   - " << error << std::endl;
        }
    }
    
    if (parser.hasErrors()) {
        for (const auto& error : parser.getErrors()) {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, error);
        }
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    if (parser.hasWarnings()) {
        for (const auto& warning : parser.getWarnings()) {
            xfawa::ErrorReporter::get().addSyntaxWarning(0, 0, warning);
        }
    }
    
    if (!program) {
        xfawa::ErrorReporter::get().addSyntaxError(0, 0, "Failed to parse program");
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    // ---- EXP-001: inject executable-comment statements into the AST -------
    for (auto& e : execComments) {
        applyExecutableComment(program.get(), e.first, std::move(e.second));
    }
    
    // ---- EXP `lie`: rewrite variable reads to their falsified value -------
    applyLieTransform(program.get());
    
    // ---- EXP `wrath` / `paradox`: retroactive history + causal paradox -----
    applyWrathParadoxTransform(program.get(), xfawa::ErrorReporter::get());
    
    if (g_debug) {
        std::cout << "[debug] AST:" << std::endl;
        std::cout << program->toString() << std::endl;
    }
    
    // ---- EXP: compiler-written annotations (--annotate) -------------------
    if (g_annotate) {
        annotateProgram(program.get(), processedSource, inputFile);
        xfawa::ErrorReporter::cleanup();
        return 0;
    }
    
    // 转译器模式
    if (g_transpile_c || g_transpile_cpp) {
        xfawa::TargetLanguage targetLang = g_transpile_c ? xfawa::TargetLanguage::C : xfawa::TargetLanguage::CPP;
        xfawa::Transpiler transpiler(targetLang);
        
        std::string transpiledCode = transpiler.transpile(program.get());
        
        // 确定输出文件名
        std::string transpileOutputFile;
        if (!outputFile.empty()) {
            transpileOutputFile = outputFile;
        } else {
            // 根据目标语言生成默认文件名
            std::filesystem::path inputPath(inputFile);
            std::string baseName = inputPath.stem().string();
            if (g_transpile_c) {
                transpileOutputFile = baseName + ".c";
            } else {
                transpileOutputFile = baseName + ".cpp";
            }
        }
        
        // 写入转译后的代码
        std::ofstream outFile(transpileOutputFile);
        if (!outFile.is_open()) {
            if (g_log_language == xfawa::LogLanguage::ZH) {
                xfawa::ErrorReporter::get().addSyntaxError(0, 0, utf8(u8"\u65e0\u6cd5\u5199\u5165\u8f6c\u8bd1\u8f93\u51fa\u6587\u4ef6"));
            } else {
                xfawa::ErrorReporter::get().addSyntaxError(0, 0, "Failed to write transpiled output file");
            }
            xfawa::ErrorReporter::get().printDiagnostics();
            xfawa::ErrorReporter::cleanup();
            return 1;
        }
        
        outFile << transpiledCode;
        outFile.close();
        
        if (g_log_language == xfawa::LogLanguage::ZH) {
            std::cout << utf8(u8"\u8f6c\u8bd1\u6210\u529f\uff0c\u8f93\u51fa\u6587\u4ef6: ") << transpileOutputFile << std::endl;
        } else {
            std::cout << "Transpilation successful, output file: " << transpileOutputFile << std::endl;
        }
        
        if (g_debug) {
            std::cout << "[debug] Transpiled code:" << std::endl;
            std::cout << transpiledCode << std::endl;
        }
        
        xfawa::ErrorReporter::cleanup();
        return 0;
    }
    
    xfawa::ASTTransformer transformer;
    transformer.setErrorSystem(&xfawa::ErrorReporter::get());
    
    if (!transformer.transform(program.get())) {
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    // ---- EXP: rage starts from the persistStore; try-catch raises it, sorry
    // lowers it; the RAII guard writes the final value back on every exit path
    // that ran analysis (earlier exits cannot have changed it).
    int initialRage = loadRageState();
    xfawa::SemanticAnalyzer semanticAnalyzer(initialRage);
    RagePersist ragePersist(initialRage, &semanticAnalyzer);
    if (!semanticAnalyzer.analyze(program.get())) {
        for (const auto& error : semanticAnalyzer.getErrors()) {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, error);
        }
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    if (semanticAnalyzer.hasWarnings()) {
        for (const auto& warning : semanticAnalyzer.getWarnings()) {
            xfawa::ErrorReporter::get().addSyntaxWarning(0, 0, warning);
        }
    }
    
    xfawa::LLVMCodegen::initializeTargets();
    
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module = std::make_unique<llvm::Module>("xfawa_module", context);
    
    xfawa::LLVMCodegen codegen(context, module.get(), config.opt_level);
    codegen.setXraphicsLogEnabled(config.xraphics_log);

    // ---- EXP-001: apply executable-comment repeat directives ----------
    std::unordered_map<const xfawa::Statement*, int> repeatMap;
    buildRepeatMap(program.get(), repeatDirectives, repeatMap);
    codegen.setRepeatMap(repeatMap);
    
    if (!codegen.codegenProgram(program.get())) {
        for (const auto& error : codegen.getErrors()) {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, error);
        }
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    if (!codegen.verifyModule()) {
        for (const auto& error : codegen.getErrors()) {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, error);
        }
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    if (codegen.hasWarnings()) {
        for (const auto& warning : codegen.getWarnings()) {
            xfawa::ErrorReporter::get().addSyntaxWarning(0, 0, warning);
        }
    }
    
    if (g_debug) {
        module->print(llvm::outs(), nullptr);
    }
    
    std::string inputBaseName = inputFile.substr(inputFile.find_last_of("/\\") + 1);
    if (inputBaseName.find('.') != std::string::npos) {
        inputBaseName = inputBaseName.substr(0, inputBaseName.find_last_of('.'));
    }
    
    if (outputFile.empty()) {
        outputFile = config.output_dir + "/" + inputBaseName + ".exe";
    }
    
    std::string outputBaseName = outputFile.substr(outputFile.find_last_of("/\\") + 1);
    if (outputBaseName.find('.') != std::string::npos) {
        outputBaseName = outputBaseName.substr(0, outputBaseName.find_last_of('.'));
    }
    
    std::string objFile = config.intermediate_dir + "\\" + outputBaseName + ".o";
    
    std::string llFile = outputBaseName + ".exe.ll";
    std::string asmFile = outputBaseName + ".exe.asm";
    
    if (!codegen.emitObjectFile(objFile, g_emit_llvm, g_emit_asm, llFile, asmFile)) {
        for (const auto& error : codegen.getErrors()) {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, error);
        }
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    if (!codegen.linkExecutable(objFile, outputFile)) {
        for (const auto& error : codegen.getErrors()) {
            xfawa::ErrorReporter::get().addSyntaxError(0, 0, error);
        }
        xfawa::ErrorReporter::get().printDiagnostics();
        xfawa::ErrorReporter::cleanup();
        return 1;
    }
    
    if (!g_keep_temp) {
        std::remove(objFile.c_str());
    }
    
    if (xfawa::ErrorReporter::get().hasWarnings()) {
        xfawa::ErrorReporter::get().printDiagnostics();
    }
    
    if (g_log_language == xfawa::LogLanguage::ZH) {
        std::cout << utf8(u8"\u7f16\u8bd1\u6210\u529f\uff1a") << outputFile << std::endl;
    } else {
        std::cout << "Compilation successful: " << outputFile << std::endl;
    }
    
    xfawa::ErrorReporter::cleanup();
    return 0;
}
