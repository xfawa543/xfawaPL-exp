#include "xfawa_llvm_codegen.h"
#include "xfawa_error.h"
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <sstream>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
#include <llvm/ADT/APInt.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/ADCE.h>
#include <llvm/Transforms/Scalar/DCE.h>
#include <llvm/Transforms/Scalar/EarlyCSE.h>
#include <llvm/Transforms/Scalar/SCCP.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm/Transforms/Utils/LoopSimplify.h>
#include <llvm/Passes/PassBuilder.h>
#include <lld/Common/Driver.h>

LLD_HAS_DRIVER(coff)

namespace xfawa {

static llvm::ConstantInt* createConstInt(llvm::LLVMContext& ctx, llvm::IntegerType* ty, int64_t value) {
    return static_cast<llvm::ConstantInt*>(llvm::ConstantInt::get(ty, llvm::APInt(ty->getBitWidth(), value, true)));
}

static char binaryOpChar(xfawa::BinaryOpType op) {
    switch (op) {
        case xfawa::BinaryOpType::ADD: return '+';
        case xfawa::BinaryOpType::SUB: return '-';
        case xfawa::BinaryOpType::MUL: return '*';
        case xfawa::BinaryOpType::DIV: return '/';
        default: return 0;
    }
}

namespace {

llvm::CodeGenOptLevel toCodeGenOptLevel(xfawa::OptimizationLevel level) {
    switch (level) {
        case xfawa::OptimizationLevel::O0:
            return llvm::CodeGenOptLevel::None;
        case xfawa::OptimizationLevel::O1:
            return llvm::CodeGenOptLevel::Less;
        case xfawa::OptimizationLevel::O2:
            return llvm::CodeGenOptLevel::Default;
        case xfawa::OptimizationLevel::O3:
            return llvm::CodeGenOptLevel::Aggressive;
    }

    return llvm::CodeGenOptLevel::Default;
}

std::unique_ptr<llvm::TargetMachine> createTargetMachine(llvm::Module& module, xfawa::OptimizationLevel optLevel, std::vector<std::string>& errors) {
    llvm::Triple triple = module.getTargetTriple();
    if (triple.str().empty()) {
        triple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
        module.setTargetTriple(triple);
    }

    std::string err;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
    if (!target) {
        errors.push_back("Unable to find target for triple '" + triple.str() + "': " + err);
        return nullptr;
    }

    llvm::TargetOptions options;
    std::optional<llvm::Reloc::Model> relocModel = llvm::Reloc::PIC_;
    auto targetMachine = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(triple, "generic", "", options, relocModel, std::nullopt, toCodeGenOptLevel(optLevel)));

    if (!targetMachine) {
        errors.push_back("Unable to create LLVM target machine for triple '" + triple.str() + "'");
        return nullptr;
    }

    module.setDataLayout(targetMachine->createDataLayout());
    return targetMachine;
}

bool emitMachineCode(llvm::Module& module, llvm::TargetMachine& targetMachine,
                     const std::string& path, llvm::CodeGenFileType fileType,
                     std::vector<std::string>& errors) {
    std::error_code ec;
    llvm::raw_fd_ostream dest(path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        errors.push_back("Unable to open output file '" + path + "': " + ec.message());
        return false;
    }

    llvm::legacy::PassManager passManager;
    if (targetMachine.addPassesToEmitFile(passManager, dest, nullptr, fileType)) {
        errors.push_back("LLVM target cannot emit requested file type for '" + path + "'");
        return false;
    }

    passManager.run(module);
    dest.flush();
    return true;
}

std::optional<std::filesystem::path> findLatestVersionDir(const std::filesystem::path& root) {
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> best;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }

        if (!best || entry.path().filename().string() > best->filename().string()) {
            best = entry.path();
        }
    }

    return best;
}

std::optional<std::filesystem::path> getEnvPath(const char* name) {
    const char* value = std::getenv(name);
    if (!value || *value == '\0') {
        return std::nullopt;
    }

    return std::filesystem::path(value);
}

bool appendLibPath(std::vector<std::string>& args, const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
        return false;
    }

    args.push_back("/libpath:" + path.string());
    return true;
}

} // namespace

LLVMCodegen::LLVMCodegen(llvm::LLVMContext& ctx, llvm::Module* mod) 
    : context(ctx), module(mod), builder(ctx), hasMainFunction(false), hasWindowStatements(false), usesRandomBuiltin(false), hasBsod(false), loopEndBB(nullptr), optLevel(OptimizationLevel::O2), generatedWindowCount(0), activeWindowId(-1), xraphicsLogEnabled(false) {
    initBuiltins();
}

std::optional<std::filesystem::path> getCompilerAdjacentXraphicsLib() {
#if defined(_WIN32)
    char pathBuffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(nullptr, pathBuffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::nullopt;
    }

    std::filesystem::path exePath(pathBuffer);
    std::filesystem::path exeDir = exePath.parent_path();
    // Accept the library either under internal/ or directly next to the compiler.
    std::filesystem::path internalCandidate = exeDir / "internal" / "xraphics.lib";
    if (std::filesystem::exists(internalCandidate)) {
        return internalCandidate;
    }
    std::filesystem::path flatCandidate = exeDir / "xraphics.lib";
    if (std::filesystem::exists(flatCandidate)) {
        return flatCandidate;
    }
#endif
    return std::nullopt;
}

// Locate the runtime helper library (bsod full-screen overlay) next to the compiler.
std::optional<std::filesystem::path> getCompilerAdjacentRuntimeLib() {
#if defined(_WIN32)
    char pathBuffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(nullptr, pathBuffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::nullopt;
    }
    std::filesystem::path exeDir = std::filesystem::path(pathBuffer).parent_path();
    std::filesystem::path internalCandidate = exeDir / "internal" / "xfawa_runtime.lib";
    if (std::filesystem::exists(internalCandidate)) {
        return internalCandidate;
    }
    std::filesystem::path flatCandidate = exeDir / "xfawa_runtime.lib";
    if (std::filesystem::exists(flatCandidate)) {
        return flatCandidate;
    }
#endif
    return std::nullopt;
}

LLVMCodegen::LLVMCodegen(llvm::LLVMContext& ctx, llvm::Module* mod, OptimizationLevel opt)
    : context(ctx), module(mod), builder(ctx), hasMainFunction(false), hasWindowStatements(false), usesRandomBuiltin(false), hasBsod(false), loopEndBB(nullptr), optLevel(opt), generatedWindowCount(0), activeWindowId(-1), xraphicsLogEnabled(false) {
    initBuiltins();
}

void LLVMCodegen::initBuiltins() {
    auto declareFunction = [this](const std::string& name, llvm::FunctionType* type) -> llvm::Function* {
        if (llvm::Function* existing = module->getFunction(name)) {
            return existing;
        }
        return llvm::Function::Create(type, llvm::Function::LinkageTypes::ExternalLinkage, 0, name, module);
    };

    llvm::Type* ptrTy = llvm::PointerType::get(context, 0);

    llvm::FunctionType* printfType = llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy}, true);
    llvm::Function* printfFunc = declareFunction("printf", printfType);
    printfFunc->setCallingConv(llvm::CallingConv::C);
    printfFunc->addFnAttr(llvm::Attribute::NoUnwind);
    declareFunction("snprintf", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy, builder.getInt64Ty(), ptrTy}, true));
    declareFunction("setvbuf", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy, ptrTy, builder.getInt32Ty(), builder.getInt64Ty()}, false));
    declareFunction("fflush", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy}, false));
    declareFunction("__acrt_iob_func", llvm::FunctionType::get(ptrTy, {builder.getInt32Ty()}, false));

    llvm::FunctionType* mallocType = llvm::FunctionType::get(ptrTy, {builder.getInt64Ty()}, false);
    declareFunction("malloc", mallocType);
    
    declareFunction("strtod", llvm::FunctionType::get(builder.getDoubleTy(), {ptrTy, ptrTy->getPointerTo()}, false));
    declareFunction("strtof", llvm::FunctionType::get(builder.getFloatTy(), {ptrTy, ptrTy->getPointerTo()}, false));
    declareFunction("strtol", llvm::FunctionType::get(builder.getInt64Ty(), {ptrTy, ptrTy->getPointerTo(), builder.getInt32Ty()}, false));
    declareFunction("strtoll", llvm::FunctionType::get(builder.getInt64Ty(), {ptrTy, ptrTy->getPointerTo(), builder.getInt32Ty()}, false));

    llvm::FunctionType* randType = llvm::FunctionType::get(builder.getInt32Ty(), false);
    declareFunction("rand", randType);

    llvm::FunctionType* srandType = llvm::FunctionType::get(builder.getVoidTy(), {builder.getInt32Ty()}, false);
    declareFunction("srand", srandType);

    llvm::FunctionType* timeType = llvm::FunctionType::get(builder.getInt64Ty(), {llvm::PointerType::get(context, 0)}, false);
    declareFunction("time", timeType);

    llvm::FunctionType* clockType = llvm::FunctionType::get(builder.getInt64Ty(), false);
    declareFunction("clock", clockType);

    declareFunction("GetModuleHandleA", llvm::FunctionType::get(ptrTy, {ptrTy}, false));
    declareFunction("FreeConsole", llvm::FunctionType::get(builder.getInt32Ty(), false));
    declareFunction("SetConsoleOutputCP", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty()}, false));
    declareFunction("SetConsoleCP", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty()}, false));
    declareFunction("LoadCursorA", llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy}, false));
    declareFunction("RegisterClassA", llvm::FunctionType::get(builder.getInt16Ty(), {ptrTy}, false));
    declareFunction("CreateWindowExA", llvm::FunctionType::get(
        ptrTy,
        {builder.getInt32Ty(), ptrTy, ptrTy, builder.getInt32Ty(), builder.getInt32Ty(),
         builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, ptrTy, ptrTy, ptrTy},
        false));
    declareFunction("CreateWindowExW", llvm::FunctionType::get(
        ptrTy,
        {builder.getInt32Ty(), ptrTy, ptrTy, builder.getInt32Ty(), builder.getInt32Ty(),
         builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, ptrTy, ptrTy, ptrTy},
        false));
    declareFunction("MessageBoxA", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy, ptrTy, ptrTy, builder.getInt32Ty()}, false));
    declareFunction("MessageBoxW", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy, ptrTy, ptrTy, builder.getInt32Ty()}, false));
    declareFunction("MultiByteToWideChar", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, builder.getInt32Ty(), ptrTy, builder.getInt32Ty()}, false));
    declareFunction("ShowWindow", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy, builder.getInt32Ty()}, false));
    declareFunction("UpdateWindow", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy}, false));
    declareFunction("GetMessageA", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy, ptrTy, builder.getInt32Ty(), builder.getInt32Ty()}, false));
    declareFunction("TranslateMessage", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy}, false));
    declareFunction("DispatchMessageA", llvm::FunctionType::get(builder.getInt64Ty(), {ptrTy}, false));
    declareFunction("DefWindowProcA", llvm::FunctionType::get(builder.getInt64Ty(), {ptrTy, builder.getInt32Ty(), builder.getInt64Ty(), builder.getInt64Ty()}, false));
    declareFunction("PostQuitMessage", llvm::FunctionType::get(builder.getVoidTy(), {builder.getInt32Ty()}, false));
    declareFunction("BeginPaint", llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy}, false));
    declareFunction("EndPaint", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy, ptrTy}, false));
    declareFunction("CreateSolidBrush", llvm::FunctionType::get(ptrTy, {builder.getInt32Ty()}, false));
    declareFunction("FillRect", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy, ptrTy, ptrTy}, false));
    declareFunction("DeleteObject", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy}, false));
    declareFunction("xfawa_window_begin", llvm::FunctionType::get(
        ptrTy,
        {builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, builder.getInt32Ty()},
        false));
    declareFunction("xfawa_window_add_button", llvm::FunctionType::get(
        builder.getInt32Ty(),
        {ptrTy, builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, ptrTy},
        false));
    declareFunction("xfawa_window_add_text", llvm::FunctionType::get(
        builder.getInt32Ty(),
        {ptrTy, builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), ptrTy},
        false));
    declareFunction("xfawa_window_add_box", llvm::FunctionType::get(
        builder.getInt32Ty(),
        {ptrTy, builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, ptrTy},
        false));
    declareFunction("xfawa_window_append_box", llvm::FunctionType::get(
        builder.getInt32Ty(),
        {ptrTy, ptrTy, ptrTy},
        false));
    declareFunction("xfawa_window_show", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy}, false));
    declareFunction("xfawa_windows_run", llvm::FunctionType::get(builder.getInt32Ty(), false));
    declareFunction("xr_create_window", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty(), builder.getInt32Ty(), ptrTy}, false));
    declareFunction("xr_show_window", llvm::FunctionType::get(builder.getInt32Ty(), false));
    declareFunction("xr_poll_events", llvm::FunctionType::get(builder.getInt32Ty(), false));
    declareFunction("xr_should_close", llvm::FunctionType::get(builder.getInt32Ty(), false));
    declareFunction("xr_begin_frame", llvm::FunctionType::get(builder.getInt32Ty(), false));
    declareFunction("xr_end_frame", llvm::FunctionType::get(builder.getInt32Ty(), false));
    declareFunction("xr_draw_rect", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty()}, false));
    declareFunction("xr_draw_text", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, builder.getInt32Ty()}, false));
    declareFunction("xr_draw_button", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, ptrTy}, false));
    declareFunction("xr_draw_box", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, ptrTy}, false));
    declareFunction("xr_draw_input", llvm::FunctionType::get(ptrTy, {builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), ptrTy, ptrTy}, false));
    declareFunction("xr_append_box", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy, ptrTy}, false));
    declareFunction("xr_load_style", llvm::FunctionType::get(builder.getInt32Ty(), {ptrTy}, false));
    declareFunction("xr_set_clear_color", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty()}, false));
    declareFunction("xr_set_xraphics_log", llvm::FunctionType::get(builder.getVoidTy(), {builder.getInt32Ty()}, false));
    declareFunction("xr_draw_canvas3d", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getFloatTy(), builder.getInt32Ty(), builder.getFloatTy()}, false));
    declareFunction("xr_add_scene_object", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty(), builder.getFloatTy(), builder.getFloatTy(), builder.getFloatTy(), builder.getFloatTy(), builder.getFloatTy(), builder.getFloatTy(), builder.getInt32Ty(), builder.getFloatTy()}, false));
    declareFunction("xr_clear_scene_objects", llvm::FunctionType::get(builder.getInt32Ty(), {}, false));

    // Input & Camera API (api.txt)
    declareFunction("xr_get_key", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty()}, false));
    declareFunction("xr_get_mouse_x", llvm::FunctionType::get(builder.getInt32Ty(), {}, false));
    declareFunction("xr_get_mouse_y", llvm::FunctionType::get(builder.getInt32Ty(), {}, false));
    declareFunction("xr_get_mouse_dx", llvm::FunctionType::get(builder.getInt32Ty(), {}, false));
    declareFunction("xr_get_mouse_dy", llvm::FunctionType::get(builder.getInt32Ty(), {}, false));
    declareFunction("xr_get_mouse_button", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getInt32Ty()}, false));
    declareFunction("xr_get_mouse_wheel", llvm::FunctionType::get(builder.getInt32Ty(), {}, false));
    declareFunction("xr_camera_create", llvm::FunctionType::get(builder.getVoidTy(), {builder.getFloatTy(), builder.getFloatTy(), builder.getFloatTy(), builder.getFloatTy(), builder.getFloatTy(), builder.getFloatTy()}, false));
    declareFunction("xr_camera_move", llvm::FunctionType::get(builder.getVoidTy(), {builder.getInt32Ty(), builder.getFloatTy()}, false));
    declareFunction("xr_camera_look", llvm::FunctionType::get(builder.getVoidTy(), {builder.getFloatTy(), builder.getFloatTy()}, false));
    declareFunction("xr_camera_get_yaw", llvm::FunctionType::get(builder.getFloatTy(), {}, false));
    declareFunction("xr_camera_get_pitch", llvm::FunctionType::get(builder.getFloatTy(), {}, false));
    declareFunction("xr_camera_get_fov", llvm::FunctionType::get(builder.getFloatTy(), {}, false));
    declareFunction("xr_camera_get_pos_x", llvm::FunctionType::get(builder.getFloatTy(), {}, false));
    declareFunction("xr_camera_get_pos_y", llvm::FunctionType::get(builder.getFloatTy(), {}, false));
    declareFunction("xr_camera_get_pos_z", llvm::FunctionType::get(builder.getFloatTy(), {}, false));
    declareFunction("xr_set_loop_callback", llvm::FunctionType::get(builder.getVoidTy(), {ptrTy}, false));
}

llvm::Type* LLVMCodegen::getLLVMType(VarType type) {
    switch (type) {
        case VarType::INT:
            return builder.getInt32Ty();
        case VarType::LONG:
            return builder.getInt64Ty();
        case VarType::FLOAT:
            return builder.getFloatTy();
        case VarType::BOOL:
            return builder.getInt1Ty();
        case VarType::STRING:
            return llvm::PointerType::get(context, 0);
        case VarType::ARRAY_INT:
            return llvm::PointerType::get(context, 0);
        case VarType::ARRAY_LONG:
            return llvm::PointerType::get(context, 0);
        case VarType::ARRAY_FLOAT:
            return llvm::PointerType::get(context, 0);
        case VarType::ARRAY_BOOL:
            return llvm::PointerType::get(context, 0);
        case VarType::ARRAY_STRING:
            return llvm::PointerType::get(context, 0);
        default:
            return builder.getInt32Ty();
    }
}

llvm::Type* LLVMCodegen::getArrayElementType(VarType type) {
    switch (type) {
        case VarType::ARRAY_INT:
            return builder.getInt32Ty();
        case VarType::ARRAY_LONG:
            return builder.getInt64Ty();
        case VarType::ARRAY_FLOAT:
            return builder.getFloatTy();
        case VarType::ARRAY_BOOL:
            return builder.getInt1Ty();
        case VarType::ARRAY_STRING:
            return builder.getInt8Ty()->getPointerTo();
        default:
            return builder.getInt32Ty();
    }
}

void LLVMCodegen::initializeTargets() {
    // Initialize X86 target
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();
    
    // Initialize native target
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
}

llvm::Value* LLVMCodegen::codegen(NumberLiteral* expr) {
    constexpr int64_t INT32_MAX_VAL = 2147483647LL;
    constexpr int64_t INT32_MIN_VAL = -2147483648LL;
    
    if (expr->value > INT32_MAX_VAL || expr->value < INT32_MIN_VAL) {
        return createConstInt(context, llvm::Type::getInt64Ty(context), expr->value);
    }
    return createConstInt(context, llvm::Type::getInt32Ty(context), expr->value);
}

llvm::Value* LLVMCodegen::codegen(FloatLiteral* expr) {
    llvm::APFloat apFloat((float)expr->value);
    return llvm::ConstantFP::get(builder.getFloatTy(), apFloat);
}

llvm::Value* LLVMCodegen::codegen(BooleanLiteral* expr) {
    return createConstInt(context, llvm::Type::getInt1Ty(context), expr->value ? 1 : 0);
}

llvm::Value* LLVMCodegen::codegen(StringLiteral* expr) {
    return builder.CreateGlobalString(expr->value, "str");
}

llvm::Value* LLVMCodegen::codegen(VariableExpression* expr) {
    auto it = locals.find(expr->name);
    if (it != locals.end()) {
        llvm::AllocaInst* alloca = it->second;
        return builder.CreateLoad(alloca->getAllocatedType(), alloca, expr->name.c_str());
    }
    
    auto globalIt = windowInputGlobals.find(expr->name);
    if (globalIt != windowInputGlobals.end()) {
        return builder.CreateLoad(globalIt->second->getValueType(), globalIt->second, expr->name.c_str());
    }
    
    addError("Undefined variable: " + expr->name);
    return nullptr;
}

// EXP `o`-literal (o / oo / ooo / 1o / 15o / ...).
// Value semantics:
//   - a pure run of 'o' (o, oo, ooo) is 10, 100, 1000, ... (10^n where n = #of o's)
//   - digits followed by 'o's (1o, 15o, 10o) is the digit prefix times 10^(#of o's)
// The raw text is "<digits><o...>" (digits may be empty, meaning an implicit 1).
llvm::Value* LLVMCodegen::codegen(OLiteralExpression* expr) {
    const std::string& raw = expr->raw;
    size_t oCount = 0;
    while (oCount < raw.size() && raw[raw.size() - 1 - oCount] == 'o') {
        oCount++;
    }
    int64_t prefix = 1;
    if (oCount < raw.size()) {
        // digits before the trailing o's
        std::string digits = raw.substr(0, raw.size() - oCount);
        try {
            prefix = std::stoll(digits);
        } catch (...) {
            prefix = 0;
        }
    }
    int64_t v = prefix;
    for (size_t i = 0; i < oCount; i++) {
        v *= 10;
    }
    return createConstInt(context, builder.getInt32Ty(), v);
}

// EXP `paradox`: a paradox value evaluates to integer 0 (deterministic). It is
// detected specially by `print` to output the literal "PARADOX".
llvm::Value* LLVMCodegen::codegen(ParadoxExpression* expr) {
    return createConstInt(context, builder.getInt32Ty(), 0);
}

// EXP `paradox` stage 2 (`幽灵论`): a GHOST variable keeps its stored value --
// evaluating it reads the exact same alloca a normal variable read would. The
// lost causal origin changes how `print` displays it (an appended "#"), not
// what the value is. Never rewritten into 0.
llvm::Value* LLVMCodegen::codegen(GhostExpression* expr) {
    auto it = locals.find(expr->name);
    if (it != locals.end()) {
        llvm::AllocaInst* alloca = it->second;
        return builder.CreateLoad(alloca->getAllocatedType(), alloca, expr->name.c_str());
    }
    auto globalIt = windowInputGlobals.find(expr->name);
    if (globalIt != windowInputGlobals.end()) {
        return builder.CreateLoad(globalIt->second->getValueType(), globalIt->second, expr->name.c_str());
    }
    addError("Undefined variable: " + expr->name);
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(UnaryOp* expr) {
    llvm::Value* operandVal = codegen(expr->expr.get());
    if (!operandVal) return nullptr;
    
    switch (expr->op) {
        case UnaryOpType::NEGATE:
            return builder.CreateNeg(operandVal, "negtmp");
        case UnaryOpType::NOT:
            return builder.CreateNot(operandVal, "nottmp");
        default:
            return nullptr;
    }
}

llvm::Value* LLVMCodegen::codegen(BinaryOp* expr) {
    // EXP `believe`: if both operands are constant ints, check the belief table.
    if (auto* l = dynamic_cast<NumberLiteral*>(expr->left.get())) {
        if (auto* r = dynamic_cast<NumberLiteral*>(expr->right.get())) {
            char opCh = binaryOpChar(expr->op);
            if (opCh != 0) {
                std::string key = std::to_string(l->value) + opCh + std::to_string(r->value);
                auto it = beliefMap.find(key);
                if (it != beliefMap.end()) {
                    return createConstInt(context, llvm::Type::getInt64Ty(context), it->second);
                }
            }
        }
    }

    // EXP `o`-literal addition ("位数合并"): when two o-literals are added, the
    // counts of 'o' merge (n1 + n2) instead of the values being added normally.
    // e.g. `1o + 1o == 100` (prefix 1, o-count 1+1=2), not 20.
    // This only fires for o-literal + o-literal; ordinary numbers are untouched.
    if (expr->op == BinaryOpType::ADD) {
        auto* oLeft = dynamic_cast<OLiteralExpression*>(expr->left.get());
        auto* oRight = dynamic_cast<OLiteralExpression*>(expr->right.get());
        if (oLeft && oRight) {
            auto parse = [](const std::string& raw, int64_t& prefix, size_t& oCount) {
                oCount = 0;
                while (oCount < raw.size() && raw[raw.size() - 1 - oCount] == 'o') {
                    oCount++;
                }
                prefix = 1;
                if (oCount < raw.size()) {
                    std::string digits = raw.substr(0, raw.size() - oCount);
                    try {
                        prefix = std::stoll(digits);
                    } catch (...) {
                        prefix = 0;
                    }
                }
            };
            int64_t p1; size_t c1; parse(oLeft->raw, p1, c1);
            int64_t p2; size_t c2; parse(oRight->raw, p2, c2);
            int64_t v = p1;
            size_t merged = c1 + c2;
            for (size_t i = 0; i < merged; i++) {
                v *= 10;
            }
            return createConstInt(context, builder.getInt32Ty(), v);
        }
    }

    llvm::Value* leftVal = codegen(expr->left.get());
    if (!leftVal) return nullptr;
    
    llvm::Value* rightVal = codegen(expr->right.get());
    if (!rightVal) return nullptr;
    
    llvm::Type* leftType = leftVal->getType();
    llvm::Type* rightType = rightVal->getType();
    
    bool isFloatOp = leftType->isFloatTy() || rightType->isFloatTy();
    bool isLongOp = leftType->isIntegerTy(64) || rightType->isIntegerTy(64);
    bool isPtrOp = leftType->isPointerTy() && rightType->isPointerTy();
    
    bool leftIsPtr = leftType->isPointerTy();
    bool rightIsPtr = rightType->isPointerTy();
    bool leftIsNum = leftType->isIntegerTy() || leftType->isFloatTy();
    bool rightIsNum = rightType->isIntegerTy() || rightType->isFloatTy();
    
    if (expr->op == BinaryOpType::ADD && ((leftIsPtr && rightIsNum) || (leftIsNum && rightIsPtr))) {
        auto convertNumToStr = [this](llvm::Value* numVal, llvm::Type* numType) -> llvm::Value* {
            llvm::Function* mallocFunc = module->getFunction("malloc");
            if (!mallocFunc) {
                llvm::FunctionType* mallocType = llvm::FunctionType::get(
                    builder.getInt8Ty()->getPointerTo(),
                    {builder.getInt64Ty()},
                    false);
                mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, 0, "malloc", module);
            }
            
            llvm::Value* buffer = builder.CreateCall(mallocFunc, {builder.getInt64(64)}, "num_str_buf");
            
            llvm::Function* sprintfFunc = module->getFunction("sprintf");
            if (!sprintfFunc) {
                llvm::FunctionType* sprintfType = llvm::FunctionType::get(
                    builder.getInt32Ty(),
                    {builder.getInt8Ty()->getPointerTo(), builder.getInt8Ty()->getPointerTo()},
                    true);
                sprintfFunc = llvm::Function::Create(sprintfType, llvm::Function::ExternalLinkage, 0, "sprintf", module);
            }
            
            llvm::Value* formatStr = nullptr;
            llvm::Value* valToPrint = numVal;
            if (numType->isFloatTy()) {
                formatStr = builder.CreateGlobalStringPtr("%g", "float_fmt");
                valToPrint = builder.CreateFPExt(numVal, builder.getDoubleTy(), "float_to_double");
            } else if (numType->isIntegerTy(64)) {
                formatStr = builder.CreateGlobalStringPtr("%lld", "long_fmt");
            } else {
                formatStr = builder.CreateGlobalStringPtr("%d", "int_fmt");
            }
            
            builder.CreateCall(sprintfFunc, {buffer, formatStr, valToPrint}, "sprintf_num");
            return buffer;
        };
        
        if (leftIsPtr && rightIsNum) {
            rightVal = convertNumToStr(rightVal, rightType);
        } else {
            leftVal = convertNumToStr(leftVal, leftType);
        }
        
        llvm::Function* strlenFunc = module->getFunction("strlen");
        if (!strlenFunc) {
            llvm::FunctionType* strlenType = llvm::FunctionType::get(builder.getInt64Ty(), {builder.getPtrTy()}, false);
            strlenFunc = llvm::Function::Create(strlenType, llvm::Function::ExternalLinkage, 0, "strlen", module);
        }
        
        llvm::Value* leftLen = builder.CreateCall(strlenFunc, {leftVal}, "left.len");
        llvm::Value* rightLen = builder.CreateCall(strlenFunc, {rightVal}, "right.len");
        
        llvm::Value* totalLen = builder.CreateAdd(leftLen, rightLen, "total.len");
        llvm::Value* totalLenPlus1 = builder.CreateAdd(totalLen, createConstInt(context, builder.getInt64Ty(), 1), "total.len.plus1");
        
        llvm::Function* mallocFunc = module->getFunction("malloc");
        llvm::Value* resultPtr = builder.CreateCall(mallocFunc, {totalLenPlus1}, "concat.alloc");
        
        llvm::Function* strcpyFunc = module->getFunction("strcpy");
        if (!strcpyFunc) {
            llvm::FunctionType* strcpyType = llvm::FunctionType::get(builder.getPtrTy(), {builder.getPtrTy(), builder.getPtrTy()}, false);
            strcpyFunc = llvm::Function::Create(strcpyType, llvm::Function::ExternalLinkage, 0, "strcpy", module);
        }
        builder.CreateCall(strcpyFunc, {resultPtr, leftVal}, "strcpy.left");
        
        llvm::Function* strcatFunc = module->getFunction("strcat");
        if (!strcatFunc) {
            llvm::FunctionType* strcatType = llvm::FunctionType::get(builder.getPtrTy(), {builder.getPtrTy(), builder.getPtrTy()}, false);
            strcatFunc = llvm::Function::Create(strcatType, llvm::Function::ExternalLinkage, 0, "strcat", module);
        }
        builder.CreateCall(strcatFunc, {resultPtr, rightVal}, "strcat.right");
        
        return resultPtr;
    }
    
    if (isPtrOp && expr->op == BinaryOpType::ADD) {
        llvm::Function* strlenFunc = module->getFunction("strlen");
        if (!strlenFunc) {
            llvm::FunctionType* strlenType = llvm::FunctionType::get(builder.getInt64Ty(), {builder.getPtrTy()}, false);
            strlenFunc = llvm::Function::Create(strlenType, llvm::Function::ExternalLinkage, 0, "strlen", module);
        }
        
        llvm::Value* leftLen = builder.CreateCall(strlenFunc, {leftVal}, "left.len");
        llvm::Value* rightLen = builder.CreateCall(strlenFunc, {rightVal}, "right.len");
        
        llvm::Value* totalLen = builder.CreateAdd(leftLen, rightLen, "total.len");
        llvm::Value* totalLenPlus1 = builder.CreateAdd(totalLen, createConstInt(context, builder.getInt64Ty(), 1), "total.len.plus1");
        
        llvm::Function* mallocFunc = module->getFunction("malloc");
        llvm::Value* resultPtr = builder.CreateCall(mallocFunc, {totalLenPlus1}, "concat.alloc");
        
        llvm::Function* strcpyFunc = module->getFunction("strcpy");
        if (!strcpyFunc) {
            llvm::FunctionType* strcpyType = llvm::FunctionType::get(builder.getPtrTy(), {builder.getPtrTy(), builder.getPtrTy()}, false);
            strcpyFunc = llvm::Function::Create(strcpyType, llvm::Function::ExternalLinkage, 0, "strcpy", module);
        }
        builder.CreateCall(strcpyFunc, {resultPtr, leftVal}, "strcpy.left");
        
        llvm::Function* strcatFunc = module->getFunction("strcat");
        if (!strcatFunc) {
            llvm::FunctionType* strcatType = llvm::FunctionType::get(builder.getPtrTy(), {builder.getPtrTy(), builder.getPtrTy()}, false);
            strcatFunc = llvm::Function::Create(strcatType, llvm::Function::ExternalLinkage, 0, "strcat", module);
        }
        builder.CreateCall(strcatFunc, {resultPtr, rightVal}, "strcat.right");
        
        return resultPtr;
    }
    
    if (leftType->isIntegerTy(1)) {
        llvm::Type* targetType = rightType->isIntegerTy(64) ? llvm::Type::getInt64Ty(context) : llvm::Type::getInt32Ty(context);
        leftVal = builder.CreateZExt(leftVal, targetType, "boolto");
    }
    if (rightType->isIntegerTy(1)) {
        llvm::Type* targetType = leftType->isIntegerTy(64) ? llvm::Type::getInt64Ty(context) : llvm::Type::getInt32Ty(context);
        rightVal = builder.CreateZExt(rightVal, targetType, "boolto");
    }
    
    // Ensure integer types match for operations
    leftType = leftVal->getType();
    rightType = rightVal->getType();
    
    if (leftType->isIntegerTy() && rightType->isIntegerTy()) {
        // If types don't match, convert to the larger type
        if (leftType->getIntegerBitWidth() > rightType->getIntegerBitWidth()) {
            rightVal = builder.CreateSExt(rightVal, leftType, "intext");
        } else if (rightType->getIntegerBitWidth() > leftType->getIntegerBitWidth()) {
            leftVal = builder.CreateSExt(leftVal, rightType, "intext");
        }
    }
    
    if (isFloatOp) {
        if (!leftType->isFloatTy()) {
            leftVal = builder.CreateSIToFP(leftVal, builder.getFloatTy(), "inttofp");
        }
        if (!rightType->isFloatTy()) {
            rightVal = builder.CreateSIToFP(rightVal, builder.getFloatTy(), "inttofp");
        }
        
        switch (expr->op) {
            case BinaryOpType::ADD:
                return builder.CreateFAdd(leftVal, rightVal, "addtmp");
            case BinaryOpType::SUB:
                return builder.CreateFSub(leftVal, rightVal, "subtmp");
            case BinaryOpType::MUL:
                return builder.CreateFMul(leftVal, rightVal, "multmp");
            case BinaryOpType::DIV:
                return builder.CreateFDiv(leftVal, rightVal, "fdivtmp");
            case BinaryOpType::MOD:
                return builder.CreateFRem(leftVal, rightVal, "fmodtmp");
            case BinaryOpType::EQUAL:
                return builder.CreateFCmpOEQ(leftVal, rightVal, "eqtmp");
            case BinaryOpType::NOT_EQUAL:
                return builder.CreateFCmpONE(leftVal, rightVal, "netmp");
            case BinaryOpType::LESS:
                return builder.CreateFCmpOLT(leftVal, rightVal, "lttmp");
            case BinaryOpType::LESS_EQUAL:
                return builder.CreateFCmpOLE(leftVal, rightVal, "letmp");
            case BinaryOpType::GREATER:
                return builder.CreateFCmpOGT(leftVal, rightVal, "gttmp");
            case BinaryOpType::GREATER_EQUAL:
                return builder.CreateFCmpOGE(leftVal, rightVal, "getmp");
            case BinaryOpType::AND:
                return builder.CreateAnd(leftVal, rightVal, "andtmp");
            case BinaryOpType::OR:
                return builder.CreateOr(leftVal, rightVal, "ortmp");
            default:
                return nullptr;
        }
    }
    
    switch (expr->op) {
        case BinaryOpType::ADD:
            return builder.CreateAdd(leftVal, rightVal, "addtmp");
        case BinaryOpType::SUB:
            return builder.CreateSub(leftVal, rightVal, "subtmp");
        case BinaryOpType::MUL:
            return builder.CreateMul(leftVal, rightVal, "multmp");
        case BinaryOpType::DIV:
            return builder.CreateSDiv(leftVal, rightVal, "divtmp");
        case BinaryOpType::MOD:
            return builder.CreateSRem(leftVal, rightVal, "modtmp");
        case BinaryOpType::EQUAL:
            return builder.CreateICmpEQ(leftVal, rightVal, "eqtmp");
        case BinaryOpType::NOT_EQUAL:
            return builder.CreateICmpNE(leftVal, rightVal, "netmp");
        case BinaryOpType::LESS:
            return builder.CreateICmpSLT(leftVal, rightVal, "lttmp");
        case BinaryOpType::LESS_EQUAL:
            return builder.CreateICmpSLE(leftVal, rightVal, "letmp");
        case BinaryOpType::GREATER:
            return builder.CreateICmpSGT(leftVal, rightVal, "gttmp");
        case BinaryOpType::GREATER_EQUAL:
            return builder.CreateICmpSGE(leftVal, rightVal, "getmp");
        case BinaryOpType::AND:
            return builder.CreateAnd(leftVal, rightVal, "andtmp");
        case BinaryOpType::OR:
            return builder.CreateOr(leftVal, rightVal, "ortmp");
        default:
            return nullptr;
    }
}

llvm::Value* LLVMCodegen::codegen(CallExpression* expr) {
    if (expr->name == "rnd") {
        usesRandomBuiltin = true;
        if (expr->args.size() == 1) {
            llvm::Value* arrVal = codegen(expr->args[0].get());
            if (!arrVal) return nullptr;
            
            int64_t knownArrayLen = 0;
            if (auto* varExpr = dynamic_cast<VariableExpression*>(expr->args[0].get())) {
                auto lenIt = arrayLengths.find(varExpr->name);
                if (lenIt != arrayLengths.end()) {
                    knownArrayLen = lenIt->second;
                }
            }
            
            llvm::Function* randFunc = module->getFunction("rand");
            llvm::Value* randVal = builder.CreateCall(randFunc, {}, "rand.val");
            
            llvm::Value* arrLen;
            if (knownArrayLen > 0) {
                arrLen = createConstInt(context, builder.getInt32Ty(), knownArrayLen);
            } else {
                addError("rnd(array): array length must be known at compile time");
                return nullptr;
            }
            
            llvm::Value* modVal = builder.CreateSRem(randVal, arrLen, "rand.idx");
            llvm::Value* idxExt = builder.CreateSExt(modVal, builder.getInt64Ty(), "idx.ext");
            llvm::Value* elemPtr = builder.CreateGEP(builder.getInt32Ty(), arrVal, idxExt, "rnd.elem.ptr");
            return builder.CreateLoad(builder.getInt32Ty(), elemPtr, "rnd.elem");
        }
        else if (expr->args.size() == 2) {
            llvm::Value* minVal = codegen(expr->args[0].get());
            if (!minVal) return nullptr;
            
            llvm::Value* maxVal = codegen(expr->args[1].get());
            if (!maxVal) return nullptr;
            
            llvm::Function* randFunc = module->getFunction("rand");
            llvm::Value* randVal = builder.CreateCall(randFunc, {}, "rand.val");
            
            // Normalize all operands to i64 so the arithmetic is type-safe
            // (literal args codegen to i32, variable/param args to i32/i64).
            llvm::Type* i64Ty = builder.getInt64Ty();
            llvm::Value* minWide = builder.CreateSExtOrTrunc(minVal, i64Ty, "rnd.min.wide");
            llvm::Value* maxWide = builder.CreateSExtOrTrunc(maxVal, i64Ty, "rnd.max.wide");
            llvm::Value* randWide = builder.CreateZExt(randVal, i64Ty, "rnd.rand.wide");
            
            llvm::Value* range = builder.CreateSub(maxWide, minWide, "range.sub");
            range = builder.CreateAdd(range, builder.getInt64(1), "range.size");
            llvm::Value* modVal = builder.CreateSRem(randWide, range, "rand.mod");
            return builder.CreateAdd(modVal, minWide, "rnd.result");
        }
        else {
            addError("rnd() requires 1 (array) or 2 (min, max) arguments");
            return nullptr;
        }
    }
    
    if (expr->name == "input") {
        llvm::Function* fgetsFunc = module->getFunction("fgets");
        if (!fgetsFunc) {
            llvm::FunctionType* fgetsType = llvm::FunctionType::get(
                builder.getInt8Ty()->getPointerTo(),
                {builder.getInt8Ty()->getPointerTo(), builder.getInt32Ty(), builder.getInt8Ty()->getPointerTo()},
                false);
            fgetsFunc = llvm::Function::Create(fgetsType, llvm::Function::ExternalLinkage, 0, "fgets", module);
        }
        
        llvm::Function* stdinFunc = module->getFunction("__acrt_iob_func");
        if (!stdinFunc) {
            llvm::FunctionType* stdinType = llvm::FunctionType::get(
                builder.getInt8Ty()->getPointerTo(),
                {builder.getInt32Ty()},
                false);
            stdinFunc = llvm::Function::Create(stdinType, llvm::Function::ExternalLinkage, 0, "__acrt_iob_func", module);
        }
        
        llvm::Function* mallocFunc = module->getFunction("malloc");
        if (!mallocFunc) {
            llvm::FunctionType* mallocType = llvm::FunctionType::get(
                builder.getInt8Ty()->getPointerTo(),
                {builder.getInt64Ty()},
                false);
            mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, 0, "malloc", module);
        }
        
        llvm::Value* bufferSize = builder.getInt64(1024);
        llvm::Value* buffer = builder.CreateCall(mallocFunc, {bufferSize}, "input_buffer");
        
        llvm::Value* stdinValue = builder.CreateCall(stdinFunc, {builder.getInt32(0)}, "stdin_val");
        builder.CreateCall(fgetsFunc, {buffer, builder.getInt32(1023), stdinValue}, "fgets_call");
        
        llvm::Function* strlenFunc = module->getFunction("strlen");
        if (!strlenFunc) {
            llvm::FunctionType* strlenType = llvm::FunctionType::get(
                builder.getInt64Ty(),
                {builder.getInt8Ty()->getPointerTo()},
                false);
            strlenFunc = llvm::Function::Create(strlenType, llvm::Function::ExternalLinkage, 0, "strlen", module);
        }
        
        llvm::Value* len = builder.CreateCall(strlenFunc, {buffer}, "input_len");
        llvm::Value* lenNotZero = builder.CreateICmpNE(len, builder.getInt64(0), "len_not_zero");
        
        llvm::Function* currentFunc = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* checkNewlineBB = llvm::BasicBlock::Create(context, "check_newline", currentFunc);
        llvm::BasicBlock* trimBB = llvm::BasicBlock::Create(context, "trim_newline", currentFunc);
        llvm::BasicBlock* noTrimBB = llvm::BasicBlock::Create(context, "no_trim_newline", currentFunc);
        llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(context, "input_done", currentFunc);
        
        builder.CreateCondBr(lenNotZero, checkNewlineBB, noTrimBB);
        
        builder.SetInsertPoint(checkNewlineBB);
        llvm::Value* lastIdx = builder.CreateSub(len, builder.getInt64(1), "last_idx");
        llvm::Value* lastCharPtr = builder.CreateGEP(builder.getInt8Ty(), buffer, lastIdx, "last_char_ptr");
        llvm::Value* lastChar = builder.CreateLoad(builder.getInt8Ty(), lastCharPtr, "last_char");
        llvm::Value* isNewline = builder.CreateICmpEQ(lastChar, builder.getInt8('\n'), "is_newline");
        builder.CreateCondBr(isNewline, trimBB, noTrimBB);
        
        builder.SetInsertPoint(trimBB);
        builder.CreateStore(builder.getInt8('\0'), lastCharPtr);
        builder.CreateBr(doneBB);
        
        builder.SetInsertPoint(noTrimBB);
        builder.CreateBr(doneBB);
        
        builder.SetInsertPoint(doneBB);

        return buffer;
    }

    // === api.txt input/camera special call expressions ===
    // Mouse properties: __mouse_x, __mouse_y, __mouse_dx, __mouse_dy, __mouse_left, __mouse_right, __mouse_middle, __mouse_wheel
    if (expr->name == "__mouse_x") {
        llvm::Function* f = module->getFunction("xr_get_mouse_x");
        return builder.CreateCall(f, {}, "mouse.x");
    }
    if (expr->name == "__mouse_y") {
        llvm::Function* f = module->getFunction("xr_get_mouse_y");
        return builder.CreateCall(f, {}, "mouse.y");
    }
    if (expr->name == "__mouse_dx") {
        llvm::Function* f = module->getFunction("xr_get_mouse_dx");
        return builder.CreateCall(f, {}, "mouse.dx");
    }
    if (expr->name == "__mouse_dy") {
        llvm::Function* f = module->getFunction("xr_get_mouse_dy");
        return builder.CreateCall(f, {}, "mouse.dy");
    }
    if (expr->name == "__mouse_left") {
        llvm::Function* f = module->getFunction("xr_get_mouse_button");
        return builder.CreateCall(f, {builder.getInt32(0)}, "mouse.left");
    }
    if (expr->name == "__mouse_right") {
        llvm::Function* f = module->getFunction("xr_get_mouse_button");
        return builder.CreateCall(f, {builder.getInt32(1)}, "mouse.right");
    }
    if (expr->name == "__mouse_middle") {
        llvm::Function* f = module->getFunction("xr_get_mouse_button");
        return builder.CreateCall(f, {builder.getInt32(2)}, "mouse.middle");
    }
    if (expr->name == "__mouse_wheel") {
        llvm::Function* f = module->getFunction("xr_get_mouse_wheel");
        return builder.CreateCall(f, {}, "mouse.wheel");
    }

    // Keyboard keys: __keyboard_w/a/s/d/space/shift/ctrl → VK codes
    // VK_W=0x57, VK_A=0x41, VK_S=0x53, VK_D=0x44, VK_SPACE=0x20, VK_SHIFT=0x10, VK_CONTROL=0x11
    if (expr->name.rfind("__keyboard_", 0) == 0) {
        std::string keyName = expr->name.substr(11);  // after "__keyboard_"
        int vkCode = 0;
        if (keyName == "w") vkCode = 0x57;
        else if (keyName == "a") vkCode = 0x41;
        else if (keyName == "s") vkCode = 0x53;
        else if (keyName == "d") vkCode = 0x44;
        else if (keyName == "space") vkCode = 0x20;
        else if (keyName == "shift") vkCode = 0x10;
        else if (keyName == "ctrl") vkCode = 0x11;
        else {
            addError("Unknown keyboard key: " + keyName);
            return nullptr;
        }
        llvm::Function* f = module->getFunction("xr_get_key");
        return builder.CreateCall(f, {builder.getInt32(vkCode)}, ("keyboard." + keyName).c_str());
    }

    // Camera move: __camera_move_forward/backward/left/right/up/down(distance)
    if (expr->name.rfind("__camera_move_", 0) == 0) {
        std::string dirName = expr->name.substr(14);  // after "__camera_move_"
        int dir = 0;
        if (dirName == "forward") dir = 0;
        else if (dirName == "backward") dir = 1;
        else if (dirName == "left") dir = 2;
        else if (dirName == "right") dir = 3;
        else if (dirName == "up") dir = 4;
        else if (dirName == "down") dir = 5;
        else {
            addError("Unknown camera move direction: " + dirName);
            return nullptr;
        }
        llvm::Value* distVal = nullptr;
        if (!expr->args.empty()) {
            distVal = codegen(expr->args[0].get());
            if (!distVal) return nullptr;
            // Convert to float if needed
            if (distVal->getType() == builder.getInt32Ty()) {
                distVal = builder.CreateSIToFP(distVal, builder.getFloatTy(), "dist.f");
            }
        } else {
            distVal = llvm::ConstantFP::get(builder.getFloatTy(), 0.1f);
        }
        llvm::Function* f = module->getFunction("xr_camera_move");
        return builder.CreateCall(f, {builder.getInt32(dir), distVal});
    }

    // Camera look: __camera_look(deltaYaw, deltaPitch)
    if (expr->name == "__camera_look") {
        llvm::Value* dyaw = nullptr, *dpitch = nullptr;
        if (expr->args.size() >= 1) {
            dyaw = codegen(expr->args[0].get());
            if (dyaw && dyaw->getType() == builder.getInt32Ty()) {
                dyaw = builder.CreateSIToFP(dyaw, builder.getFloatTy(), "yaw.f");
            }
        }
        if (expr->args.size() >= 2) {
            dpitch = codegen(expr->args[1].get());
            if (dpitch && dpitch->getType() == builder.getInt32Ty()) {
                dpitch = builder.CreateSIToFP(dpitch, builder.getFloatTy(), "pitch.f");
            }
        }
        if (!dyaw) dyaw = llvm::ConstantFP::get(builder.getFloatTy(), 0.0f);
        if (!dpitch) dpitch = llvm::ConstantFP::get(builder.getFloatTy(), 0.0f);
        llvm::Function* f = module->getFunction("xr_camera_look");
        return builder.CreateCall(f, {dyaw, dpitch});
    }

    std::string funcName = expr->ns.empty() ? expr->name : (expr->ns + ":" + expr->name);
    llvm::Function* callee = module->getFunction(funcName);
    
    // If function not found and no namespace specified, try to find by name only
    if (!callee && expr->ns.empty()) {
        callee = module->getFunction(expr->name);
        
        // If still not found, search for functions with matching name
        if (!callee) {
            for (auto it = module->begin(); it != module->end(); ++it) {
                std::string fname = it->getName().str();
                size_t colonPos = fname.find(':');
                if (colonPos != std::string::npos) {
                    std::string pureName = fname.substr(colonPos + 1);
                    if (pureName == expr->name) {
                        callee = &(*it);
                        break;
                    }
                }
            }
        }
    }
    
    if (!callee) {
        // Provide suggestions for similar function names
        std::vector<std::string> candidates;
        for (auto it = module->begin(); it != module->end(); ++it) {
            candidates.push_back(it->getName().str());
        }
        
        std::vector<std::string> suggestions = findSuggestions(funcName, candidates, 3, 3);
        
        std::string errorMsg = "Unknown function: " + funcName;
        if (!suggestions.empty()) {
            errorMsg += "\n  Did you mean:\n";
            for (size_t i = 0; i < suggestions.size(); i++) {
                errorMsg += "    " + std::to_string(i + 1) + ". " + suggestions[i] + "\n";
            }
        }
        
        addError(errorMsg);
        return nullptr;
    }
    
    std::vector<llvm::Value*> args;
    std::vector<VarType> argTypes;
    for (auto& arg : expr->args) {
        VarType argType = VarType::UNKNOWN;
        if (auto* strLit = dynamic_cast<StringLiteral*>(arg.get())) {
            argType = VarType::STRING;
        } else if (auto* numLit = dynamic_cast<NumberLiteral*>(arg.get())) {
            constexpr int64_t INT32_MAX_VAL = 2147483647LL;
            if (numLit->value > INT32_MAX_VAL || numLit->value < -INT32_MAX_VAL - 1) {
                argType = VarType::LONG;
            } else {
                argType = VarType::INT;
            }
        } else if (auto* varExpr = dynamic_cast<VariableExpression*>(arg.get())) {
            auto typeIt = localTypes.find(varExpr->name);
            if (typeIt != localTypes.end()) {
                argType = typeIt->second;
            }
        }
        argTypes.push_back(argType);
        
        llvm::Value* argVal = codegen(arg.get());
        if (!argVal) return nullptr;
        
        // Get function parameter type for this argument
        auto argTypesIt = callArgTypes.find(funcName);
        VarType expectedParamType = VarType::UNKNOWN;
        if (argTypesIt != callArgTypes.end() && args.size() < argTypesIt->second.size()) {
            expectedParamType = argTypesIt->second[args.size()];
        }
        
        // Convert argument to match function signature
        llvm::FunctionType* funcType = callee->getFunctionType();
        llvm::Type* expectedLLVMType = funcType->getParamType(args.size());
        
        // Convert argument to expected type
        if (expectedLLVMType->isIntegerTy(64)) {
            // Function expects int64
            if (argVal->getType()->isIntegerTy(1)) {
                argVal = builder.CreateZExt(argVal, expectedLLVMType, "argbool");
            } else if (argVal->getType()->isIntegerTy(32)) {
                argVal = builder.CreateSExt(argVal, expectedLLVMType, "argext");
            } else if (argVal->getType()->isPointerTy()) {
                argVal = builder.CreatePtrToInt(argVal, expectedLLVMType, "argptr2int");
            }
        } else if (expectedLLVMType->isIntegerTy(32)) {
            // Function expects int32
            if (argVal->getType()->isIntegerTy(1)) {
                argVal = builder.CreateZExt(argVal, expectedLLVMType, "argbool");
            } else if (argVal->getType()->isIntegerTy(64)) {
                argVal = builder.CreateTrunc(argVal, expectedLLVMType, "argtrunc");
            } else if (argVal->getType()->isPointerTy()) {
                argVal = builder.CreatePtrToInt(argVal, llvm::Type::getInt64Ty(context), "argptr2int");
                argVal = builder.CreateTrunc(argVal, expectedLLVMType, "argtrunc");
            }
        } else if (expectedLLVMType->isPointerTy()) {
            // Function expects pointer
            if (argVal->getType()->isIntegerTy(1)) {
                argVal = builder.CreateZExt(argVal, llvm::Type::getInt64Ty(context), "argbool");
                argVal = builder.CreateIntToPtr(argVal, expectedLLVMType, "argboolptr");
            } else if (argVal->getType()->isIntegerTy(32)) {
                argVal = builder.CreateSExt(argVal, llvm::Type::getInt64Ty(context), "argext");
                argVal = builder.CreateIntToPtr(argVal, expectedLLVMType, "argextptr");
            } else if (argVal->getType()->isIntegerTy(64)) {
                argVal = builder.CreateIntToPtr(argVal, expectedLLVMType, "argptr");
            }
        }
        
        args.push_back(argVal);
    }
    
    callArgTypes[funcName] = argTypes;
    
    llvm::CallInst* callInst = builder.CreateCall(callee->getFunctionType(), callee, args);
    
    auto retTypeIt = funcReturnTypes.find(funcName);
    if (retTypeIt != funcReturnTypes.end()) {
        if (retTypeIt->second == VarType::FLOAT) {
            llvm::Value* bits = callInst;
            bits = builder.CreateBitCast(bits, builder.getDoubleTy(), "float.ret.bits");
            return builder.CreateFPTrunc(bits, builder.getFloatTy(), "float.ret");
        } else if (retTypeIt->second == VarType::BOOL) {
            return builder.CreateTrunc(callInst, builder.getInt1Ty(), "bool.ret");
        } else if (retTypeIt->second == VarType::STRING) {
            // Convert int64 back to pointer for string return
            return builder.CreateIntToPtr(callInst, builder.getPtrTy(), "string.ret.ptr");
        } else if (retTypeIt->second == VarType::INT) {
            return builder.CreateTrunc(callInst, builder.getInt32Ty(), "int.ret");
        }
    }
    
    return callInst;
}

llvm::Value* LLVMCodegen::codegen(ArrayRangeExpression* expr) {
    if (expr->isSlice && expr->array) {
        llvm::Value* arrVal = codegen(expr->array.get());
        if (!arrVal) return nullptr;
        
        llvm::Value* startVal = codegen(expr->start.get());
        if (!startVal) return nullptr;
        
        llvm::Value* endVal = codegen(expr->end.get());
        if (!endVal) return nullptr;
        
        int64_t knownArrayLen = 0;
        if (auto* varExpr = dynamic_cast<VariableExpression*>(expr->array.get())) {
            auto lenIt = arrayLengths.find(varExpr->name);
            if (lenIt != arrayLengths.end()) {
                knownArrayLen = lenIt->second;
            }
        }
        
        llvm::Value* sliceSize = builder.CreateSub(endVal, startVal, "slice.size");
        sliceSize = builder.CreateAdd(sliceSize, createConstInt(context, builder.getInt32Ty(), 1), "slice.size.adj");
        
        llvm::Value* sliceSizeExt = builder.CreateSExt(sliceSize, builder.getInt64Ty(), "slice.size.ext");
        llvm::Value* allocSize = builder.CreateMul(sliceSizeExt, createConstInt(context, builder.getInt64Ty(), 4), "alloc.size");
        
        llvm::Function* mallocFunc = module->getFunction("malloc");
        if (!mallocFunc) {
            llvm::FunctionType* mallocType = llvm::FunctionType::get(llvm::PointerType::get(context, 0), {builder.getInt64Ty()}, false);
            mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, 0, "malloc", module);
        }
        llvm::Value* rawPtr = builder.CreateCall(mallocFunc, {allocSize}, "slice.alloc");
        llvm::Value* slicePtr = rawPtr;
        
        llvm::Function* func = builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(context, "slice.cond", func);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "slice.body", func);
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(context, "slice.end", func);
        
        llvm::AllocaInst* iAlloca = builder.CreateAlloca(builder.getInt32Ty(), nullptr, "slice.i");
        builder.CreateStore(createConstInt(context, builder.getInt32Ty(), 0), iAlloca);
        
        builder.CreateBr(condBB);
        
        builder.SetInsertPoint(condBB);
        llvm::Value* iVal = builder.CreateLoad(builder.getInt32Ty(), iAlloca, "slice.i.val");
        llvm::Value* cond = builder.CreateICmpSLT(iVal, sliceSize, "slice.cond");
        builder.CreateCondBr(cond, bodyBB, endBB);
        
        builder.SetInsertPoint(bodyBB);
        llvm::Value* srcIdx = builder.CreateAdd(startVal, iVal, "src.idx");
        llvm::Value* srcIdxExt = builder.CreateSExt(srcIdx, builder.getInt64Ty(), "src.idx.ext");
        llvm::Value* srcElemPtr = builder.CreateGEP(builder.getInt32Ty(), arrVal, srcIdxExt, "src.elem.ptr");
        llvm::Value* elem = builder.CreateLoad(builder.getInt32Ty(), srcElemPtr, "elem");
        
        llvm::Value* dstElemPtr = builder.CreateGEP(builder.getInt32Ty(), slicePtr, builder.CreateSExt(iVal, builder.getInt64Ty()), "dst.elem.ptr");
        builder.CreateStore(elem, dstElemPtr);
        
        llvm::Value* nextI = builder.CreateAdd(iVal, createConstInt(context, builder.getInt32Ty(), 1), "next.i");
        builder.CreateStore(nextI, iAlloca);
        builder.CreateBr(condBB);
        
        builder.SetInsertPoint(endBB);
        
        return slicePtr;
    }
    
    llvm::Value* startVal = codegen(expr->start.get());
    if (!startVal) return nullptr;
    
    llvm::Value* endVal = codegen(expr->end.get());
    if (!endVal) return nullptr;
    
    llvm::Function* func = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* entryBB = builder.GetInsertBlock();
    
    llvm::BasicBlock* randBB = llvm::BasicBlock::Create(context, "rand.block", func);
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context, "rand.merge", func);
    
    llvm::Value* isRandom = builder.CreateICmpEQ(
        builder.CreateGlobalString(expr->accessType, "access.type"),
        builder.CreateGlobalString("rnd", "rnd.str")
    );
    
    builder.CreateCondBr(isRandom, randBB, mergeBB);
    
    builder.SetInsertPoint(randBB);
    usesRandomBuiltin = true;
    llvm::Function* randFunc = module->getFunction("rand");
    if (!randFunc) {
        llvm::FunctionType* randType = llvm::FunctionType::get(builder.getInt32Ty(), false);
        randFunc = llvm::Function::Create(randType, llvm::Function::ExternalLinkage, 0, "rand", module);
    }
    
    llvm::Value* randVal = builder.CreateCall(randFunc);
    llvm::Value* range = builder.CreateSub(endVal, startVal);
    range = builder.CreateAdd(range, createConstInt(context, builder.getInt32Ty(), 1));
    llvm::Value* modVal = builder.CreateSRem(randVal, range);
    llvm::Value* result = builder.CreateAdd(modVal, startVal);
    builder.CreateBr(mergeBB);
    
    builder.SetInsertPoint(mergeBB);
    llvm::PHINode* phi = builder.CreatePHI(builder.getInt32Ty(), 2, "range.result");
    phi->addIncoming(result, randBB);
    phi->addIncoming(startVal, entryBB);
    
    return phi;
}

llvm::Value* LLVMCodegen::codegen(ArrayLiteral* expr) {
    if (expr->isRange) {
        llvm::Value* startVal = codegen(expr->rangeStart.get());
        if (!startVal) return nullptr;
        
        llvm::Value* endVal = codegen(expr->rangeEnd.get());
        if (!endVal) return nullptr;
        
        if (auto* startInt = llvm::dyn_cast<llvm::ConstantInt>(startVal)) {
            if (auto* endInt = llvm::dyn_cast<llvm::ConstantInt>(endVal)) {
                int64_t start = startInt->getSExtValue();
                int64_t end = endInt->getSExtValue();
                int64_t size = end - start + 1;
                
                if (size > 0 && size < 10000) {
                    llvm::Function* mallocFunc = module->getFunction("malloc");
                    llvm::Value* allocSize = createConstInt(context, builder.getInt64Ty(), size * 4);
                    llvm::Value* arrPtr = builder.CreateCall(mallocFunc, {allocSize}, "arr.alloc");
                    
                    for (int64_t i = 0; i < size; i++) {
                        llvm::Value* idx = createConstInt(context, builder.getInt64Ty(), i);
                        llvm::Value* elemPtr = builder.CreateGEP(builder.getInt32Ty(), arrPtr, idx, "elem.ptr");
                        llvm::Value* val = createConstInt(context, builder.getInt32Ty(), start + i);
                        builder.CreateStore(val, elemPtr);
                    }
                    
                    return arrPtr;
                }
            }
        }
        
        addError("Range array must have constant integer bounds");
        return nullptr;
    }
    
    size_t size = expr->elements.size();
    if (size == 0) {
        return llvm::ConstantPointerNull::get(builder.getPtrTy());
    }
    
    bool isStringArray = false;
    bool isLongArray = false;
    size_t elemSize = 4;
    
    if (!expr->elements.empty()) {
        if (auto* strLit = dynamic_cast<StringLiteral*>(expr->elements[0].get())) {
            isStringArray = true;
            elemSize = 8;
            (void)strLit;
        } else if (auto* numLit = dynamic_cast<NumberLiteral*>(expr->elements[0].get())) {
            constexpr int64_t INT32_MAX_VAL = 2147483647LL;
            if (numLit->value > INT32_MAX_VAL || numLit->value < -INT32_MAX_VAL - 1) {
                isLongArray = true;
                elemSize = 8;
            }
        }
    }
    
    llvm::Function* mallocFunc = module->getFunction("malloc");
    llvm::Value* allocSize = createConstInt(context, builder.getInt64Ty(), size * elemSize);
    llvm::Value* arrPtr = builder.CreateCall(mallocFunc, {allocSize}, "arr.alloc");
    
    llvm::Type* elemType = builder.getInt32Ty();
    if (isStringArray) {
        elemType = static_cast<llvm::Type*>(builder.getInt8Ty()->getPointerTo());
    } else if (isLongArray) {
        elemType = static_cast<llvm::Type*>(builder.getInt64Ty());
    }
    
    for (size_t i = 0; i < size; i++) {
        llvm::Value* elemVal = codegen(expr->elements[i].get());
        if (!elemVal) return nullptr;
        
        if (!isStringArray) {
            if (elemVal->getType()->isIntegerTy(1)) {
                elemVal = builder.CreateZExtOrTrunc(elemVal, isLongArray ? builder.getInt64Ty() : builder.getInt32Ty(), "boolext");
            }
        }
        
        llvm::Value* idx = createConstInt(context, builder.getInt64Ty(), i);
        llvm::Value* elemPtr = builder.CreateGEP(elemType, arrPtr, idx, "elem.ptr");
        builder.CreateStore(elemVal, elemPtr);
    }
    
    return arrPtr;
}

llvm::Value* LLVMCodegen::codegen(ArrayIndexExpression* expr) {
    llvm::Value* arrVal = codegen(expr->array.get());
    if (!arrVal) return nullptr;
    
    llvm::Value* idxVal = codegen(expr->index.get());
    if (!idxVal) return nullptr;
    
    int64_t knownArrayLen = 0;
    VarType arrType = VarType::UNKNOWN;
    if (auto* varExpr = dynamic_cast<VariableExpression*>(expr->array.get())) {
        auto lenIt = arrayLengths.find(varExpr->name);
        if (lenIt != arrayLengths.end()) {
            knownArrayLen = lenIt->second;
        }
        auto typeIt = localTypes.find(varExpr->name);
        if (typeIt != localTypes.end()) {
            arrType = typeIt->second;
        }
    }
    
    llvm::Value* finalIdx = idxVal;
    
    if (auto* constIdx = llvm::dyn_cast<llvm::ConstantInt>(idxVal)) {
        int64_t idx = constIdx->getSExtValue();
        if (idx < 0 && knownArrayLen > 0) {
            int64_t adjustedIdx = knownArrayLen + idx;
            finalIdx = createConstInt(context, builder.getInt64Ty(), adjustedIdx);
        } else if (idx < 0) {
            addWarning("Negative array index without known array length");
        }
    } else {
        if (knownArrayLen > 0) {
            llvm::Value* negCheck = builder.CreateICmpSLT(idxVal, createConstInt(context, builder.getInt32Ty(), 0), "neg.check");
            llvm::Value* adjustedIdx = builder.CreateAdd(
                createConstInt(context, builder.getInt32Ty(), knownArrayLen),
                idxVal,
                "adj.idx"
            );
            finalIdx = builder.CreateSelect(negCheck, adjustedIdx, idxVal, "final.idx");
        }
    }
    
    llvm::Value* idxExt = finalIdx;
    if (finalIdx->getType()->isIntegerTy(32)) {
        idxExt = builder.CreateSExt(finalIdx, builder.getInt64Ty(), "idx.ext");
    }
    
    llvm::Type* elemType = getArrayElementType(arrType);
    llvm::Value* elemPtr = builder.CreateGEP(elemType, arrVal, idxExt, "arr.elem.ptr");
    return builder.CreateLoad(elemType, elemPtr, "arr.elem");
}

llvm::Value* LLVMCodegen::codegen(ExpressionStatement* stmt) {
    return codegen(stmt->expr.get());
}

llvm::Value* LLVMCodegen::codegen(AssignmentStatement* stmt) {
    int64_t arrayLen = 0;
    if (auto* arrLit = dynamic_cast<ArrayLiteral*>(stmt->value.get())) {
        if (arrLit->isRange) {
            if (auto* startInt = dynamic_cast<NumberLiteral*>(arrLit->rangeStart.get())) {
                if (auto* endInt = dynamic_cast<NumberLiteral*>(arrLit->rangeEnd.get())) {
                    arrayLen = endInt->value - startInt->value + 1;
                }
            }
        } else {
            arrayLen = arrLit->elements.size();
        }
    } else if (auto* arrSlice = dynamic_cast<ArrayRangeExpression*>(stmt->value.get())) {
        if (arrSlice->isSlice) {
            if (auto* startInt = dynamic_cast<NumberLiteral*>(arrSlice->start.get())) {
                if (auto* endInt = dynamic_cast<NumberLiteral*>(arrSlice->end.get())) {
                    arrayLen = endInt->value - startInt->value + 1;
                }
            }
        }
    }
    
    llvm::Value* value = nullptr;
    
    if (auto* callExpr = dynamic_cast<CallExpression*>(stmt->value.get())) {
        if (callExpr->name == "input" && stmt->hasExplicitType) {
            llvm::Function* fgetsFunc = module->getFunction("fgets");
            if (!fgetsFunc) {
                llvm::FunctionType* fgetsType = llvm::FunctionType::get(
                    builder.getInt8Ty()->getPointerTo(),
                    {builder.getInt8Ty()->getPointerTo(), builder.getInt32Ty(), builder.getInt8Ty()->getPointerTo()},
                    false);
                fgetsFunc = llvm::Function::Create(fgetsType, llvm::Function::ExternalLinkage, 0, "fgets", module);
            }
            
            llvm::Function* stdinFunc = module->getFunction("__acrt_iob_func");
            if (!stdinFunc) {
                llvm::FunctionType* stdinType = llvm::FunctionType::get(
                    builder.getInt8Ty()->getPointerTo(),
                    {builder.getInt32Ty()},
                    false);
                stdinFunc = llvm::Function::Create(stdinType, llvm::Function::ExternalLinkage, 0, "__acrt_iob_func", module);
            }
            
            llvm::Function* mallocFunc = module->getFunction("malloc");
            if (!mallocFunc) {
                llvm::FunctionType* mallocType = llvm::FunctionType::get(
                    builder.getInt8Ty()->getPointerTo(),
                    {builder.getInt64Ty()},
                    false);
                mallocFunc = llvm::Function::Create(mallocType, llvm::Function::ExternalLinkage, 0, "malloc", module);
            }
            
            llvm::Value* bufferSize = builder.getInt64(256);
            llvm::Value* buffer = builder.CreateCall(mallocFunc, {bufferSize}, "input_buffer");
            
            llvm::Value* stdinValue = builder.CreateCall(stdinFunc, {builder.getInt32(0)}, "stdin_val");
            
            llvm::Function* strlenFunc = module->getFunction("strlen");
            if (!strlenFunc) {
                llvm::FunctionType* strlenType = llvm::FunctionType::get(
                    builder.getInt64Ty(),
                    {builder.getInt8Ty()->getPointerTo()},
                    false);
                strlenFunc = llvm::Function::Create(strlenType, llvm::Function::ExternalLinkage, 0, "strlen", module);
            }
            
            llvm::Function* currentFunc = builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* readLoopBB = llvm::BasicBlock::Create(context, "read_loop", currentFunc);
            llvm::BasicBlock* checkBB = llvm::BasicBlock::Create(context, "check_input", currentFunc);
            llvm::BasicBlock* doneInputBB = llvm::BasicBlock::Create(context, "done_input", currentFunc);
            llvm::BasicBlock* eofBB = llvm::BasicBlock::Create(context, "eof_handler", currentFunc);
            
            builder.CreateBr(readLoopBB);
            
            builder.SetInsertPoint(readLoopBB);
            llvm::Value* fgetsResult = builder.CreateCall(fgetsFunc, {buffer, builder.getInt32(255), stdinValue}, "fgets_call");
            llvm::Value* fgetsOk = builder.CreateICmpNE(fgetsResult, llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0)), "fgets_ok");
            builder.CreateCondBr(fgetsOk, checkBB, eofBB);
            
            builder.SetInsertPoint(checkBB);
            llvm::Value* len = builder.CreateCall(strlenFunc, {buffer}, "input_len");
            llvm::Value* lenIsZero = builder.CreateICmpEQ(len, builder.getInt64(0), "len_is_zero");
            llvm::Value* firstChar = builder.CreateLoad(builder.getInt8Ty(), buffer, "first_char");
            llvm::Value* isNewline = builder.CreateICmpEQ(firstChar, builder.getInt8('\n'), "is_newline_char");
            llvm::Value* isEmpty = builder.CreateOr(lenIsZero, isNewline, "is_empty");
            builder.CreateCondBr(isEmpty, readLoopBB, doneInputBB);
            
            builder.SetInsertPoint(eofBB);
            builder.CreateStore(builder.getInt8('\0'), buffer);
            builder.CreateBr(doneInputBB);
            
            builder.SetInsertPoint(doneInputBB);
            
            llvm::Value* len2 = builder.CreateCall(strlenFunc, {buffer}, "input_len2");
            llvm::Value* len2NotZero = builder.CreateICmpNE(len2, builder.getInt64(0), "len2_not_zero");
            
            llvm::BasicBlock* trimNewlineBB2 = llvm::BasicBlock::Create(context, "trim_newline2", currentFunc);
            llvm::BasicBlock* checkCRBB = llvm::BasicBlock::Create(context, "check_cr", currentFunc);
            llvm::BasicBlock* trimCRBB = llvm::BasicBlock::Create(context, "trim_cr", currentFunc);
            llvm::BasicBlock* doneTrimBB = llvm::BasicBlock::Create(context, "done_trim", currentFunc);
            
            builder.CreateCondBr(len2NotZero, trimNewlineBB2, doneTrimBB);
            
            builder.SetInsertPoint(trimNewlineBB2);
            llvm::Value* lastIdx2 = builder.CreateSub(len2, builder.getInt64(1), "last_idx2");
            llvm::Value* lastCharPtr2 = builder.CreateGEP(builder.getInt8Ty(), buffer, lastIdx2, "last_char_ptr2");
            llvm::Value* lastChar2 = builder.CreateLoad(builder.getInt8Ty(), lastCharPtr2, "last_char2");
            llvm::Value* isNewline2 = builder.CreateICmpEQ(lastChar2, builder.getInt8('\n'), "is_newline2");
            builder.CreateCondBr(isNewline2, checkCRBB, doneTrimBB);
            
            builder.SetInsertPoint(checkCRBB);
            builder.CreateStore(builder.getInt8('\0'), lastCharPtr2);
            llvm::Value* len3 = builder.CreateCall(strlenFunc, {buffer}, "input_len3");
            llvm::Value* len3NotZero = builder.CreateICmpNE(len3, builder.getInt64(0), "len3_not_zero");
            builder.CreateCondBr(len3NotZero, trimCRBB, doneTrimBB);
            
            builder.SetInsertPoint(trimCRBB);
            llvm::Value* lastIdx3 = builder.CreateSub(len3, builder.getInt64(1), "last_idx3");
            llvm::Value* lastCharPtr3 = builder.CreateGEP(builder.getInt8Ty(), buffer, lastIdx3, "last_char_ptr3");
            llvm::Value* lastChar3 = builder.CreateLoad(builder.getInt8Ty(), lastCharPtr3, "last_char3");
            llvm::Value* isCR = builder.CreateICmpEQ(lastChar3, builder.getInt8('\r'), "is_cr");
            llvm::BasicBlock* doTrimCRBB = llvm::BasicBlock::Create(context, "do_trim_cr", currentFunc);
            builder.CreateCondBr(isCR, doTrimCRBB, doneTrimBB);
            
            builder.SetInsertPoint(doTrimCRBB);
            builder.CreateStore(builder.getInt8('\0'), lastCharPtr3);
            builder.CreateBr(doneTrimBB);
            
            builder.SetInsertPoint(doneTrimBB);
            
            if (stmt->declaredType == VarType::INT) {
                llvm::Function* strtolFunc = module->getFunction("strtol");
                if (!strtolFunc) {
                    llvm::FunctionType* strtolType = llvm::FunctionType::get(
                        builder.getInt64Ty(),
                        {builder.getInt8Ty()->getPointerTo(), builder.getInt8Ty()->getPointerTo()->getPointerTo(), builder.getInt32Ty()},
                        false);
                    strtolFunc = llvm::Function::Create(strtolType, llvm::Function::ExternalLinkage, 0, "strtol", module);
                }
                llvm::Value* nullPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0));
                llvm::Value* longVal = builder.CreateCall(strtolFunc, {buffer, nullPtr, builder.getInt32(10)}, "strtol_call");
                value = builder.CreateTrunc(longVal, builder.getInt32Ty(), "int_trunc");
            }
            else if (stmt->declaredType == VarType::LONG) {
                llvm::Function* strtollFunc = module->getFunction("strtoll");
                if (!strtollFunc) {
                    llvm::FunctionType* strtollType = llvm::FunctionType::get(
                        builder.getInt64Ty(),
                        {builder.getInt8Ty()->getPointerTo(), builder.getInt8Ty()->getPointerTo()->getPointerTo(), builder.getInt32Ty()},
                        false);
                    strtollFunc = llvm::Function::Create(strtollType, llvm::Function::ExternalLinkage, 0, "strtoll", module);
                }
                llvm::Value* nullPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0));
                value = builder.CreateCall(strtollFunc, {buffer, nullPtr, builder.getInt32(10)}, "strtoll_call");
            }
            else if (stmt->declaredType == VarType::FLOAT) {
                llvm::Function* strtodFunc = module->getFunction("strtod");
                if (!strtodFunc) {
                    llvm::FunctionType* strtodType = llvm::FunctionType::get(
                        builder.getDoubleTy(),
                        {builder.getInt8Ty()->getPointerTo(), builder.getInt8Ty()->getPointerTo()->getPointerTo()},
                        false);
                    strtodFunc = llvm::Function::Create(strtodType, llvm::Function::ExternalLinkage, 0, "strtod", module);
                }
                llvm::Value* nullPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0));
                llvm::CallInst* call = builder.CreateCall(strtodFunc, {buffer, nullPtr}, "strtod_call");
                call->setTailCallKind(llvm::CallInst::TCK_None);
                llvm::Value* doubleVal = call;
                value = builder.CreateFPTrunc(doubleVal, builder.getFloatTy(), "double_to_float");
            }
            else {
                llvm::Function* strlenFunc = module->getFunction("strlen");
                if (!strlenFunc) {
                    llvm::FunctionType* strlenType = llvm::FunctionType::get(
                        builder.getInt64Ty(),
                        {builder.getInt8Ty()->getPointerTo()},
                        false);
                    strlenFunc = llvm::Function::Create(strlenType, llvm::Function::ExternalLinkage, 0, "strlen", module);
                }
                
                llvm::Value* len = builder.CreateCall(strlenFunc, {buffer}, "input_len");
                llvm::Value* lenNotZero = builder.CreateICmpNE(len, builder.getInt64(0), "len_not_zero");
                
                llvm::Function* currentFunc = builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* checkNewlineBB = llvm::BasicBlock::Create(context, "check_newline", currentFunc);
                llvm::BasicBlock* trimBB = llvm::BasicBlock::Create(context, "trim_newline", currentFunc);
                llvm::BasicBlock* noTrimBB = llvm::BasicBlock::Create(context, "no_trim_newline", currentFunc);
                llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(context, "input_done", currentFunc);
                
                builder.CreateCondBr(lenNotZero, checkNewlineBB, noTrimBB);
                
                builder.SetInsertPoint(checkNewlineBB);
                llvm::Value* lastIdx = builder.CreateSub(len, builder.getInt64(1), "last_idx");
                llvm::Value* lastCharPtr = builder.CreateGEP(builder.getInt8Ty(), buffer, lastIdx, "last_char_ptr");
                llvm::Value* lastChar = builder.CreateLoad(builder.getInt8Ty(), lastCharPtr, "last_char");
                llvm::Value* isNewline = builder.CreateICmpEQ(lastChar, builder.getInt8('\n'), "is_newline");
                builder.CreateCondBr(isNewline, trimBB, noTrimBB);
                
                builder.SetInsertPoint(trimBB);
                builder.CreateStore(builder.getInt8('\0'), lastCharPtr);
                builder.CreateBr(doneBB);
                
                builder.SetInsertPoint(noTrimBB);
                builder.CreateBr(doneBB);
                
                builder.SetInsertPoint(doneBB);
                value = buffer;
            }
        }
    }
    
    if (!value) {
        value = codegen(stmt->value.get());
    }
    if (!value) return nullptr;
    
    llvm::AllocaInst* alloca = nullptr;
    auto it = locals.find(stmt->name);
    
    if (stmt->isReassignment) {
        if (it == locals.end()) {
            addError("Variable '" + stmt->name + "' used before declaration");
            return nullptr;
        }
        alloca = it->second;
    } else {
        if (it == locals.end()) {
            llvm::Function* func = builder.GetInsertBlock()->getParent();
            llvm::IRBuilderBase::InsertPoint ip = builder.saveIP();
            
            llvm::BasicBlock* entryBB = &func->getEntryBlock();
            if (entryBB->empty()) {
                builder.SetInsertPoint(entryBB);
            } else {
                builder.SetInsertPoint(entryBB, entryBB->getFirstInsertionPt());
            }
            
            llvm::Type* valueType = value->getType();
            VarType actualType = stmt->declaredType;
            
            if (stmt->hasExplicitType) {
                if (valueType->isPointerTy() || dynamic_cast<ArrayRangeExpression*>(stmt->value.get())) {
                    if (stmt->declaredType == VarType::INT) {
                        actualType = VarType::ARRAY_INT;
                    } else if (stmt->declaredType == VarType::LONG) {
                        actualType = VarType::ARRAY_LONG;
                    } else if (stmt->declaredType == VarType::FLOAT) {
                        actualType = VarType::ARRAY_FLOAT;
                    } else if (stmt->declaredType == VarType::BOOL) {
                        actualType = VarType::ARRAY_BOOL;
                    } else if (stmt->declaredType == VarType::STRING) {
                        actualType = VarType::ARRAY_STRING;
                    }
                }
                valueType = getLLVMType(actualType);
                localTypes[stmt->name] = actualType;
            } else {
                if (valueType->isIntegerTy(1)) {
                    valueType = llvm::Type::getInt32Ty(context);
                    localTypes[stmt->name] = VarType::INT;
                } else if (valueType->isFloatTy()) {
                    localTypes[stmt->name] = VarType::FLOAT;
                } else if (valueType->isPointerTy() || dynamic_cast<ArrayRangeExpression*>(stmt->value.get())) {
                    valueType = llvm::PointerType::get(context, 0);
                    if (auto* arrLit = dynamic_cast<ArrayLiteral*>(stmt->value.get())) {
                        if (!arrLit->elements.empty()) {
                            if (dynamic_cast<StringLiteral*>(arrLit->elements[0].get())) {
                                localTypes[stmt->name] = VarType::ARRAY_STRING;
                            } else if (auto* numLit = dynamic_cast<NumberLiteral*>(arrLit->elements[0].get())) {
                                constexpr int64_t INT32_MAX_VAL = 2147483647LL;
                                if (numLit->value > INT32_MAX_VAL || numLit->value < -INT32_MAX_VAL - 1) {
                                    localTypes[stmt->name] = VarType::ARRAY_LONG;
                                } else {
                                    localTypes[stmt->name] = VarType::ARRAY_INT;
                                }
                            } else {
                                localTypes[stmt->name] = VarType::ARRAY_INT;
                            }
                        } else {
                            localTypes[stmt->name] = VarType::ARRAY_INT;
                        }
                    } else {
                        localTypes[stmt->name] = VarType::ARRAY_INT;
                    }
                } else {
                    localTypes[stmt->name] = VarType::INT;
                }
            }
            
            alloca = builder.CreateAlloca(valueType, nullptr, stmt->name.c_str());
            builder.restoreIP(ip);
            
            locals[stmt->name] = alloca;
        } else {
            alloca = it->second;
        }
    }
    
    if (arrayLen > 0) {
        arrayLengths[stmt->name] = arrayLen;
    }
    
    llvm::Value* storeVal = value;
    llvm::Type* allocaType = alloca->getAllocatedType();
    llvm::Type* storeType = storeVal->getType();
    
    // Handle type conversions for store operations
    if (storeType->isIntegerTy(1) && allocaType->isIntegerTy(32)) {
        storeVal = builder.CreateZExt(storeVal, allocaType, "boolstore");
    } else if (storeType->isIntegerTy(32) && allocaType->isIntegerTy(1)) {
        storeVal = builder.CreateICmpNE(storeVal, createConstInt(context, builder.getInt32Ty(), 0), "inttobool");
    } else if (storeType->isIntegerTy(64) && allocaType->isIntegerTy(32)) {
        // Truncate int64 to int32
        storeVal = builder.CreateTrunc(storeVal, allocaType, "longtoint");
    } else if (storeType->isIntegerTy(32) && allocaType->isIntegerTy(64)) {
        // Extend int32 to int64
        storeVal = builder.CreateSExt(storeVal, allocaType, "inttolong");
    } else if (storeType->isIntegerTy(64) && allocaType->isIntegerTy(1)) {
        // Convert int64 to bool
        storeVal = builder.CreateICmpNE(storeVal, createConstInt(context, builder.getInt64Ty(), 0), "longtobool");
    } else if (storeType->isPointerTy() && allocaType->isIntegerTy(64)) {
        // Convert pointer to int64
        storeVal = builder.CreatePtrToInt(storeVal, allocaType, "ptrtolong");
    } else if (storeType->isIntegerTy(64) && allocaType->isPointerTy()) {
        // Convert int64 to pointer
        storeVal = builder.CreateIntToPtr(storeVal, allocaType, "longtoptr");
    }
    
    builder.CreateStore(storeVal, alloca);
    return value;
}

llvm::Value* LLVMCodegen::codegen(PrintStatement* stmt) {
    // EXP `un`: a disabled print emits nothing unless overridden by `!`.
    if (unPrintDisabled && !stmt->overridden) {
        return nullptr;
    }
    // EXP `paradox`: a direct print of a paradox expression outputs "PARADOX".
    if (dynamic_cast<ParadoxExpression*>(stmt->expr.get())) {
        llvm::Function* printfFunc = module->getFunction("printf");
        if (printfFunc) {
            llvm::Value* formatPtr = builder.CreateGlobalStringPtr("PARADOX\n", "paradox_fmt");
            builder.CreateCall(printfFunc->getFunctionType(), printfFunc, {formatPtr}, "paradox_printf");
        }
        return nullptr;
    }
    // EXP `paradox` stage 2: a direct print of a GHOST variable keeps its value
    // and appends "#" (the ghost marker). Only the trailing marker changes --
    // the stored value itself is printed unchanged and is never 0.
    bool ghostPrint = dynamic_cast<GhostExpression*>(stmt->expr.get()) != nullptr;
    auto makePrintFormat = [&](const std::string& base) -> llvm::Value* {
        if (!ghostPrint) {
            return builder.CreateGlobalStringPtr(base, "format");
        }
        std::string s = base;
        size_t nl = s.find('\n');
        if (nl != std::string::npos) s.insert(nl, "#");
        else s += '#';
        return builder.CreateGlobalStringPtr(s, "format_ghost");
    };
    VarType exprType = VarType::UNKNOWN;
    if (auto* strLit = dynamic_cast<StringLiteral*>(stmt->expr.get())) {
        exprType = VarType::STRING;
    } else if (auto* numLit = dynamic_cast<NumberLiteral*>(stmt->expr.get())) {
        constexpr int64_t INT32_MAX_VAL = 2147483647LL;
        if (numLit->value > INT32_MAX_VAL || numLit->value < -INT32_MAX_VAL - 1) {
            exprType = VarType::LONG;
        } else {
            exprType = VarType::INT;
        }
    } else if (auto* floatLit = dynamic_cast<FloatLiteral*>(stmt->expr.get())) {
        exprType = VarType::FLOAT;
    } else if (auto* boolLit = dynamic_cast<BooleanLiteral*>(stmt->expr.get())) {
        exprType = VarType::BOOL;
    } else if (auto* varExpr = dynamic_cast<VariableExpression*>(stmt->expr.get())) {
        auto typeIt = localTypes.find(varExpr->name);
        if (typeIt != localTypes.end()) {
            exprType = typeIt->second;
        } else {
            auto globalTypeIt = windowInputTypes.find(varExpr->name);
            if (globalTypeIt != windowInputTypes.end()) {
                exprType = globalTypeIt->second;
            }
        }
    } else if (auto* ghostExpr = dynamic_cast<GhostExpression*>(stmt->expr.get())) {
        auto typeIt = localTypes.find(ghostExpr->name);
        if (typeIt != localTypes.end()) {
            exprType = typeIt->second;
        } else {
            auto globalTypeIt = windowInputTypes.find(ghostExpr->name);
            if (globalTypeIt != windowInputTypes.end()) {
                exprType = globalTypeIt->second;
            }
        }
    } else if (auto* callExpr = dynamic_cast<CallExpression*>(stmt->expr.get())) {
        std::string funcName = callExpr->ns.empty() ? callExpr->name : (callExpr->ns + ":" + callExpr->name);
        auto retTypeIt = funcReturnTypes.find(funcName);
        if (retTypeIt != funcReturnTypes.end()) {
            exprType = retTypeIt->second;
        } else if (callExpr->name == "input") {
            exprType = VarType::STRING;
        }
    } else if (auto* arrIdx = dynamic_cast<ArrayIndexExpression*>(stmt->expr.get())) {
        if (auto* varExpr = dynamic_cast<VariableExpression*>(arrIdx->array.get())) {
            auto typeIt = localTypes.find(varExpr->name);
            if (typeIt != localTypes.end()) {
                VarType arrType = typeIt->second;
                if (arrType == VarType::ARRAY_STRING) {
                    exprType = VarType::STRING;
                } else if (arrType == VarType::ARRAY_INT) {
                    exprType = VarType::INT;
                } else if (arrType == VarType::ARRAY_LONG) {
                    exprType = VarType::LONG;
                } else if (arrType == VarType::ARRAY_FLOAT) {
                    exprType = VarType::FLOAT;
                } else if (arrType == VarType::ARRAY_BOOL) {
                    exprType = VarType::BOOL;
                }
            }
        }
    } else if (auto* binOp = dynamic_cast<BinaryOp*>(stmt->expr.get())) {
        if (binOp->op == BinaryOpType::ADD) {
            exprType = VarType::STRING;
        }
    }
    
    llvm::Value* arg = codegen(stmt->expr.get());
    if (!arg) return nullptr;

    if (hasWindowStatements) {
        llvm::Function* messageBoxWFunc = module->getFunction("MessageBoxW");
        llvm::Function* multiByteToWideCharFunc = module->getFunction("MultiByteToWideChar");
        llvm::Function* snprintfFunc = module->getFunction("snprintf");
        llvm::Function* appendBoxFunc = module->getFunction("xr_append_box");
        llvm::Value* titlePtr = builder.CreateGlobalStringPtr("xfawa print", "print_msgbox_title");
        llvm::Value* nullPtr = llvm::Constant::getNullValue(builder.getInt8Ty()->getPointerTo());
        llvm::Value* textPtr = nullptr;

        if (arg->getType()->isPointerTy() && exprType == VarType::STRING) {
            if (ghostPrint) {
                llvm::AllocaInst* buffer = builder.CreateAlloca(builder.getInt8Ty(), builder.getInt32(256), "ghost_str_buffer");
                llvm::Value* bufferPtr = builder.CreateBitCast(buffer, builder.getInt8Ty()->getPointerTo(), "ghost_str_buffer_ptr");
                builder.CreateCall(snprintfFunc, {bufferPtr, builder.getInt64(256),
                    builder.CreateGlobalStringPtr("%s#", "format_ghost_str"), arg}, "snprintf_ghost_str");
                textPtr = bufferPtr;
            } else {
                textPtr = builder.CreateBitCast(arg, builder.getInt8Ty()->getPointerTo(), "window_print_text_ptr");
            }
        } else {
            llvm::AllocaInst* buffer = builder.CreateAlloca(builder.getInt8Ty(), builder.getInt32(256), "print_buffer");
            llvm::Value* bufferPtr = builder.CreateBitCast(buffer, builder.getInt8Ty()->getPointerTo(), "print_buffer_ptr");
            llvm::Value* formatPtr;
            llvm::Value* printArg = arg;

            if (arg->getType()->isFloatTy()) {
                formatPtr = makePrintFormat("%f");
                printArg = builder.CreateFPExt(arg, builder.getDoubleTy(), "print_float_ext");
            } else if (arg->getType()->isIntegerTy(1)) {
                formatPtr = makePrintFormat("%d");
                printArg = builder.CreateZExtOrTrunc(arg, builder.getInt32Ty(), "print_bool_ext");
            } else if (arg->getType()->isIntegerTy(64)) {
                formatPtr = makePrintFormat("%lld");
            } else if (arg->getType()->isPointerTy()) {
                if (exprType == VarType::LONG || exprType == VarType::UNKNOWN) {
                    formatPtr = makePrintFormat("%lld");
                    printArg = builder.CreatePtrToInt(arg, builder.getInt64Ty(), "ptrtolong");
                } else if (exprType == VarType::INT) {
                    formatPtr = makePrintFormat("%d");
                    printArg = builder.CreatePtrToInt(arg, builder.getInt32Ty(), "ptrtoint");
                } else if (exprType == VarType::BOOL) {
                    formatPtr = makePrintFormat("%d");
                    printArg = builder.CreatePtrToInt(arg, builder.getInt32Ty(), "ptrtobool");
                } else if (exprType == VarType::FLOAT) {
                    formatPtr = makePrintFormat("%f");
                    printArg = builder.CreatePtrToInt(arg, builder.getInt64Ty(), "ptrtofloat");
                    printArg = builder.CreateSIToFP(printArg, builder.getDoubleTy(), "inttofp");
                } else {
                    formatPtr = makePrintFormat("%s");
                    printArg = arg;
                }
            } else {
                formatPtr = makePrintFormat("%d");
                if (!arg->getType()->isIntegerTy(32)) {
                    printArg = builder.CreateSExtOrTrunc(arg, builder.getInt32Ty(), "print_int_cast");
                }
            }

            builder.CreateCall(snprintfFunc, {bufferPtr, builder.getInt64(256), formatPtr, printArg}, "snprintfcall");
            textPtr = bufferPtr;
        }

        if (!stmt->outputTarget.empty() && activeWindowId >= 0 && appendBoxFunc) {
            llvm::Value* boxIdPtr = builder.CreateGlobalStringPtr(
                stmt->outputTarget,
                "xr_window_box_id_" + std::to_string(activeWindowId) + "_" + std::to_string(stmt->location.line) + "_" + std::to_string(stmt->location.column));
            llvm::Value* appendResult = builder.CreateCall(appendBoxFunc, {boxIdPtr, textPtr}, "append_box_result");
            llvm::Value* appended = builder.CreateICmpNE(appendResult, builder.getInt32(0), "append_box_ok");

            llvm::BasicBlock* appendOkBB = llvm::BasicBlock::Create(context, "append_box_ok", builder.GetInsertBlock()->getParent());
            llvm::BasicBlock* appendFallbackBB = llvm::BasicBlock::Create(context, "append_box_fallback", builder.GetInsertBlock()->getParent());
            llvm::BasicBlock* appendContinueBB = llvm::BasicBlock::Create(context, "append_box_continue", builder.GetInsertBlock()->getParent());

            builder.CreateCondBr(appended, appendOkBB, appendFallbackBB);

            builder.SetInsertPoint(appendOkBB);
            builder.CreateBr(appendContinueBB);

            builder.SetInsertPoint(appendFallbackBB);
            
            // Convert UTF-8 to wide char for MessageBoxW
            if (messageBoxWFunc && multiByteToWideCharFunc) {
                llvm::AllocaInst* wbuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wprint_buffer");
                llvm::Value* wbufferPtr = builder.CreateBitCast(wbuffer, builder.getInt16Ty()->getPointerTo(), "wprint_buffer_ptr");
                llvm::Value* wtitleBuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wtitle_buffer");
                llvm::Value* wtitlePtr = builder.CreateBitCast(wtitleBuffer, builder.getInt16Ty()->getPointerTo(), "wtitle_buffer_ptr");
                
                // Convert text to wide char: MultiByteToWideChar(CP_UTF8, 0, textPtr, -1, wbuffer, 256)
                builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), textPtr, builder.getInt32(-1), wbufferPtr, builder.getInt32(256)}, "text_to_wide");
                // Convert title to wide char
                builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), titlePtr, builder.getInt32(-1), wtitlePtr, builder.getInt32(256)}, "title_to_wide");
                // Call MessageBoxW
                builder.CreateCall(messageBoxWFunc, {nullPtr, wbufferPtr, wtitlePtr, builder.getInt32(0)}, "msgboxwcall");
            } else {
                // Fallback to MessageBoxA if wide char functions not available
                llvm::Function* messageBoxAFunc = module->getFunction("MessageBoxA");
                if (messageBoxAFunc) {
                    builder.CreateCall(messageBoxAFunc, {nullPtr, textPtr, titlePtr, builder.getInt32(0)}, "msgboxacall");
                }
            }
            builder.CreateBr(appendContinueBB);

            builder.SetInsertPoint(appendContinueBB);
            return builder.getInt32(1);
        }

        // Convert UTF-8 to wide char for MessageBoxW
        if (messageBoxWFunc && multiByteToWideCharFunc) {
            llvm::AllocaInst* wbuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wprint_buffer");
            llvm::Value* wbufferPtr = builder.CreateBitCast(wbuffer, builder.getInt16Ty()->getPointerTo(), "wprint_buffer_ptr");
            llvm::AllocaInst* wtitleBuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wtitle_buffer");
            llvm::Value* wtitlePtr = builder.CreateBitCast(wtitleBuffer, builder.getInt16Ty()->getPointerTo(), "wtitle_buffer_ptr");
            
            // Convert text to wide char: MultiByteToWideChar(CP_UTF8, 0, textPtr, -1, wbuffer, 256)
            builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), textPtr, builder.getInt32(-1), wbufferPtr, builder.getInt32(256)}, "text_to_wide");
            // Convert title to wide char
            builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), titlePtr, builder.getInt32(-1), wtitlePtr, builder.getInt32(256)}, "title_to_wide");
            // Call MessageBoxW
            return builder.CreateCall(messageBoxWFunc, {nullPtr, wbufferPtr, wtitlePtr, builder.getInt32(0)}, "msgboxwcall");
        } else {
            // Fallback to MessageBoxA if wide char functions not available
            llvm::Function* messageBoxAFunc = module->getFunction("MessageBoxA");
            if (messageBoxAFunc) {
                return builder.CreateCall(messageBoxAFunc, {nullPtr, textPtr, titlePtr, builder.getInt32(0)}, "msgboxacall");
            }
        }
        return builder.getInt32(0);
    }

    llvm::Function* printfFunc = module->getFunction("printf");
    if (printfFunc) {
        llvm::Value* formatPtr;
        llvm::Value* printArg = arg;
        
        if (arg->getType()->isFloatTy()) {
            formatPtr = makePrintFormat("%f\n");
            printArg = builder.CreateFPExt(arg, builder.getDoubleTy(), "float.ext");
        } else if (arg->getType()->isIntegerTy(1)) {
            formatPtr = makePrintFormat("%d\n");
            printArg = builder.CreateZExtOrTrunc(arg, builder.getInt32Ty(), "bool.ext");
        } else if (arg->getType()->isIntegerTy(64)) {
            formatPtr = makePrintFormat("%lld\n");
        } else if (arg->getType()->isIntegerTy()) {
            formatPtr = makePrintFormat("%d\n");
        } else if (arg->getType()->isPointerTy()) {
            if (exprType == VarType::LONG || exprType == VarType::UNKNOWN) {
                formatPtr = makePrintFormat("%lld\n");
                printArg = builder.CreatePtrToInt(arg, builder.getInt64Ty(), "ptrtolong");
            } else if (exprType == VarType::INT) {
                formatPtr = makePrintFormat("%d\n");
                printArg = builder.CreatePtrToInt(arg, builder.getInt32Ty(), "ptrtoint");
            } else if (exprType == VarType::BOOL) {
                formatPtr = makePrintFormat("%d\n");
                printArg = builder.CreatePtrToInt(arg, builder.getInt32Ty(), "ptrtobool");
            } else if (exprType == VarType::FLOAT) {
                formatPtr = makePrintFormat("%f\n");
                printArg = builder.CreatePtrToInt(arg, builder.getInt64Ty(), "ptrtofloat");
                printArg = builder.CreateSIToFP(printArg, builder.getDoubleTy(), "inttofp");
            } else {
                formatPtr = makePrintFormat("%s\n");
            }
        } else {
            formatPtr = makePrintFormat("%d\n");
        }
        
        builder.CreateCall(printfFunc->getFunctionType(), printfFunc, {formatPtr, printArg}, "printfcall");
        
        llvm::Function* fflushFunc = module->getFunction("fflush");
        if (!fflushFunc) {
            llvm::FunctionType* fflushType = llvm::FunctionType::get(
                builder.getInt32Ty(),
                {builder.getInt8Ty()->getPointerTo()},
                false);
            fflushFunc = llvm::Function::Create(fflushType, llvm::Function::ExternalLinkage, 0, "fflush", module);
        }
        llvm::Value* stdoutPtr = builder.CreateCall(module->getFunction("__acrt_iob_func"), {builder.getInt32(1)}, "stdout");
        builder.CreateCall(fflushFunc, {stdoutPtr}, "fflush_stdout");
        
        return nullptr;
    }
    
    return arg;
}

llvm::Value* LLVMCodegen::codegen(ReturnStatement* stmt) {
    llvm::Value* value = codegen(stmt->value.get());
    if (!value) return nullptr;
    
    llvm::Type* retType = llvm::Type::getInt64Ty(context);
    
    if (value->getType()->isIntegerTy(1)) {
        value = builder.CreateZExt(value, retType, "retbool");
    } else if (value->getType()->isIntegerTy(32)) {
        value = builder.CreateSExt(value, retType, "retint");
    } else if (value->getType()->isIntegerTy(64)) {
        // Already int64, no conversion needed
    } else if (value->getType()->isFloatTy()) {
        value = builder.CreateFPExt(value, builder.getDoubleTy(), "float.ext");
        value = builder.CreateBitCast(value, retType, "float.bits");
    } else if (value->getType()->isDoubleTy()) {
        value = builder.CreateBitCast(value, retType, "double.bits");
    } else if (value->getType()->isPointerTy()) {
        // Convert pointer to int64 for return
        value = builder.CreatePtrToInt(value, retType, "ptr_to_int64");
    }
    
    builder.CreateRet(value);
    return value;
}

llvm::Value* LLVMCodegen::codegen(BreakStatement* stmt) {
    if (loopEndBB) {
        builder.CreateBr(loopEndBB);
    }
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(BoomStatement* stmt) {
    if (unBoomDisabled) return nullptr;

    // Explosion sound: a falling tone via the system beeper (synchronous, so it
    // finishes before we exit). On systems without a beeper this silently does
    // nothing; the terminal BEL below is the cross-platform fallback.
    llvm::Function* beepFunc = module->getFunction("Beep");
    if (!beepFunc) {
        llvm::FunctionType* beepType = llvm::FunctionType::get(
            builder.getInt32Ty(), {builder.getInt32Ty(), builder.getInt32Ty()}, false);
        beepFunc = llvm::Function::Create(beepType, llvm::Function::ExternalLinkage, "Beep", module);
    }
    builder.CreateCall(beepFunc, {builder.getInt32(440), builder.getInt32(120)});
    builder.CreateCall(beepFunc, {builder.getInt32(220), builder.getInt32(160)});
    builder.CreateCall(beepFunc, {builder.getInt32(110), builder.getInt32(240)});

    llvm::Function* printfFunc = module->getFunction("printf");
    if (printfFunc) {
        // "\a" is the terminal bell (BEL) — audible/flashing feedback without any assets.
        llvm::Value* formatPtr = builder.CreateGlobalStringPtr("\aBOOM!!\n", "boom_fmt");
        builder.CreateCall(printfFunc->getFunctionType(), printfFunc, {formatPtr}, "boom_printf");
        llvm::Function* fflushFunc = module->getFunction("fflush");
        llvm::Value* stdoutPtr = builder.CreateCall(module->getFunction("__acrt_iob_func"), {builder.getInt32(1)}, "stdout");
        builder.CreateCall(fflushFunc, {stdoutPtr}, "fflush_stdout");
    }
    // Normal, orderly exit (the program "explodes" but does not actually crash).
    builder.CreateRet(createConstInt(context, llvm::Type::getInt64Ty(context), 0));
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(BsodStatement* stmt) {
    if (unBsodDisabled) return nullptr;
    hasBsod = true;

    // Full-screen fake blue screen via a self-contained runtime helper.
    // (The helper falls back to a system beep if a window cannot be created.)
    llvm::Function* overlayFunc = module->getFunction("xfawa_bsod_overlay");
    if (!overlayFunc) {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
        overlayFunc = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "xfawa_bsod_overlay", module);
    }
    builder.CreateCall(overlayFunc, {});

    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(BelieveStatement* stmt) {
    // Parse "int <op> int = int" from the raw proposition text.
    std::istringstream iss(stmt->raw);
    int64_t a = 0, b = 0, r = 0;
    char op = 0, eq = 0;
    if (iss >> a >> op >> b >> eq >> r && eq == '=') {
        char opCh = binaryOpChar(
            op == '+' ? xfawa::BinaryOpType::ADD :
            op == '-' ? xfawa::BinaryOpType::SUB :
            op == '*' ? xfawa::BinaryOpType::MUL :
            op == '/' ? xfawa::BinaryOpType::DIV : xfawa::BinaryOpType::NONE);
        if (opCh != 0) {
            std::string key = std::to_string(a) + opCh + std::to_string(b);
            beliefMap[key] = r;
        } else {
            addError("Unsupported operator in 'believe': \"" + stmt->raw + "\" (supported: + - * /)");
        }
    } else {
        addError("Invalid 'believe' proposition: \"" + stmt->raw + "\" (expected: int <op> int = int)");
    }
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(LieStatement* stmt) {
    // The lie rewrite already substituted reads inside `body` with constants;
    // we just emit the body normally.
    if (stmt->body) {
        codegen(stmt->body.get());
    }
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(UnStatement* stmt) {
    switch (stmt->target) {
case TokenType::KEYWORD_PRINT: unPrintDisabled = true; break;
            case TokenType::KEYWORD_BOOM:  unBoomDisabled  = true; break;
            case TokenType::KEYWORD_BSOD:  unBsodDisabled  = true; break;
            case TokenType::KEYWORD_SLEEP: unSleepDisabled = true; break;
        default: break;
    }
    return nullptr;
}

// EXP `ignore`: completely skip the inner statement; generates no code.
llvm::Value* LLVMCodegen::codegen(IgnoreStatement* stmt) {
    return nullptr;
}

// EXP `do`: force-execute the inner statement, regardless of any active `un`.
llvm::Value* LLVMCodegen::codegen(DoStatement* stmt) {
    // If this `do` was already hoisted to run unconditionally ahead of an
    // enclosing conditional/loop, do nothing at its original position.
    if (hoistedDo.count(stmt)) return nullptr;
    emitDoInner(stmt);
    return nullptr;
}

void LLVMCodegen::emitDoInner(DoStatement* stmt) {
    bool savedPrint = unPrintDisabled;
    bool savedBoom = unBoomDisabled;
    bool savedBsod = unBsodDisabled;
    bool savedSleep = unSleepDisabled;
    unPrintDisabled = unBoomDisabled = unBsodDisabled = unSleepDisabled = false;
    codegen(stmt->inner.get());
    unPrintDisabled = savedPrint;
    unBoomDisabled = savedBoom;
    unBsodDisabled = savedBsod;
    unSleepDisabled = savedSleep;
}

void LLVMCodegen::hoistDoFromBranch(Statement* stmt) {
    if (!stmt) return;
    if (auto* doSt = dynamic_cast<DoStatement*>(stmt)) {
        if (!hoistedDo.count(doSt)) {
            hoistedDo.insert(doSt);
            emitDoInner(doSt);
        }
        return;
    }
    if (auto* blk = dynamic_cast<BlockStatement*>(stmt)) {
        for (auto& s : blk->statements) hoistDoFromBranch(s.get());
        return;
    }
    if (auto* ifs = dynamic_cast<IfStatement*>(stmt)) {
        hoistDoFromBranch(ifs->thenBranch.get());
        for (auto& e : ifs->elseIfBranches) hoistDoFromBranch(e.second.get());
        if (ifs->elseBranch) hoistDoFromBranch(ifs->elseBranch.get());
        return;
    }
    if (auto* wh = dynamic_cast<WhileStatement*>(stmt)) {
        hoistDoFromBranch(wh->body.get());
        return;
    }
    if (auto* loop = dynamic_cast<LoopStatement*>(stmt)) {
        for (auto& s : loop->body) hoistDoFromBranch(s.get());
        return;
    }
}

// EXP `please`: print "thank you!" before executing the inner statement.
llvm::Value* LLVMCodegen::codegen(PleaseStatement* stmt) {
    llvm::Function* printfFunc = module->getFunction("printf");
    if (printfFunc) {
        llvm::Value* fmt = builder.CreateGlobalStringPtr("thank you!\n", "please_fmt");
        builder.CreateCall(printfFunc->getFunctionType(), printfFunc, {fmt}, "please_printf");
    }
    codegenOnce(stmt->inner.get());
    return nullptr;
}

// EXP bare `please`: a keyword with NO runtime meaning whatsoever (no
// "thank you!" print). Its only effects are compile-time (rage meter /
// red-hot compliance), handled entirely inside the semantic analyzer.
llvm::Value* LLVMCodegen::codegen(PleaseNoticeStatement* stmt) {
    (void)stmt;
    return nullptr;
}

// EXP `shutup`: warnings were already suppressed at parse time; no code is emitted.
llvm::Value* LLVMCodegen::codegen(ShutupStatement* stmt) {
    return nullptr;
}

// Reliable `rand()` handle (declared or created).
llvm::Function* LLVMCodegen::getRandFunction() {
    llvm::Function* f = module->getFunction("rand");
    if (!f) {
        f = llvm::Function::Create(
            llvm::FunctionType::get(builder.getInt32Ty(), {}, false),
            llvm::Function::ExternalLinkage, "rand", module);
    }
    return f;
}

// EXP `...`: randomly pick one of the safe functions collected earlier and
// REALLY call it, with arguments generated from its actual signature. The
// selection itself uses rand() (seeded once when the first `...` runs).
llvm::Value* LLVMCodegen::codegen(EllipsisStatement* stmt) {
    usesRandomBuiltin = true;
    return emitRandomCallDispatch();
}

// EXP `sleep`: pause for `expr` seconds. int/float both work; the duration is
// converted to milliseconds and passed to Win32 Sleep().
llvm::Value* LLVMCodegen::codegen(SleepStatement* stmt) {
    if (unSleepDisabled && !stmt->overridden) return nullptr;

    llvm::Function* sleepFunc = module->getFunction("Sleep");
    if (!sleepFunc) {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder.getVoidTy(), {builder.getInt32Ty()}, false);
        sleepFunc = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "Sleep", module);
    }

    llvm::Value* secs = codegen(stmt->expr.get());
    if (!secs) return nullptr;

    llvm::Type* i64Ty = llvm::Type::getInt64Ty(context);
    llvm::Value* ms;
    if (secs->getType()->isFloatingPointTy()) {
        llvm::Constant* k1000 = secs->getType()->isFloatTy()
            ? llvm::ConstantFP::get(builder.getFloatTy(), 1000.0f)
            : llvm::ConstantFP::get(secs->getType(), 1000.0);
        llvm::Value* scaled = builder.CreateFMul(secs, k1000, "");
        ms = builder.CreateFPToSI(scaled, i64Ty, "");
    } else {
        llvm::Value* secs64 = builder.CreateSExtOrTrunc(secs, i64Ty, "");
        ms = builder.CreateMul(secs64, builder.getInt64(1000), "");
    }

    // A negative duration would wrap around into a ~49-day Sleep as a DWORD:
    // clamp to (at least) zero so it behaves as a no-op instead.
    llvm::Value* zero = builder.getInt64(0);
    llvm::Value* isNeg = builder.CreateICmpSLT(ms, zero, "");
    ms = builder.CreateSelect(isNeg, zero, ms, "");

    llvm::Value* ms32 = builder.CreateTrunc(ms, builder.getInt32Ty(), "");
    builder.CreateCall(sleepFunc, {ms32});
    return nullptr;
}

// One-shot runtime seed for rand(). The seed mixes second-resolution time with
// the millisecond tick counter so reruns within the same second still differ.
void LLVMCodegen::emitRandomCallSeedOnce() {
    if (hasEllipsisRandSeeded) return;
    llvm::Function* timeFunc = module->getFunction("time");
    if (!timeFunc) {
        timeFunc = llvm::Function::Create(
            llvm::FunctionType::get(builder.getInt64Ty(), {builder.getPtrTy()}, false),
            llvm::Function::ExternalLinkage, "time", module);
    }
    llvm::Function* tickFunc = module->getFunction("GetTickCount");
    if (!tickFunc) {
        tickFunc = llvm::Function::Create(
            llvm::FunctionType::get(builder.getInt32Ty(), {}, false),
            llvm::Function::ExternalLinkage, "GetTickCount", module);
    }
    llvm::Function* srandFunc = module->getFunction("srand");
    if (!srandFunc) {
        srandFunc = llvm::Function::Create(
            llvm::FunctionType::get(builder.getVoidTy(), {builder.getInt32Ty()}, false),
            llvm::Function::ExternalLinkage, "srand", module);
    }
    llvm::Value* now   = builder.CreateCall(timeFunc, {llvm::Constant::getNullValue(builder.getPtrTy())});
    llvm::Value* ticks = builder.CreateCall(tickFunc, {});
    llvm::Value* seed  = builder.CreateXor(builder.CreateTrunc(now, builder.getInt32Ty()), ticks);
    builder.CreateCall(srandFunc, {seed});
    hasEllipsisRandSeeded = true;
}

// Fill a per-candidate [64 x i8] buffer with random printable ASCII and a '\0'
// terminator, so STRING parameters get a real, safe, in-bounds string.
void LLVMCodegen::emitRandomStringFill(llvm::GlobalVariable* buffer, int index) {
    llvm::Function* owner = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* preBB = builder.GetInsertBlock();
    llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(context, "dots.strloop", owner);
    llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(context, "dots.strexit", owner);

    builder.CreateBr(loopBB);

    builder.SetInsertPoint(loopBB);
    llvm::PHINode* idx = builder.CreatePHI(builder.getInt32Ty(), 2, "dots.stridx");
    idx->addIncoming(builder.getInt32(0), preBB);
    llvm::Value* rv = builder.CreateCall(getRandFunction(), {}, "dots.strr");
    llvm::Value* m = builder.CreateSRem(rv, builder.getInt32(94), "dots.strm");
    llvm::Value* ch = builder.CreateAdd(m, builder.getInt32(33), "dots.strc");
    llvm::Value* slot = builder.CreateInBoundsGEP(builder.getInt8Ty(), buffer, {builder.getInt32(0), idx}, "dots.strslot");
    builder.CreateStore(builder.CreateTrunc(ch, builder.getInt8Ty(), "dots.strb"), slot);
    llvm::Value* next = builder.CreateAdd(idx, builder.getInt32(1), "dots.strnext");
    llvm::Value* cont = builder.CreateICmpSLT(next, builder.getInt32(31), "dots.strcont");
    idx->addIncoming(next, loopBB);
    builder.CreateCondBr(cont, loopBB, exitBB);

    builder.SetInsertPoint(exitBB);
    llvm::Value* endSlot = builder.CreateInBoundsGEP(builder.getInt8Ty(), buffer, {builder.getInt32(0), builder.getInt32(31)}, "dots.str0");
    builder.CreateStore(builder.getInt8(0), endSlot);
}

// void() trampoline for one candidate:
//   entry: depth >= MAX? -> done (this is the recursion guard, so a function
//          choosing itself through `...` can never overflow the stack)
//   body:  depth++, build one argument per real parameter type, REAL call,
//          depth--, done.
llvm::Function* LLVMCodegen::createRandomCallTrampoline(const RandomCallCandidate& cand, int index) {
    std::string name = "__xfa_dots_" + std::to_string(index);
    llvm::FunctionType* ft = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
    llvm::Function* tramp = llvm::Function::Create(ft, llvm::Function::InternalLinkage, 0, name, module);

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "dots.entry", tramp);
    llvm::BasicBlock* bodyBB  = llvm::BasicBlock::Create(context, "dots.body", tramp);
    llvm::BasicBlock* doneBB  = llvm::BasicBlock::Create(context, "dots.done", tramp);

    builder.SetInsertPoint(entryBB);
    llvm::Value* d0 = builder.CreateLoad(builder.getInt32Ty(), randomCallDepth, "dots.d");
    llvm::Value* over = builder.CreateICmpSGE(d0, builder.getInt32(kRandomCallMaxDepth), "dots.over");
    builder.CreateCondBr(over, doneBB, bodyBB);

    builder.SetInsertPoint(bodyBB);
    builder.CreateStore(builder.CreateAdd(d0, builder.getInt32(1), "dots.d1"), randomCallDepth);

    llvm::ArrayRef<llvm::Type*> paramTys = cand.callee->getFunctionType()->params();
    std::vector<llvm::Value*> args;
    llvm::GlobalVariable* strBuf = nullptr;

    for (unsigned i = 0; i < paramTys.size(); ++i) {
        llvm::Type* ty = paramTys[i];
        llvm::Value* argVal = nullptr;
        if (cand.nullPtrArg) {
            argVal = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ty));
        } else if (ty == builder.getInt1Ty()) {
            llvm::Value* rv = builder.CreateCall(getRandFunction(), {}, "dots.r");
            argVal = builder.CreateICmpNE(builder.CreateSRem(rv, builder.getInt32(2), "dots.rb"), builder.getInt32(0), "dots.bool");
        } else if (ty == builder.getInt32Ty()) {
            llvm::Value* rv = builder.CreateCall(getRandFunction(), {}, "dots.r");
            argVal = builder.CreateSub(builder.CreateSRem(rv, builder.getInt32(2001), "dots.im"), builder.getInt32(1000), "dots.i");
        } else if (ty == builder.getInt64Ty()) {
            llvm::Value* rv = builder.CreateCall(getRandFunction(), {}, "dots.r");
            llvm::Value* wide = builder.CreateSExt(rv, builder.getInt64Ty(), "dots.w");
            argVal = builder.CreateSub(builder.CreateSRem(wide, builder.getInt64(2000001), "dots.lm"), builder.getInt64(1000000), "dots.l");
        } else if (ty == builder.getFloatTy()) {
            llvm::Value* rv = builder.CreateCall(getRandFunction(), {}, "dots.r");
            llvm::Value* m = builder.CreateSRem(rv, builder.getInt32(1001), "dots.fm");
            llvm::Value* f = builder.CreateUIToFP(m, builder.getFloatTy(), "dots.fuf");
            argVal = builder.CreateFSub(builder.CreateFDiv(f, llvm::ConstantFP::get(builder.getFloatTy(), 100.0f), "dots.fdiv"), llvm::ConstantFP::get(builder.getFloatTy(), 5.0f), "dots.f");
        } else if (ty->isPointerTy()) {
            if (!strBuf) {
                llvm::ArrayType* at = llvm::ArrayType::get(builder.getInt8Ty(), 64);
                strBuf = new llvm::GlobalVariable(*module, at, false,
                    llvm::GlobalValue::InternalLinkage,
                    llvm::ConstantAggregateZero::get(at), "__xfa_dots_str_" + std::to_string(index));
            }
            emitRandomStringFill(strBuf, index);
            argVal = builder.CreateInBoundsGEP(builder.getInt8Ty(), strBuf, {builder.getInt32(0), builder.getInt32(0)}, "dots.str");
        } else {
            // Unsupported param type: candidates were filtered so this cannot
            // happen; if it ever does, bail out without calling.
            builder.CreateBr(doneBB);
            llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(context, "dots.dead", tramp);
            builder.SetInsertPoint(deadBB);
            builder.CreateRetVoid();
            return tramp;
        }
        args.push_back(argVal);
    }

    builder.CreateCall(cand.callee->getFunctionType(), cand.callee, args);
    llvm::Value* d2 = builder.CreateLoad(builder.getInt32Ty(), randomCallDepth, "dots.d2");
    builder.CreateStore(builder.CreateSub(d2, builder.getInt32(1), "dots.dec"), randomCallDepth);
    builder.CreateBr(doneBB);

    builder.SetInsertPoint(doneBB);
    builder.CreateRetVoid();
    return tramp;
}

// The `...` runtime dispatch: rand() % N -> slot in the trampoline table -> call.
// The table + trampolines are created lazily on the first `...` and shared by all.
llvm::Value* LLVMCodegen::emitRandomCallDispatch() {
    emitRandomCallSeedOnce();
    if (randomCallCandidates.empty()) return nullptr;

    if (!randomCallTableEmitted) {
        randomCallTableEmitted = true;
        randomCallDepth = new llvm::GlobalVariable(*module, builder.getInt32Ty(), false,
            llvm::GlobalValue::InternalLinkage, builder.getInt32(0), "__xfa_dots_depth");

        llvm::BasicBlock* savedBB = builder.GetInsertBlock();
        std::vector<llvm::Constant*> ptrs;
        for (unsigned i = 0; i < randomCallCandidates.size(); ++i) {
            ptrs.push_back(createRandomCallTrampoline(randomCallCandidates[i], static_cast<int>(i)));
        }
        llvm::ArrayType* at = llvm::ArrayType::get(builder.getPtrTy(), ptrs.size());
        randomCallTable = new llvm::GlobalVariable(*module, at, false,
            llvm::GlobalValue::InternalLinkage,
            llvm::ConstantArray::get(at, ptrs), "__xfa_dots_table");
        builder.SetInsertPoint(savedBB);
    }

    if (!randomCallTable) return nullptr;

    llvm::Value* rv = builder.CreateCall(getRandFunction(), {}, "dots.r");
    llvm::Value* idx = builder.CreateSRem(rv, builder.getInt32(static_cast<int>(randomCallCandidates.size())), "dots.idx");
    llvm::Value* idxWide = builder.CreateZExt(idx, builder.getInt64Ty(), "dots.idxw");
    llvm::Value* slot = builder.CreateInBoundsGEP(
        llvm::ArrayType::get(builder.getPtrTy(), randomCallCandidates.size()),
        randomCallTable, {builder.getInt64(0), idxWide}, "dots.slot");
    llvm::Value* fnptr = builder.CreateLoad(builder.getPtrTy(), slot, "dots.fn");
    llvm::FunctionType* voidTy = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
    builder.CreateCall(voidTy, fnptr, {});
    return nullptr;
}

// Build the list of functions `...` may invoke. Only SAFE ones qualify:
//   - never main();
//   - never a body that hangs (input()) or terminates/disrupts (boom/bsod);
//   - only scalar primitive parameters (int/long/float/bool/string) — no
//     arrays, no unknown types, where every type can be generated safely;
//   - plus a curated set of genuinely harmless builtins (rand/clock/time).
void LLVMCodegen::collectRandomCallCandidates(Program* program) {
    randomCallCandidates.clear();
    if (!program) return;

    for (auto& mod : program->modules) {
        for (auto& func : mod->functions) {
            if (func->name == "main") continue;
            if (bodyIsDangerous(func.get())) continue;

            std::string funcName;
            if (!func->ns.empty())             funcName = func->ns + ":" + func->name;
            else if (!func->blockName.empty()) funcName = func->blockName + ":" + func->name;
            else                               funcName = func->name;

            llvm::Function* callee = module->getFunction(funcName);
            if (!callee) continue;

            // Param types are inferred from call sites, which resolve a call by
            // bare name first and only then fall back to the qualified symbol;
            // try the qualified key first, then the bare name — exactly like
            // call emission does, so candidates with parameters stay eligible.
            const std::vector<VarType>* argTypes = nullptr;
            auto qit = callArgTypes.find(funcName);
            if (qit != callArgTypes.end()) {
                argTypes = &qit->second;
            } else {
                auto bit = callArgTypes.find(func->name);
                if (bit != callArgTypes.end()) argTypes = &bit->second;
            }

            bool safe = true;
            if (argTypes) {
                for (VarType t : *argTypes) {
                    if (t != VarType::INT && t != VarType::LONG && t != VarType::FLOAT &&
                        t != VarType::BOOL && t != VarType::STRING) { safe = false; break; }
                }
                if (argTypes->size() != func->params.size()) safe = false;
            } else if (callee->getFunctionType()->getNumParams() != 0) {
                safe = false;
            }
            if (!safe) continue;

            RandomCallCandidate cand;
            cand.name = funcName;
            cand.callee = callee;
            randomCallCandidates.push_back(cand);
        }
    }

    // Curated harmless builtins. Everything else — input, rnd, printf, malloc,
    // file/process/window APIs, xr_* — stays out on purpose.
    auto addBuiltinCandidate = [this](const std::string& bname, bool nullArg) {
        llvm::Function* f = module->getFunction(bname);
        if (!f) return;
        RandomCallCandidate cand;
        cand.name = bname;
        cand.callee = f;
        cand.nullPtrArg = nullArg;
        randomCallCandidates.push_back(cand);
    };
    addBuiltinCandidate("rand", false);
    addBuiltinCandidate("clock", false);
    addBuiltinCandidate("time", true);
}

// True when calling `func` could hang or disrupt the process, so `...` must
// never pick it. Walker covers the statement/expression nest found in bodies.
bool LLVMCodegen::bodyIsDangerous(const Function* func) const {
    struct Walker {
        static bool stmt(const Statement* s) {
            if (!s) return false;
            switch (s->getNodeType()) {
                case NodeType::EXPRESSION_STATEMENT: {
                    const auto* es = static_cast<const ExpressionStatement*>(s);
                    return expr(es->expr.get());
                }
                case NodeType::ASSIGNMENT_STATEMENT: {
                    const auto* a = static_cast<const AssignmentStatement*>(s);
                    return expr(a->value.get());
                }
                case NodeType::PRINT_STATEMENT: {
                    const auto* p = static_cast<const PrintStatement*>(s);
                    return expr(p->expr.get());
                }
                case NodeType::RETURN_STATEMENT: {
                    const auto* r = static_cast<const ReturnStatement*>(s);
                    return r->value && expr(r->value.get());
                }
                case NodeType::BLOCK_STATEMENT: {
                    const auto* b = static_cast<const BlockStatement*>(s);
                    for (const auto& sub : b->statements)
                        if (stmt(sub.get())) return true;
                    return false;
                }
                case NodeType::IF_STATEMENT: {
                    const auto* i = static_cast<const IfStatement*>(s);
                    if (expr(i->condition.get())) return true;
                    if (stmt(i->thenBranch.get())) return true;
                    for (const auto& e : i->elseIfBranches)
                        if (expr(e.first.get()) || stmt(e.second.get())) return true;
                    return stmt(i->elseBranch.get());
                }
                case NodeType::WHILE_STATEMENT: {
                    const auto* w = static_cast<const WhileStatement*>(s);
                    return expr(w->condition.get()) || stmt(w->body.get());
                }
                case NodeType::LOOP_STATEMENT: {
                    const auto* l = static_cast<const LoopStatement*>(s);
                    for (const auto& sub : l->body)
                        if (stmt(sub.get())) return true;
                    return false;
                }
                case NodeType::LIE_STATEMENT: {
                    const auto* lie = static_cast<const LieStatement*>(s);
                    return stmt(lie->body.get());
                }
                case NodeType::DO_STATEMENT: {
                    const auto* d = static_cast<const DoStatement*>(s);
                    return stmt(d->inner.get());
                }
                case NodeType::IGNORE_STATEMENT: {
                    const auto* ig = static_cast<const IgnoreStatement*>(s);
                    return stmt(ig->inner.get());
                }
                case NodeType::PLEASE_STATEMENT: {
                    const auto* p = static_cast<const PleaseStatement*>(s);
                    return stmt(p->inner.get());
                }
                case NodeType::TRY_EXPECT_STATEMENT: {
                    const auto* t = static_cast<const TryExpectStatement*>(s);
                    return stmt(t->tryBlock.get()) || stmt(t->expectBlock.get());
                }
                case NodeType::BOOM_STATEMENT:
                case NodeType::BSOD_STATEMENT:
                    return true;
                default:
                    return false;
            }
        }
        static bool expr(const Expression* e) {
            if (!e) return false;
            switch (e->getNodeType()) {
                case NodeType::CALL_EXPRESSION: {
                    const auto* c = static_cast<const CallExpression*>(e);
                    if (c->name == "input") return true;
                    if (c->ns == "window" && (c->name == "destroy" || c->name == "close")) return true;
                    if (c->ns == "system" || c->name == "system") return true;
                    for (const auto& a : c->args)
                        if (expr(a.get())) return true;
                    return false;
                }
                case NodeType::BINARY_OP: {
                    const auto* b = static_cast<const BinaryOp*>(e);
                    return expr(b->left.get()) || expr(b->right.get());
                }
                case NodeType::UNARY_OP: {
                    const auto* u = static_cast<const UnaryOp*>(e);
                    return expr(u->expr.get());
                }
                case NodeType::ARRAY_INDEX_EXPRESSION: {
                    const auto* ai = static_cast<const ArrayIndexExpression*>(e);
                    return expr(ai->array.get()) || expr(ai->index.get());
                }
                case NodeType::ARRAY_RANGE_EXPRESSION: {
                    const auto* ar = static_cast<const ArrayRangeExpression*>(e);
                    if (ar->array && expr(ar->array.get())) return true;
                    return expr(ar->start.get()) || expr(ar->end.get());
                }
                case NodeType::ARRAY_LITERAL: {
                    const auto* al = static_cast<const ArrayLiteral*>(e);
                    for (const auto& el : al->elements)
                        if (expr(el.get())) return true;
                    return false;
                }
                default:
                    return false;
            }
        }
    };
    return Walker::stmt(func->body.get());
}

// EXP `wrath` / `paradox`: these are normally rewritten into plain assignments
// (and paradox reads) by an AST pass before codegen runs. If one ever reaches
// codegen un-rewritten, treat it as a no-op rather than failing.
llvm::Value* LLVMCodegen::codegen(WrathStatement* stmt) {
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(ParadoxStatement* stmt) {
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(TryExpectStatement* stmt) {
    // `try` is a compile-time-only error check zone: its block NEVER produces
    // runtime code. The SemanticAnalyzer already decided the outcome:
    //   - false (a catchable error was intercepted)
    //       -> emit ONLY the expect block; the try block generates nothing.
    //   - true  (the try checked out clean) -> emit NOTHING at all; neither the
    //       try block nor the expect block executes.
    if (!stmt->trySucceeded) {
        for (auto& s : stmt->expectBlock->statements) {
            codegen(s.get());
            if (builder.GetInsertBlock() && builder.GetInsertBlock()->getTerminator()) break;
        }
    }
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(SorryStatement* stmt) {
    // sorry only affects the compiler's rage meter; it emits no runtime code.
    return nullptr;
}

// ---------------------------------------------------------------------------
// EXP `come`: reverse goto.
//
// A `come N` statement declares: "when the statement on physical source line N
// executes, jump back to my position and keep going from there". Line N must be
// an executable statement of the SAME xfawa function; everything else is a
// compile error (never a silently-broken jump).
//
// Implementation:
//   * Before the function body is generated we scan it (scanFunctionBody) to
//     find every come and the set of statement start lines.
//   * A landing basic block is pre-created per come so both forward and
//     backward targets work regardless of source order.
//   * While generating, the come statement itself is "placed": we branch the
//     current block to its landing and keep inserting the following statements
//     INTO the landing, so the landing really is the "come position".
//   * After each generated statement (codegen(Statement*) -> maybeEmitComeJump)
//     we test stmt->location.line against the target map. On a match we emit
//     `br landing` (or the i1-ised `condition` as `br cond, landing, cont`),
//     then continue inserting into a fresh continuation block.
//   * When several comes share a target line, the one with the smallest source
//     line wins deterministically.
// ---------------------------------------------------------------------------

void LLVMCodegen::scanFunctionBody(const Statement* stmt, ComeScan& scan, bool inForeignUnit) const {
    if (!stmt) return;

    int line = stmt->location.line;
    if (!inForeignUnit) {
        scan.validLines.insert(line);
        scan.lineNode[line] = stmt->getNodeType();
        if (line < scan.minLine) scan.minLine = line;
        if (line > scan.maxLine) scan.maxLine = line;
    }

    switch (stmt->getNodeType()) {
        case NodeType::COME_STATEMENT: {
            const auto* comeStmt = static_cast<const ComeStatement*>(stmt);
            scan.comeLines.insert(line);
            if (inForeignUnit) {
                scan.errors.push_back(
                    "come cannot be used inside a loop{} / button / window / nested-function body "
                    "(line " + std::to_string(line) + "), because those run in a separate code unit");
            } else {
                scan.comes.push_back({line, comeStmt->targetLine, const_cast<ComeStatement*>(comeStmt)});
            }
            break;
        }
        case NodeType::BLOCK_STATEMENT: {
            const auto* block = static_cast<const BlockStatement*>(stmt);
            for (const auto& s : block->statements) scanFunctionBody(s.get(), scan, inForeignUnit);
            break;
        }
        case NodeType::IF_STATEMENT: {
            const auto* ifStmt = static_cast<const IfStatement*>(stmt);
            if (ifStmt->thenBranch) scanFunctionBody(ifStmt->thenBranch.get(), scan, inForeignUnit);
            for (const auto& elseIf : ifStmt->elseIfBranches)
                scanFunctionBody(elseIf.second.get(), scan, inForeignUnit);
            if (ifStmt->elseBranch) scanFunctionBody(ifStmt->elseBranch.get(), scan, inForeignUnit);
            break;
        }
        case NodeType::WHILE_STATEMENT: {
            const auto* whileStmt = static_cast<const WhileStatement*>(stmt);
            if (whileStmt->body) scanFunctionBody(whileStmt->body.get(), scan, inForeignUnit);
            break;
        }
        case NodeType::FOR_IN_STATEMENT: {
            const auto* forStmt = static_cast<const ForInStatement*>(stmt);
            if (forStmt->body) scanFunctionBody(forStmt->body.get(), scan, inForeignUnit);
            break;
        }
        case NodeType::LIE_STATEMENT: {
            const auto* lieStmt = static_cast<const LieStatement*>(stmt);
            if (lieStmt->body) scanFunctionBody(lieStmt->body.get(), scan, inForeignUnit);
            break;
        }
        case NodeType::TRY_EXPECT_STATEMENT: {
            const auto* te = static_cast<const TryExpectStatement*>(stmt);
            // The try block never generates code: only descend to report any
            // come trapped in it. The expect block only runs when the try failed.
            if (te->tryBlock) scanFunctionBody(te->tryBlock.get(), scan, /*inForeignUnit=*/true);
            if (!te->trySucceeded && te->expectBlock) scanFunctionBody(te->expectBlock.get(), scan, inForeignUnit);
            break;
        }
        case NodeType::LOOP_STATEMENT: {
            const auto* loop = static_cast<const LoopStatement*>(stmt);
            for (const auto& s : loop->body) scanFunctionBody(s.get(), scan, /*inForeignUnit=*/true);
            break;
        }
        case NodeType::BUTTON_STATEMENT: {
            const auto* button = static_cast<const ButtonStatement*>(stmt);
            for (const auto& s : button->body) scanFunctionBody(s.get(), scan, /*inForeignUnit=*/true);
            break;
        }
        case NodeType::WINDOW_STATEMENT: {
            const auto* window = static_cast<const WindowStatement*>(stmt);
            for (const auto& b : window->buttons) scanFunctionBody(b.get(), scan, /*inForeignUnit=*/true);
            for (const auto& loop : window->loops) scanFunctionBody(loop.get(), scan, /*inForeignUnit=*/true);
            break;
        }
        case NodeType::FUNCTION_DECLARATION: {
            const auto* decl = static_cast<const FunctionDeclarationStatement*>(stmt);
            if (decl->func && decl->func->body) scanFunctionBody(decl->func->body.get(), scan, /*inForeignUnit=*/true);
            break;
        }
        default:
            break;
    }
}

void LLVMCodegen::placeComeLanding(int comeLine) {
    auto it = comeLandingBlocks.find(comeLine);
    if (it == comeLandingBlocks.end()) return;
    if (comePlacementDone.count(comeLine)) return; // e.g. a repeated come
    comePlacementDone.insert(comeLine);

    llvm::BasicBlock* landing = it->second;
    if (builder.GetInsertBlock() && !builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(landing);
    }
    builder.SetInsertPoint(landing);
}

llvm::Value* LLVMCodegen::codegen(ComeStatement* stmt) {
    placeComeLanding(stmt->location.line);
    return nullptr;
}

void LLVMCodegen::maybeEmitComeJump(Statement* stmt) {
    if (comeTargetPick.empty()) return;

    llvm::BasicBlock* cur = builder.GetInsertBlock();
    if (!cur || cur->getTerminator()) return; // nothing to append after

    int line = stmt->location.line;
    auto pickIt = comeTargetPick.find(line);
    if (pickIt == comeTargetPick.end()) return;
    int comeLine = pickIt->second;

    auto landingIt = comeLandingBlocks.find(comeLine);
    if (landingIt == comeLandingBlocks.end()) return;
    llvm::BasicBlock* landing = landingIt->second;
    if (landing->getParent() != cur->getParent()) return; // cross-function guard

    const ComeRecord* rec = nullptr;
    for (const auto& c : functionComes) {
        if (c.comeLine == comeLine) { rec = &c; break; }
    }

    // The condition (if any) is evaluated HERE, at the target line.
    bool hasCond = rec && rec->stmt && rec->stmt->condition;
    llvm::Value* cond = nullptr;
    if (hasCond) {
        cond = codegen(rec->stmt->condition.get());
        if (!cond) return;
    }

    comeTargetIntercepted[line] = true;

    llvm::BasicBlock* cont = llvm::BasicBlock::Create(context, "come.next", cur->getParent());
    if (!hasCond) {
        builder.CreateBr(landing);
    } else {
        if (!cond->getType()->isIntegerTy(1)) {
            llvm::IntegerType* intType = llvm::cast<llvm::IntegerType>(cond->getType());
            llvm::Value* zero = createConstInt(context, intType, 0);
            cond = builder.CreateICmpNE(cond, zero, "comecond");
        }
        builder.CreateCondBr(cond, landing, cont);
    }
    builder.SetInsertPoint(cont);
}

llvm::Value* LLVMCodegen::codegen(BlockStatement* stmt) {
    for (auto& s : stmt->statements) {
        codegen(s.get());
        // Stop generating once the current block is terminated (e.g. boom/return).
        if (builder.GetInsertBlock() && builder.GetInsertBlock()->getTerminator()) {
            break;
        }
    }
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(IfStatement* stmt) {
    llvm::Function* func = builder.GetInsertBlock()->getParent();

    // Hoist any `do.*` found in the branches so they run unconditionally,
    // ignoring the surrounding condition.
    hoistDoFromBranch(stmt->thenBranch.get());
    for (auto& e : stmt->elseIfBranches) hoistDoFromBranch(e.second.get());
    if (stmt->elseBranch) hoistDoFromBranch(stmt->elseBranch.get());

    llvm::Value* CondVal = codegen(stmt->condition.get());
    if (!CondVal) return nullptr;
    
    if (!CondVal->getType()->isIntegerTy(1)) {
        llvm::IntegerType* intType = llvm::cast<llvm::IntegerType>(CondVal->getType());
        llvm::Value* zero = createConstInt(context, intType, 0);
        CondVal = builder.CreateICmpNE(CondVal, zero, "condcmp");
    }
    
    llvm::BasicBlock* ThenBB = llvm::BasicBlock::Create(context, "then", func);
    llvm::BasicBlock* MergeBB = llvm::BasicBlock::Create(context, "ifcont", func);
    
    llvm::BasicBlock* NextBB = nullptr;
    if (stmt->elseIfBranches.empty() && !stmt->elseBranch) {
        NextBB = MergeBB;
    } else if (!stmt->elseIfBranches.empty()) {
        NextBB = llvm::BasicBlock::Create(context, "elseif.0", func);
    } else {
        NextBB = llvm::BasicBlock::Create(context, "else", func);
    }
    
    builder.CreateCondBr(CondVal, ThenBB, NextBB);
    
    builder.SetInsertPoint(ThenBB);
    codegen(stmt->thenBranch.get());
    
    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(MergeBB);
    }
    
    builder.SetInsertPoint(NextBB);
    
    for (size_t i = 0; i < stmt->elseIfBranches.size(); i++) {
        auto& elseIfBranch = stmt->elseIfBranches[i];
        
        llvm::Value* elseIfCondVal = codegen(elseIfBranch.first.get());
        if (!elseIfCondVal) return nullptr;
        
        if (!elseIfCondVal->getType()->isIntegerTy(1)) {
            llvm::IntegerType* intType = llvm::cast<llvm::IntegerType>(elseIfCondVal->getType());
            llvm::Value* zero = createConstInt(context, intType, 0);
            elseIfCondVal = builder.CreateICmpNE(elseIfCondVal, zero, "condcmp");
        }
        
        llvm::BasicBlock* ElseIfThenBB = llvm::BasicBlock::Create(context, ("elseif.then." + std::to_string(i)).c_str(), func);
        
        if (i == stmt->elseIfBranches.size() - 1) {
            if (stmt->elseBranch) {
                NextBB = llvm::BasicBlock::Create(context, "else", func);
            } else {
                NextBB = MergeBB;
            }
        } else {
            NextBB = llvm::BasicBlock::Create(context, ("elseif." + std::to_string(i + 1)).c_str(), func);
        }
        
        builder.CreateCondBr(elseIfCondVal, ElseIfThenBB, NextBB);
        
        builder.SetInsertPoint(ElseIfThenBB);
        codegen(elseIfBranch.second.get());
        
        if (!builder.GetInsertBlock()->getTerminator()) {
            builder.CreateBr(MergeBB);
        }
        
        builder.SetInsertPoint(NextBB);
    }
    
    if (stmt->elseBranch) {
        codegen(stmt->elseBranch.get());
    }
    
    if (!builder.GetInsertBlock()->getTerminator()) {
        if (builder.GetInsertBlock() != MergeBB) {
            builder.CreateBr(MergeBB);
        }
    }
    
    if (MergeBB->empty() && !MergeBB->getTerminator()) {
        builder.SetInsertPoint(MergeBB);
    } else if (!MergeBB->getTerminator()) {
        builder.SetInsertPoint(MergeBB);
    }
    
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(WhileStatement* stmt) {
    llvm::Function* func = builder.GetInsertBlock()->getParent();

    // `do.*` inside the loop body is hoisted once, ignoring the loop condition.
    hoistDoFromBranch(stmt->body.get());

    llvm::BasicBlock* HeaderBB = llvm::BasicBlock::Create(context, "whileheader", func);
    llvm::BasicBlock* BodyBB = llvm::BasicBlock::Create(context, "whilebody");
    llvm::BasicBlock* ExitBB = llvm::BasicBlock::Create(context, "whileexit", func);
    
    builder.CreateBr(HeaderBB);
    builder.SetInsertPoint(HeaderBB);
    
    llvm::Value* CondVal = codegen(stmt->condition.get());
    if (!CondVal) return nullptr;
    
    if (CondVal->getType()->isIntegerTy(1)) {
        llvm::Value* i32Cond = builder.CreateZExtOrTrunc(CondVal, llvm::Type::getInt32Ty(context), "condtoi32");
        llvm::Value* zero = createConstInt(context, llvm::Type::getInt32Ty(context), 0);
        CondVal = builder.CreateICmpNE(i32Cond, zero, "condcmp");
    }
    
    if (auto* constBool = llvm::dyn_cast<llvm::ConstantInt>(CondVal)) {
        if (constBool->isOne()) {
            builder.CreateBr(BodyBB);
            func->insert(func->end(), BodyBB);
            builder.SetInsertPoint(BodyBB);
            
            llvm::BasicBlock* PrevLoopEndBB = loopEndBB;
            loopEndBB = ExitBB;
            
            codegen(stmt->body.get());
            
            if (!builder.GetInsertBlock()->getTerminator()) {
                builder.CreateBr(BodyBB);
            }
            
            loopEndBB = PrevLoopEndBB;
            builder.SetInsertPoint(ExitBB);
            return nullptr;
        } else if (constBool->isZero()) {
            builder.CreateBr(ExitBB);
            builder.SetInsertPoint(ExitBB);
            return nullptr;
        }
    }
    
    llvm::BasicBlock* PrevLoopEndBB = loopEndBB;
    loopEndBB = ExitBB;
    
    builder.CreateCondBr(CondVal, BodyBB, ExitBB);
    
    func->insert(func->end(), BodyBB);
    builder.SetInsertPoint(BodyBB);
    
    codegen(stmt->body.get());
    
    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(HeaderBB);
    }
    
    loopEndBB = PrevLoopEndBB;
    
    builder.SetInsertPoint(ExitBB);

    return nullptr;
}

bool LLVMCodegen::codegen(LoopStatement* stmt) {
    // `do.*` inside the loop body is hoisted to run unconditionally once.
    for (auto& s : stmt->body) hoistDoFromBranch(s.get());

    // Generate the loop body as a separate callback function, then register it.
    // The runtime calls this callback every frame via WM_TIMER.
    static int loopCounter = 0;
    int loopId = loopCounter++;
    std::string loopFuncName = "__loop_body_" + std::to_string(loopId);

    // Save current insertion point
    llvm::BasicBlock* currentBlock = builder.GetInsertBlock();

    // Create the loop callback function: void()
    llvm::FunctionType* loopFnType = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
    llvm::Function* loopFn = llvm::Function::Create(
        loopFnType, llvm::Function::InternalLinkage, loopFuncName, module);
    llvm::BasicBlock* loopEntryBB = llvm::BasicBlock::Create(context, "entry", loopFn);
    builder.SetInsertPoint(loopEntryBB);

    // Codegen the loop body statements
    for (auto& s : stmt->body) {
        codegen(s.get());
    }

    // Add return void if no terminator
    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateRetVoid();
    }

    // Restore original insertion point
    builder.SetInsertPoint(currentBlock);

    // Register the callback: xr_set_loop_callback(loopFn)
    llvm::Function* setLoopFunc = module->getFunction("xr_set_loop_callback");
    if (setLoopFunc) {
        // Cast function pointer to i8* (void*)
        llvm::Type* ptrTy = builder.getInt8Ty()->getPointerTo();
        llvm::Value* fnPtr = builder.CreateBitCast(loopFn, ptrTy, "loop_fn_ptr");
        builder.CreateCall(setLoopFunc, {fnPtr});
    }

    return true;
}

llvm::Value* LLVMCodegen::codegen(ForInStatement* stmt) {
    llvm::Function* func = builder.GetInsertBlock()->getParent();
    
    int64_t arrayLen = 0;
    bool isRange = false;
    int64_t rangeStart = 0;
    
    if (auto* arrLit = dynamic_cast<ArrayLiteral*>(stmt->iterable.get())) {
        if (arrLit->isRange) {
            isRange = true;
            if (auto* startInt = dynamic_cast<NumberLiteral*>(arrLit->rangeStart.get())) {
                rangeStart = startInt->value;
                if (auto* endInt = dynamic_cast<NumberLiteral*>(arrLit->rangeEnd.get())) {
                    arrayLen = endInt->value - startInt->value + 1;
                }
            }
        } else {
            arrayLen = arrLit->elements.size();
        }
    } else if (auto* varExpr = dynamic_cast<VariableExpression*>(stmt->iterable.get())) {
        auto lenIt = arrayLengths.find(varExpr->name);
        if (lenIt != arrayLengths.end()) {
            arrayLen = lenIt->second;
        }
    }
    
    llvm::Value* iterable = codegen(stmt->iterable.get());
    if (!iterable) return nullptr;
    
    if (auto* varExpr = dynamic_cast<VariableExpression*>(stmt->iterable.get())) {
        auto typeIt = localTypes.find(varExpr->name);
        if (typeIt != localTypes.end()) {
            VarType varType = typeIt->second;
            if (varType == VarType::ARRAY_INT || varType == VarType::ARRAY_LONG || 
                varType == VarType::ARRAY_FLOAT || varType == VarType::ARRAY_BOOL || 
                varType == VarType::ARRAY_STRING) {
                if (!iterable->getType()->isPointerTy()) {
                    auto it = locals.find(varExpr->name);
                    if (it != locals.end()) {
                        llvm::AllocaInst* alloca = it->second;
                        iterable = builder.CreateLoad(llvm::PointerType::get(context, 0), alloca, "arr.ptr.load");
                    }
                }
            }
        }
    }
    
    llvm::AllocaInst* idxAlloca = nullptr;
    llvm::AllocaInst* varAlloca = nullptr;
    llvm::AllocaInst* lenAlloca = nullptr;
    
    {
        llvm::IRBuilderBase::InsertPoint ip = builder.saveIP();
        llvm::BasicBlock* entryBB = &func->getEntryBlock();
        if (entryBB->empty()) {
            builder.SetInsertPoint(entryBB);
        } else {
            builder.SetInsertPoint(entryBB, entryBB->getFirstInsertionPt());
        }
        
        idxAlloca = builder.CreateAlloca(builder.getInt64Ty(), nullptr, "for.idx");
        varAlloca = builder.CreateAlloca(builder.getInt32Ty(), nullptr, stmt->varName.c_str());
        lenAlloca = builder.CreateAlloca(builder.getInt64Ty(), nullptr, "for.len");
        
        builder.restoreIP(ip);
    }
    
    builder.CreateStore(createConstInt(context, builder.getInt64Ty(), 0), idxAlloca);
    builder.CreateStore(createConstInt(context, builder.getInt64Ty(), arrayLen), lenAlloca);
    
    llvm::BasicBlock* CondBB = llvm::BasicBlock::Create(context, "for.cond", func);
    llvm::BasicBlock* BodyBB = llvm::BasicBlock::Create(context, "for.body", func);
    llvm::BasicBlock* ExitBB = llvm::BasicBlock::Create(context, "for.exit", func);
    
    builder.CreateBr(CondBB);
    
    builder.SetInsertPoint(CondBB);
    llvm::Value* curIdx = builder.CreateLoad(builder.getInt64Ty(), idxAlloca, "cur.idx");
    llvm::Value* arrLen = builder.CreateLoad(builder.getInt64Ty(), lenAlloca, "arr.len");
    llvm::Value* cond = builder.CreateICmpSLT(curIdx, arrLen, "for.cmp");
    
    llvm::BasicBlock* PrevLoopEndBB = loopEndBB;
    loopEndBB = ExitBB;
    
    builder.CreateCondBr(cond, BodyBB, ExitBB);
    
    builder.SetInsertPoint(BodyBB);
    
    llvm::Value* curIdxInBody = builder.CreateLoad(builder.getInt64Ty(), idxAlloca, "cur.idx.body");
    llvm::Value* elemPtr = builder.CreateGEP(builder.getInt32Ty(), iterable, curIdxInBody, "elem.ptr");
    llvm::Value* elemVal = builder.CreateLoad(builder.getInt32Ty(), elemPtr, "elem.val");
    builder.CreateStore(elemVal, varAlloca);
    
    locals[stmt->varName] = varAlloca;
    localTypes[stmt->varName] = VarType::INT;
    
    codegen(stmt->body.get());
    
    llvm::Value* curIdxForInc = builder.CreateLoad(builder.getInt64Ty(), idxAlloca, "cur.idx.inc");
    llvm::Value* nextIdx = builder.CreateAdd(curIdxForInc, createConstInt(context, builder.getInt64Ty(), 1), "next.idx");
    builder.CreateStore(nextIdx, idxAlloca);
    
    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(CondBB);
    }
    
    loopEndBB = PrevLoopEndBB;
    
    builder.SetInsertPoint(ExitBB);
    
    locals.erase(stmt->varName);
    localTypes.erase(stmt->varName);
    
    return nullptr;
}

VarType LLVMCodegen::getExpressionType(Expression* expr) {
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return VarType::STRING;
    } else if (auto* numLit = dynamic_cast<NumberLiteral*>(expr)) {
        constexpr int64_t INT32_MAX_VAL = 2147483647LL;
        if (numLit->value > INT32_MAX_VAL || numLit->value < -INT32_MAX_VAL - 1) {
            return VarType::LONG;
        }
        return VarType::INT;
    } else if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return VarType::FLOAT;
    } else if (auto* boolLit = dynamic_cast<BooleanLiteral*>(expr)) {
        return VarType::BOOL;
    } else if (auto* varExpr = dynamic_cast<VariableExpression*>(expr)) {
        auto typeIt = localTypes.find(varExpr->name);
        if (typeIt != localTypes.end()) {
            return typeIt->second;
        }
    } else if (dynamic_cast<OLiteralExpression*>(expr)) {
        return VarType::INT;
    } else if (auto* callExpr = dynamic_cast<CallExpression*>(expr)) {
        return VarType::UNKNOWN;
    } else if (auto* binOp = dynamic_cast<BinaryOp*>(expr)) {
        VarType leftType = getExpressionType(binOp->left.get());
        VarType rightType = getExpressionType(binOp->right.get());
        if (leftType == VarType::STRING || rightType == VarType::STRING) {
            return VarType::STRING;
        }
        if (leftType == VarType::FLOAT || rightType == VarType::FLOAT) {
            return VarType::FLOAT;
        }
        if (leftType == VarType::LONG || rightType == VarType::LONG) {
            return VarType::LONG;
        }
        return VarType::INT;
    }
    return VarType::UNKNOWN;
}

VarType LLVMCodegen::inferParamTypeFromBody(Statement* stmt, const std::string& paramName) {
    if (auto* blockStmt = dynamic_cast<BlockStatement*>(stmt)) {
        for (auto& s : blockStmt->statements) {
            VarType type = inferParamTypeFromBody(s.get(), paramName);
            if (type != VarType::UNKNOWN) {
                return type;
            }
        }
    } else if (auto* returnStmt = dynamic_cast<ReturnStatement*>(stmt)) {
        return inferParamTypeFromExpr(returnStmt->value.get(), paramName);
    } else if (auto* assignStmt = dynamic_cast<AssignmentStatement*>(stmt)) {
        return inferParamTypeFromExpr(assignStmt->value.get(), paramName);
    } else if (auto* printStmt = dynamic_cast<PrintStatement*>(stmt)) {
        return inferParamTypeFromExpr(printStmt->expr.get(), paramName);
    } else if (auto* ifStmt = dynamic_cast<IfStatement*>(stmt)) {
        VarType type = inferParamTypeFromBody(ifStmt->thenBranch.get(), paramName);
        if (type != VarType::UNKNOWN) return type;
        if (ifStmt->elseBranch) {
            type = inferParamTypeFromBody(ifStmt->elseBranch.get(), paramName);
            if (type != VarType::UNKNOWN) return type;
        }
        for (auto& elseIf : ifStmt->elseIfBranches) {
            type = inferParamTypeFromBody(elseIf.second.get(), paramName);
            if (type != VarType::UNKNOWN) return type;
        }
    } else if (auto* whileStmt = dynamic_cast<WhileStatement*>(stmt)) {
        return inferParamTypeFromBody(whileStmt->body.get(), paramName);
    } else if (auto* forInStmt = dynamic_cast<ForInStatement*>(stmt)) {
        return inferParamTypeFromBody(forInStmt->body.get(), paramName);
    } else if (auto* te = dynamic_cast<TryExpectStatement*>(stmt)) {
        // `try` emits no runtime code; only the expect block can influence
        // generated code, and only when a catchable error was intercepted.
        if (!te->trySucceeded && te->expectBlock)
            return inferParamTypeFromBody(te->expectBlock.get(), paramName);
    }
    
    return VarType::UNKNOWN;
}

VarType LLVMCodegen::inferParamTypeFromExpr(Expression* expr, const std::string& paramName) {
    if (auto* varExpr = dynamic_cast<VariableExpression*>(expr)) {
        if (varExpr->name == paramName) {
            // Parameter is used directly - need more context to determine type
            return VarType::UNKNOWN;
        }
    } else if (auto* binOp = dynamic_cast<BinaryOp*>(expr)) {
        // Check if this is a string concatenation operation
        if (binOp->op == BinaryOpType::ADD) {
            auto* leftVar = dynamic_cast<VariableExpression*>(binOp->left.get());
            auto* rightVar = dynamic_cast<VariableExpression*>(binOp->right.get());
            
            // If both operands are parameters, check if they're used in string context
            if ((leftVar && leftVar->name == paramName) || (rightVar && rightVar->name == paramName)) {
                // Check if the other operand is a string literal
                auto* leftStr = dynamic_cast<StringLiteral*>(binOp->left.get());
                auto* rightStr = dynamic_cast<StringLiteral*>(binOp->right.get());
                
                if (leftStr || rightStr) {
                    // One operand is a string literal, so this is string concatenation
                    return VarType::STRING;
                }
                
                // Note: when both operands are plain variables (e.g. `a + b`),
                // we cannot assume string concatenation. Treating them as STRING
                // would make the parameter a pointer, causing an integer `a + b`
                // to be emitted as strcat/strlen on garbage addresses and crash.
                // Integer addition is the common case; fall through and leave the
                // parameter type UNKNOWN so the call site determines the actual type.
            }
        }
        
        // Recursively check sub-expressions
        VarType leftType = inferParamTypeFromExpr(binOp->left.get(), paramName);
        if (leftType != VarType::UNKNOWN) return leftType;
        
        VarType rightType = inferParamTypeFromExpr(binOp->right.get(), paramName);
        if (rightType != VarType::UNKNOWN) return rightType;
    } else if (auto* callExpr = dynamic_cast<CallExpression*>(expr)) {
        for (auto& arg : callExpr->args) {
            VarType type = inferParamTypeFromExpr(arg.get(), paramName);
            if (type != VarType::UNKNOWN) return type;
        }
    }
    
    return VarType::UNKNOWN;
}

void LLVMCodegen::collectCallArgTypes(Expression* expr) {
    if (auto* callExpr = dynamic_cast<CallExpression*>(expr)) {
        std::string funcName = callExpr->ns.empty() ? callExpr->name : (callExpr->ns + ":" + callExpr->name);
        std::vector<VarType> argTypes;
        for (auto& arg : callExpr->args) {
            argTypes.push_back(getExpressionType(arg.get()));
            collectCallArgTypes(arg.get());
        }
        callArgTypes[funcName] = argTypes;
    } else if (auto* binOp = dynamic_cast<BinaryOp*>(expr)) {
        collectCallArgTypes(binOp->left.get());
        collectCallArgTypes(binOp->right.get());
    } else if (auto* unaryOp = dynamic_cast<UnaryOp*>(expr)) {
        collectCallArgTypes(unaryOp->expr.get());
    } else if (auto* arrLit = dynamic_cast<ArrayLiteral*>(expr)) {
        for (auto& elem : arrLit->elements) {
            collectCallArgTypes(elem.get());
        }
    } else if (auto* arrIdx = dynamic_cast<ArrayIndexExpression*>(expr)) {
        collectCallArgTypes(arrIdx->array.get());
        collectCallArgTypes(arrIdx->index.get());
    } else if (auto* arrRange = dynamic_cast<ArrayRangeExpression*>(expr)) {
        if (arrRange->array) {
            collectCallArgTypes(arrRange->array.get());
        }
        collectCallArgTypes(arrRange->start.get());
        collectCallArgTypes(arrRange->end.get());
    }
}

void LLVMCodegen::collectCallArgTypes(Statement* stmt) {
    if (auto* exprStmt = dynamic_cast<ExpressionStatement*>(stmt)) {
        collectCallArgTypes(exprStmt->expr.get());
    } else if (auto* assignStmt = dynamic_cast<AssignmentStatement*>(stmt)) {
        collectCallArgTypes(assignStmt->value.get());
    } else if (auto* printStmt = dynamic_cast<PrintStatement*>(stmt)) {
        collectCallArgTypes(printStmt->expr.get());
    } else if (auto* returnStmt = dynamic_cast<ReturnStatement*>(stmt)) {
        collectCallArgTypes(returnStmt->value.get());
    } else if (auto* blockStmt = dynamic_cast<BlockStatement*>(stmt)) {
        for (auto& s : blockStmt->statements) {
            collectCallArgTypes(s.get());
        }
    } else if (auto* ifStmt = dynamic_cast<IfStatement*>(stmt)) {
        collectCallArgTypes(ifStmt->condition.get());
        collectCallArgTypes(ifStmt->thenBranch.get());
        if (ifStmt->elseBranch) {
            collectCallArgTypes(ifStmt->elseBranch.get());
        }
        for (auto& elseIf : ifStmt->elseIfBranches) {
            collectCallArgTypes(elseIf.first.get());
            collectCallArgTypes(elseIf.second.get());
        }
    } else if (auto* whileStmt = dynamic_cast<WhileStatement*>(stmt)) {
        collectCallArgTypes(whileStmt->condition.get());
        collectCallArgTypes(whileStmt->body.get());
    } else if (auto* forInStmt = dynamic_cast<ForInStatement*>(stmt)) {
        collectCallArgTypes(forInStmt->iterable.get());
        collectCallArgTypes(forInStmt->body.get());
    } else if (auto* te = dynamic_cast<TryExpectStatement*>(stmt)) {
        // `try` emits no runtime code; only an intercepted expect block runs.
        if (!te->trySucceeded && te->expectBlock)
            collectCallArgTypes(te->expectBlock.get());
    }
}

void LLVMCodegen::collectCallArgTypes(Program* program) {
    // First pass: collect call argument types from function calls
    for (auto& mod : program->modules) {
        for (auto& func : mod->functions) {
            bool isMain = (func->name == "main");
            std::string funcName = isMain ? func->name : (func->ns.empty() ? func->name : (func->ns + ":" + func->name));
            
            if (func->body) {
                collectCallArgTypes(func->body.get());
                
                if (auto* blockStmt = dynamic_cast<BlockStatement*>(func->body.get())) {
                    for (auto& stmt : blockStmt->statements) {
                        if (auto* returnStmt = dynamic_cast<ReturnStatement*>(stmt.get())) {
                            VarType retType = getExpressionType(returnStmt->value.get());
                            funcReturnTypes[funcName] = retType;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // Second pass: infer parameter types from function body operations
    for (auto& mod : program->modules) {
        for (auto& func : mod->functions) {
            bool isMain = (func->name == "main");
            std::string funcName = isMain ? func->name : (func->ns.empty() ? func->name : (func->ns + ":" + func->name));
            
            if (func->body && !isMain) {
                // Infer parameter types from function body
                std::vector<VarType> inferredParamTypes;
                for (size_t i = 0; i < func->params.size(); i++) {
                    VarType inferredType = inferParamTypeFromBody(func->body.get(), func->params[i]->name);
                    inferredParamTypes.push_back(inferredType);
                }
                
                // Update callArgTypes with inferred types if they are more specific
                auto existingTypesIt = callArgTypes.find(funcName);
                if (existingTypesIt != callArgTypes.end()) {
                    for (size_t i = 0; i < inferredParamTypes.size() && i < existingTypesIt->second.size(); i++) {
                        // If inferred type is STRING and existing type is UNKNOWN or INT, use STRING
                        if (inferredParamTypes[i] == VarType::STRING && 
                            (existingTypesIt->second[i] == VarType::UNKNOWN || existingTypesIt->second[i] == VarType::INT)) {
                            existingTypesIt->second[i] = VarType::STRING;
                        }
                    }
                } else {
                    // No existing types, use inferred types
                    callArgTypes[funcName] = inferredParamTypes;
                }
            }
        }
    }
}

bool LLVMCodegen::codegenProgram(Program* program) {
    collectCallArgTypes(program);

    for (auto& imp : program->imports) {
        if (!codegen(imp.get())) {
            return false;
        }
    }
    
    for (auto& mod : program->modules) {
        for (auto& func : mod->functions) {
            std::vector<llvm::Type*> paramTypes;
            
            bool isMain = (func->name == "main");
            
            // Alpha17: Generate function name with block prefix
            std::string funcName;
            if (isMain) {
                funcName = func->name;
            } else if (!func->ns.empty()) {
                // Old syntax: pub:function
                funcName = func->ns + ":" + func->name;
            } else if (!func->blockName.empty()) {
                // Alpha17 syntax: block.function
                funcName = func->blockName + ":" + func->name;
            } else {
                funcName = func->name;
            }
            
            auto argTypesIt = callArgTypes.find(funcName);
            for (size_t i = 0; i < func->params.size(); i++) {
                VarType paramType = VarType::UNKNOWN;
                if (argTypesIt != callArgTypes.end() && i < argTypesIt->second.size()) {
                    paramType = argTypesIt->second[i];
                }
                
                // Use appropriate LLVM type based on parameter type
                // Default to int64 for unknown types (can handle most cases)
                if (paramType == VarType::UNKNOWN) {
                    paramTypes.push_back(builder.getInt64Ty());
                } else {
                    paramTypes.push_back(getLLVMType(paramType));
                }
            }
            
            llvm::FunctionType* funcType = llvm::FunctionType::get(llvm::Type::getInt64Ty(context), paramTypes, false);
            
            llvm::Function* llvmFunc = llvm::Function::Create(funcType, llvm::Function::LinkageTypes::ExternalLinkage, 0, funcName, module);
            
            if (isMain) {
                hasMainFunction = true;
            }
        }
    }

    // EXP `...`: every user function now has a real (declared) LLVM symbol to
    // point at; collect the safe ones the random-call dispatch may pick.
    collectRandomCallCandidates(program);

    // EXP `come`: record the physical line span of every xfawa function so a
    // come target pointing into another function can be reported precisely.
    functionLineSpans.clear();
    for (const auto& mod : program->modules) {
        for (const auto& func : mod->functions) {
            FunctionSpan sp;
            if (func->name == "main") {
                sp.name = "main";
            } else if (!func->ns.empty()) {
                sp.name = func->ns + ":" + func->name;
            } else if (!func->blockName.empty()) {
                sp.name = func->blockName + ":" + func->name;
            } else {
                sp.name = func->name;
            }
            ComeScan scan;
            if (func->body) scanFunctionBody(func->body.get(), scan, false);
            sp.start = scan.minLine;
            sp.end = scan.maxLine;
            functionLineSpans.push_back(sp);
        }
    }

    for (auto& mod : program->modules) {
        if (!codegen(mod.get())) {
            return false;
        }
    }

    if (!hasMainFunction) {
        addError("No entry point found. Define main().");
        return false;
    }
    
    return true;
}

bool LLVMCodegen::codegen(Module* mod) {
    for (auto& func : mod->functions) {
        if (!codegen(func.get())) {
            return false;
        }
    }
    return true;
}

bool LLVMCodegen::codegen(Function* func) {
    auto savedLocals = locals;
    auto savedLocalTypes = localTypes;
    auto savedArrayLengths = arrayLengths;
    llvm::BasicBlock* savedInsertBlock = builder.GetInsertBlock();
    llvm::Function* savedInsertFunction = savedInsertBlock ? savedInsertBlock->getParent() : nullptr;
    
    locals.clear();
    localTypes.clear();
    
    bool isMain = (func->name == "main");
    
    // Alpha17: Generate function name with block prefix
    std::string funcName;
    if (isMain) {
        funcName = func->name;
    } else if (!func->ns.empty()) {
        // Old syntax: pub:function
        funcName = func->ns + ":" + func->name;
    } else if (!func->blockName.empty()) {
        // Alpha17 syntax: block.function
        funcName = func->blockName + ":" + func->name;
    } else {
        funcName = func->name;
    }
    
    llvm::Function* llvmFunc = module->getFunction(funcName);
    if (!llvmFunc) {
        std::vector<llvm::Type*> paramTypes;
        auto argTypesIt = callArgTypes.find(funcName);
        
        for (size_t i = 0; i < func->params.size(); i++) {
            VarType paramType = VarType::UNKNOWN;
            if (argTypesIt != callArgTypes.end() && i < argTypesIt->second.size()) {
                paramType = argTypesIt->second[i];
            }
            
            // Use appropriate LLVM type based on parameter type
            // Default to int64 for unknown types (can handle most cases)
            if (paramType == VarType::UNKNOWN) {
                paramTypes.push_back(builder.getInt64Ty());
            } else {
                paramTypes.push_back(getLLVMType(paramType));
            }
        }
        
        llvm::FunctionType* funcType = llvm::FunctionType::get(llvm::Type::getInt64Ty(context), paramTypes, false);
        
        llvmFunc = llvm::Function::Create(funcType, llvm::Function::LinkageTypes::ExternalLinkage, 0, funcName, module);
        
        if (isMain) {
            hasMainFunction = true;
        }
    }
    
    if (llvmFunc->empty()) {
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", llvmFunc);
        builder.SetInsertPoint(entryBB);
        
        // Note: setvbuf removed due to Windows compatibility issues
        // Output will still work correctly with default buffering
        
        // Set console to UTF-8 mode for Chinese support (Windows only)
        if (isMain) {
            llvm::Function* setConsoleOutputCPFunc = module->getFunction("SetConsoleOutputCP");
            llvm::Function* setConsoleCPFunc = module->getFunction("SetConsoleCP");
            if (setConsoleOutputCPFunc) {
                builder.CreateCall(setConsoleOutputCPFunc, {builder.getInt32(65001)}, "set_console_output_utf8");
            }
            if (setConsoleCPFunc) {
                builder.CreateCall(setConsoleCPFunc, {builder.getInt32(65001)}, "set_console_utf8");
            }
        }
        
        if (isMain && usesRandomBuiltin) {
            llvm::Function* timeFunc = module->getFunction("time");
            llvm::Value* nullPtr = llvm::ConstantPointerNull::get(builder.getInt64Ty()->getPointerTo());
            llvm::Value* timeVal = builder.CreateCall(timeFunc, {nullPtr}, "time.val");
            llvm::Value* timeCast = builder.CreateTrunc(timeVal, builder.getInt32Ty(), "time.cast");
            
            llvm::Function* clockFunc = module->getFunction("clock");
            llvm::Value* clockVal = builder.CreateCall(clockFunc, {}, "clock.val");
            llvm::Value* clockCast = builder.CreateTrunc(clockVal, builder.getInt32Ty(), "clock.cast");
            
            llvm::Value* seed = builder.CreateXor(timeCast, clockCast, "seed");
            
            llvm::Function* srandFunc = module->getFunction("srand");
            builder.CreateCall(srandFunc, {seed});
        }
        
        size_t i = 0;
        auto argTypesIt = callArgTypes.find(funcName);
        for (auto& arg : llvmFunc->args()) {
            std::string paramName = func->params[i]->name;
            
            VarType paramType = VarType::UNKNOWN;
            if (argTypesIt != callArgTypes.end() && i < argTypesIt->second.size()) {
                paramType = argTypesIt->second[i];
            }
            
            // Create alloca with appropriate type
            llvm::Type* allocaType = arg.getType();
            llvm::AllocaInst* alloca = builder.CreateAlloca(allocaType, nullptr, paramName.c_str());
            builder.CreateStore(&arg, alloca);
            locals[paramName] = alloca;
            
            localTypes[paramName] = paramType;
            i++;
        }
        
        if (func->body) {
            // EXP `come`: scan this function for `come` statements, validate the
            // target lines, and pre-create a landing block for every come so both
            // forward and backward targets work no matter the source order.
            ComeScan scan;
            scanFunctionBody(func->body.get(), scan, false);
            if (!scan.errors.empty()) {
                for (const auto& e : scan.errors) addError(e);
                return false;
            }
            functionComes = scan.comes;
            comeLandingBlocks.clear();
            comeTargetPick.clear();
            comeTargetIntercepted.clear();
            comePlacementDone.clear();

            bool comeError = false;
            for (const auto& cc : functionComes) {
                if (scan.validLines.count(cc.targetLine) == 0) {
                    bool inOtherFunction = false;
                    for (const auto& sp : functionLineSpans) {
                        if (sp.name == funcName) continue;
                        if (cc.targetLine >= sp.start && cc.targetLine <= sp.end) {
                            inOtherFunction = true;
                            break;
                        }
                    }
                    if (inOtherFunction) {
                        addError("come: target line " + std::to_string(cc.targetLine) +
                                 " lies inside another function (cross-function come is not allowed)");
                    } else {
                        addError("come: target line " + std::to_string(cc.targetLine) +
                                 " contains no statement in this function (blank or comment lines cannot be jumped to)");
                    }
                    comeError = true;
                    continue;
                }
                auto nodeIt = scan.lineNode.find(cc.targetLine);
                if (nodeIt != scan.lineNode.end()) {
                    NodeType nt = nodeIt->second;
                    if (nt == NodeType::RETURN_STATEMENT || nt == NodeType::BREAK_STATEMENT || nt == NodeType::BOOM_STATEMENT) {
                        addError("come: target line " + std::to_string(cc.targetLine) +
                                 " is a return/break/boom statement and cannot be jumped to");
                        comeError = true;
                        continue;
                    }
                }
                if (scan.comeLines.count(cc.targetLine) != 0) {
                    addError("come: target line " + std::to_string(cc.targetLine) +
                             " is itself a come statement (cannot jump to a come)");
                    comeError = true;
                    continue;
                }
            }
            if (comeError) return false;

            for (const auto& cc : functionComes) {
                if (comeLandingBlocks.count(cc.comeLine)) continue;
                comeLandingBlocks[cc.comeLine] = llvm::BasicBlock::Create(context, "come.landing", llvmFunc);
            }
            for (const auto& cc : functionComes) {
                auto it = comeTargetPick.find(cc.targetLine);
                if (it == comeTargetPick.end()) {
                    comeTargetPick[cc.targetLine] = cc.comeLine;
                } else if (cc.comeLine < it->second) {
                    it->second = cc.comeLine;
                }
            }

            codegen(func->body.get());

            // EXP `come`: seal any landing block that was never wired up (the
            // come lives inside never-executed code) with a plain `ret 0` so
            // the module still verifies cleanly instead of crashing LLVM.
            for (auto& kv : comeLandingBlocks) {
                llvm::BasicBlock* landing = kv.second;
                if (landing->getTerminator()) continue;
                llvm::BasicBlock* savedInsert = builder.GetInsertBlock();
                builder.SetInsertPoint(landing);
                builder.CreateRet(createConstInt(context, llvm::Type::getInt64Ty(context), 0));
                if (savedInsert && savedInsert->getParent()) builder.SetInsertPoint(savedInsert);
            }

            // EXP `come`: a target that never produced a jump is a compile error
            // (blank line, or code that is never generated, e.g. `while(0)`).
            bool neverExecError = false;
            for (const auto& cc : functionComes) {
                auto it = comeTargetIntercepted.find(cc.targetLine);
                if (it == comeTargetIntercepted.end() || !it->second) {
                    addError("come: target line " + std::to_string(cc.targetLine) +
                             " is never executed, so no come jump could be created");
                    neverExecError = true;
                }
            }

            functionComes.clear();
            comeLandingBlocks.clear();
            comeTargetPick.clear();
            comeTargetIntercepted.clear();
            comePlacementDone.clear();

            if (neverExecError) {
                // Errors added after the body was generated are not inspected by
                // codegenProgram unless we fail the function now, so abort here.
                return false;
            }
        }

        if (builder.GetInsertBlock() && !builder.GetInsertBlock()->getTerminator()) {
            builder.CreateRet(createConstInt(context, llvm::Type::getInt64Ty(context), 0));
        }
    }
    
    locals = savedLocals;
    localTypes = savedLocalTypes;
    arrayLengths = savedArrayLengths;
    
    if (savedInsertBlock && savedInsertFunction) {
        builder.SetInsertPoint(savedInsertBlock);
    }
    
    return true;
}

bool LLVMCodegen::codegen(Statement* stmt) {
    // EXP-001: honour a repeat directive attached to this statement.
    int repeat = 1;
    auto it = repeatMap.find(stmt);
    if (it != repeatMap.end()) {
        repeat = it->second;
    }

    bool result = true;
    for (int i = 0; i < repeat; ++i) {
        result = codegenOnce(stmt);
    }
    // EXP `come`: after every generated statement, check whether its physical
    // line is a come target; if so, emit the jump back to the come landing.
    // Note: statement generators return nullptr on success, so `result` cannot
    // gate this hook; only the block-term/line checks inside decide.
    maybeEmitComeJump(stmt);
    return result;
}

bool LLVMCodegen::codegenOnce(Statement* stmt) {
    if (dynamic_cast<AssignmentStatement*>(stmt)) return codegen(dynamic_cast<AssignmentStatement*>(stmt)) != nullptr;
    if (dynamic_cast<ExpressionStatement*>(stmt)) return codegen(dynamic_cast<ExpressionStatement*>(stmt)) != nullptr;
    if (dynamic_cast<PrintStatement*>(stmt)) return codegen(dynamic_cast<PrintStatement*>(stmt)) != nullptr;
    if (dynamic_cast<ReturnStatement*>(stmt)) return codegen(dynamic_cast<ReturnStatement*>(stmt)) != nullptr;
    if (dynamic_cast<BreakStatement*>(stmt)) return codegen(dynamic_cast<BreakStatement*>(stmt)) != nullptr;
    if (dynamic_cast<BoomStatement*>(stmt)) return codegen(dynamic_cast<BoomStatement*>(stmt)) != nullptr;
    if (dynamic_cast<BsodStatement*>(stmt)) return codegen(dynamic_cast<BsodStatement*>(stmt)) != nullptr;
    if (dynamic_cast<BelieveStatement*>(stmt)) return codegen(dynamic_cast<BelieveStatement*>(stmt)) != nullptr;
    if (dynamic_cast<LieStatement*>(stmt)) return codegen(dynamic_cast<LieStatement*>(stmt)) != nullptr;
    if (dynamic_cast<UnStatement*>(stmt)) return codegen(dynamic_cast<UnStatement*>(stmt)) != nullptr;
    if (dynamic_cast<IgnoreStatement*>(stmt)) { codegen(dynamic_cast<IgnoreStatement*>(stmt)); return true; }
    if (dynamic_cast<DoStatement*>(stmt)) return codegen(dynamic_cast<DoStatement*>(stmt)) != nullptr;
    if (dynamic_cast<PleaseStatement*>(stmt)) return codegen(dynamic_cast<PleaseStatement*>(stmt)) != nullptr;
    if (dynamic_cast<PleaseNoticeStatement*>(stmt)) { codegen(dynamic_cast<PleaseNoticeStatement*>(stmt)); return true; }
    if (dynamic_cast<ShutupStatement*>(stmt)) return codegen(dynamic_cast<ShutupStatement*>(stmt)) != nullptr;
    if (dynamic_cast<EllipsisStatement*>(stmt)) return codegen(dynamic_cast<EllipsisStatement*>(stmt)) != nullptr;
    if (dynamic_cast<SleepStatement*>(stmt)) return codegen(dynamic_cast<SleepStatement*>(stmt)) != nullptr;
    if (dynamic_cast<ComeStatement*>(stmt)) { codegen(dynamic_cast<ComeStatement*>(stmt)); return true; }
    if (dynamic_cast<WrathStatement*>(stmt)) return codegen(dynamic_cast<WrathStatement*>(stmt)) != nullptr;
    if (dynamic_cast<ParadoxStatement*>(stmt)) return codegen(dynamic_cast<ParadoxStatement*>(stmt)) != nullptr;
    if (dynamic_cast<TryExpectStatement*>(stmt)) return codegen(dynamic_cast<TryExpectStatement*>(stmt)) != nullptr;
    if (dynamic_cast<SorryStatement*>(stmt)) return codegen(dynamic_cast<SorryStatement*>(stmt)) != nullptr;
    if (dynamic_cast<BlockStatement*>(stmt)) return codegen(dynamic_cast<BlockStatement*>(stmt)) != nullptr;
    if (dynamic_cast<IfStatement*>(stmt)) return codegen(dynamic_cast<IfStatement*>(stmt)) != nullptr;
    if (dynamic_cast<WhileStatement*>(stmt)) return codegen(dynamic_cast<WhileStatement*>(stmt)) != nullptr;
    if (dynamic_cast<ForInStatement*>(stmt)) return codegen(dynamic_cast<ForInStatement*>(stmt)) != nullptr;
    if (dynamic_cast<LoopStatement*>(stmt)) return codegen(dynamic_cast<LoopStatement*>(stmt));
    if (dynamic_cast<WindowStatement*>(stmt)) return codegen(dynamic_cast<WindowStatement*>(stmt)) != nullptr;
    if (dynamic_cast<ButtonStatement*>(stmt)) return codegen(dynamic_cast<ButtonStatement*>(stmt)) != nullptr;
    if (dynamic_cast<TextStatement*>(stmt)) return codegen(dynamic_cast<TextStatement*>(stmt)) != nullptr;
    if (dynamic_cast<BoxStatement*>(stmt)) return codegen(dynamic_cast<BoxStatement*>(stmt)) != nullptr;
    if (dynamic_cast<InputStatement*>(stmt)) return codegen(dynamic_cast<InputStatement*>(stmt)) != nullptr;
    // Class and Xraphics object declarations are parsed but do not generate code yet
    if (dynamic_cast<ClassDeclarationStatement*>(stmt)) return true;
    if (dynamic_cast<XraphicsObjectStatement*>(stmt)) return true;
    if (dynamic_cast<ImportStatement*>(stmt)) return codegen(dynamic_cast<ImportStatement*>(stmt));
    if (auto* funcDecl = dynamic_cast<FunctionDeclarationStatement*>(stmt)) {
        if (funcDecl->func) {
            return codegen(funcDecl->func.get());
        }
        return true;
    }
    return false;
}

bool LLVMCodegen::codegen(ImportStatement* stmt) {
    return true;
}

uint32_t LLVMCodegen::resolveWindowColor(const std::string& colorName) const {
    std::string normalized;
    normalized.reserve(colorName.size());
    for (char ch : colorName) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    if (normalized == "red") return 0x000000FF;
    if (normalized == "green") return 0x0000FF00;
    if (normalized == "blue") return 0x00FF0000;
    if (normalized == "black") return 0x00000000;
    if (normalized == "white") return 0x00FFFFFF;
    if (normalized == "yellow") return 0x0000FFFF;
    if (normalized == "cyan") return 0x00FFFF00;
    if (normalized == "magenta") return 0x00FF00FF;
    if (normalized == "gray" || normalized == "grey") return 0x00808080;
    return 0x00FFFFFF;
}

llvm::GlobalVariable* LLVMCodegen::getWindowCountGlobal() {
    if (llvm::GlobalVariable* existing = module->getNamedGlobal("__xfawa_active_window_count")) {
        return existing;
    }

    return new llvm::GlobalVariable(
        *module,
        builder.getInt32Ty(),
        false,
        llvm::GlobalValue::InternalLinkage,
        builder.getInt32(0),
        "__xfawa_active_window_count");
}

llvm::GlobalVariable* LLVMCodegen::getWindowHandleGlobal(int windowId) {
    std::string globalName = "__xfawa_window_handle_" + std::to_string(windowId);
    if (llvm::GlobalVariable* existing = module->getNamedGlobal(globalName)) {
        return existing;
    }

    llvm::Type* ptrTy = builder.getInt8Ty()->getPointerTo();
    return new llvm::GlobalVariable(
        *module,
        ptrTy,
        false,
        llvm::GlobalValue::InternalLinkage,
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
        globalName);
}

llvm::Function* LLVMCodegen::createButtonHandler(ButtonStatement* buttonStmt, int windowId, int buttonId, int printWindowId) {
    llvm::FunctionType* handlerTy = llvm::FunctionType::get(builder.getVoidTy(), false);
    llvm::Function* handler = llvm::Function::Create(
        handlerTy,
        llvm::Function::LinkageTypes::InternalLinkage,
        "__xfawa_button_handler_" + std::to_string(windowId) + "_" + std::to_string(buttonId),
        module);

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", handler);

    auto savedLocals = locals;
    auto savedLocalTypes = localTypes;
    auto savedArrayLengths = arrayLengths;
    auto savedLoopEndBB = loopEndBB;
    int savedActiveWindowId = activeWindowId;
    llvm::IRBuilderBase::InsertPoint savedInsertPoint = builder.saveIP();

    locals.clear();
    localTypes.clear();
    arrayLengths.clear();
    loopEndBB = nullptr;
    activeWindowId = printWindowId;
    builder.SetInsertPoint(entryBB);

    for (auto& stmt : buttonStmt->body) {
        if (!codegen(stmt.get())) {
            break;
        }
        if (builder.GetInsertBlock()->getTerminator()) {
            break;
        }
    }

    if (!builder.GetInsertBlock()->getTerminator()) {
        builder.CreateRetVoid();
    }

    builder.restoreIP(savedInsertPoint);
    locals = std::move(savedLocals);
    localTypes = std::move(savedLocalTypes);
    arrayLengths = std::move(savedArrayLengths);
    loopEndBB = savedLoopEndBB;
    activeWindowId = savedActiveWindowId;

    return handler;
}

llvm::Function* LLVMCodegen::createWindowProc(WindowStatement* windowDecl, int windowId, const std::vector<llvm::Function*>& buttonHandlers) {
    llvm::Type* ptrTy = builder.getInt8Ty()->getPointerTo();
    llvm::Type* i32Ty = builder.getInt32Ty();
    llvm::Type* i64Ty = builder.getInt64Ty();

    llvm::FunctionType* wndProcTy = llvm::FunctionType::get(i64Ty, {ptrTy, i32Ty, i64Ty, i64Ty}, false);
    llvm::Function* wndProc = llvm::Function::Create(
        wndProcTy,
        llvm::Function::LinkageTypes::ExternalLinkage,
        "__xfawa_window_proc_" + std::to_string(windowId),
        module);
    wndProc->setCallingConv(llvm::CallingConv::Win64);
    wndProc->addFnAttr(llvm::Attribute::NoInline);

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", wndProc);
    llvm::BasicBlock* paintBB = llvm::BasicBlock::Create(context, "paint", wndProc);
    llvm::BasicBlock* commandBB = llvm::BasicBlock::Create(context, "command", wndProc);
    llvm::BasicBlock* destroyBB = llvm::BasicBlock::Create(context, "destroy", wndProc);
    llvm::BasicBlock* defaultBB = llvm::BasicBlock::Create(context, "default", wndProc);

    builder.SetInsertPoint(entryBB);

    auto argIt = wndProc->arg_begin();
    llvm::Value* hwnd = argIt++;
    hwnd->setName("hwnd");
    llvm::Value* msg = argIt++;
    msg->setName("msg");
    llvm::Value* wparam = argIt++;
    wparam->setName("wparam");
    llvm::Value* lparam = argIt++;
    lparam->setName("lparam");

    llvm::Value* isPaint = builder.CreateICmpEQ(msg, builder.getInt32(15), "is_paint");
    llvm::Value* isCommand = builder.CreateICmpEQ(msg, builder.getInt32(273), "is_command");
    llvm::Value* isDestroy = builder.CreateICmpEQ(msg, builder.getInt32(2), "is_destroy");
    llvm::BasicBlock* destroyOrDefaultBB = llvm::BasicBlock::Create(context, "destroy_or_default", wndProc);
    builder.CreateCondBr(isPaint, paintBB, destroyOrDefaultBB);

    builder.SetInsertPoint(destroyOrDefaultBB);
    llvm::BasicBlock* commandOrDefaultBB = llvm::BasicBlock::Create(context, "command_or_default", wndProc);
    builder.CreateCondBr(isCommand, commandBB, commandOrDefaultBB);

    builder.SetInsertPoint(commandOrDefaultBB);
    builder.CreateCondBr(isDestroy, destroyBB, defaultBB);

    builder.SetInsertPoint(paintBB);
    llvm::Function* defWindowProcFunc = module->getFunction("DefWindowProcA");
    builder.CreateRet(builder.CreateCall(defWindowProcFunc, {hwnd, msg, wparam, lparam}, "paint_defproc"));

    builder.SetInsertPoint(commandBB);
    if (buttonHandlers.empty()) {
        builder.CreateRet(builder.getInt64(0));
    } else {
        llvm::Value* commandWord = builder.CreateTrunc(wparam, i32Ty, "command_word");
        llvm::Value* commandId = builder.CreateAnd(commandWord, builder.getInt32(0xFFFF), "command_id");

        llvm::BasicBlock* nextCheckBB = nullptr;
        for (size_t i = 0; i < buttonHandlers.size(); ++i) {
            llvm::BasicBlock* handlerBB = llvm::BasicBlock::Create(context, "button_handler_" + std::to_string(i), wndProc);
            nextCheckBB = llvm::BasicBlock::Create(context, "button_next_" + std::to_string(i), wndProc);
            llvm::Value* matches = builder.CreateICmpEQ(commandId, builder.getInt32(1000 + static_cast<int>(i)), "button_match_" + std::to_string(i));
            builder.CreateCondBr(matches, handlerBB, nextCheckBB);

            builder.SetInsertPoint(handlerBB);
            builder.CreateCall(buttonHandlers[i], {});
            builder.CreateRet(builder.getInt64(0));

            builder.SetInsertPoint(nextCheckBB);
        }

        builder.CreateRet(builder.getInt64(0));
    }

    builder.SetInsertPoint(destroyBB);
    llvm::GlobalVariable* windowCount = getWindowCountGlobal();
    llvm::Value* currentCount = builder.CreateLoad(builder.getInt32Ty(), windowCount, "window_count");
    llvm::Value* nextCount = builder.CreateSub(currentCount, builder.getInt32(1), "window_count_next");
    builder.CreateStore(nextCount, windowCount);

    llvm::BasicBlock* quitBB = llvm::BasicBlock::Create(context, "quit", wndProc);
    llvm::BasicBlock* noQuitBB = llvm::BasicBlock::Create(context, "no_quit", wndProc);
    llvm::Value* shouldQuit = builder.CreateICmpSLE(nextCount, builder.getInt32(0), "should_quit");
    builder.CreateCondBr(shouldQuit, quitBB, noQuitBB);

    builder.SetInsertPoint(quitBB);
    llvm::Function* postQuitMessageFunc = module->getFunction("PostQuitMessage");
    builder.CreateCall(postQuitMessageFunc, {builder.getInt32(0)});
    builder.CreateRet(builder.getInt64(0));

    builder.SetInsertPoint(noQuitBB);
    builder.CreateRet(builder.getInt64(0));

    builder.SetInsertPoint(defaultBB);
    builder.CreateRet(builder.CreateCall(defWindowProcFunc, {hwnd, msg, wparam, lparam}, "defproc"));

    return wndProc;
}

llvm::Function* LLVMCodegen::createWindowRuntime(WindowStatement* windowDecl, int windowId, llvm::Function* wndProc) {
    llvm::Type* ptrTy = builder.getInt8Ty()->getPointerTo();
    llvm::Type* i32Ty = builder.getInt32Ty();

    llvm::FunctionType* runtimeTy = llvm::FunctionType::get(builder.getVoidTy(), false);
    llvm::Function* mainFunc = llvm::Function::Create(
        runtimeTy,
        llvm::Function::LinkageTypes::InternalLinkage,
        "__xfawa_window_runtime_" + std::to_string(windowId),
        module);

    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", mainFunc);

    builder.SetInsertPoint(entryBB);

    llvm::StructType* wndClassTy = llvm::StructType::create(
        context,
        {i32Ty, ptrTy, i32Ty, i32Ty, ptrTy, ptrTy, ptrTy, ptrTy, ptrTy, ptrTy},
        "xfawa.wndclassa");

    llvm::AllocaInst* wndClass = builder.CreateAlloca(wndClassTy, nullptr, "wndclass");
    builder.CreateStore(llvm::Constant::getNullValue(wndClassTy), wndClass);

    llvm::Value* className = builder.CreateGlobalStringPtr("XfawaWindowClass" + std::to_string(windowId), "window_class_name_" + std::to_string(windowId));
    llvm::Value* title = builder.CreateGlobalStringPtr(windowDecl->title, "window_title_" + std::to_string(windowId));

    llvm::Function* getModuleHandleFunc = module->getFunction("GetModuleHandleA");
    llvm::Function* freeConsoleFunc = module->getFunction("FreeConsole");
    llvm::Function* loadCursorFunc = module->getFunction("LoadCursorA");
    llvm::Function* registerClassFunc = module->getFunction("RegisterClassA");
    llvm::Function* createWindowFunc = module->getFunction("CreateWindowExA");
    llvm::Function* createWindowWFunc = module->getFunction("CreateWindowExW");
    llvm::Function* multiByteToWideCharFunc = module->getFunction("MultiByteToWideChar");
    llvm::Function* createSolidBrushFunc = module->getFunction("CreateSolidBrush");
    llvm::Function* messageBoxFunc = module->getFunction("MessageBoxA");
    llvm::Function* showWindowFunc = module->getFunction("ShowWindow");
    llvm::Function* updateWindowFunc = module->getFunction("UpdateWindow");
    llvm::Value* nullPtr = llvm::Constant::getNullValue(ptrTy);
    if (windowId == 0) {
        builder.CreateCall(freeConsoleFunc, {});
    }
    llvm::Value* hInstance = builder.CreateCall(getModuleHandleFunc, {nullPtr}, "hinstance");
    llvm::Value* arrowCursorId = llvm::ConstantExpr::getIntToPtr(builder.getInt64(32512), llvm::cast<llvm::PointerType>(ptrTy));
    llvm::Value* cursor = builder.CreateCall(loadCursorFunc, {nullPtr, arrowCursorId}, "cursor");
    llvm::Value* backgroundBrush = builder.CreateCall(createSolidBrushFunc, {builder.getInt32(resolveWindowColor(windowDecl->color))}, "background_brush");

    builder.CreateStore(builder.getInt32(3), builder.CreateStructGEP(wndClassTy, wndClass, 0));
    builder.CreateStore(builder.CreateBitCast(wndProc, ptrTy), builder.CreateStructGEP(wndClassTy, wndClass, 1));
    builder.CreateStore(builder.getInt32(0), builder.CreateStructGEP(wndClassTy, wndClass, 2));
    builder.CreateStore(builder.getInt32(0), builder.CreateStructGEP(wndClassTy, wndClass, 3));
    builder.CreateStore(hInstance, builder.CreateStructGEP(wndClassTy, wndClass, 4));
    builder.CreateStore(nullPtr, builder.CreateStructGEP(wndClassTy, wndClass, 5));
    builder.CreateStore(cursor, builder.CreateStructGEP(wndClassTy, wndClass, 6));
    builder.CreateStore(backgroundBrush, builder.CreateStructGEP(wndClassTy, wndClass, 7));
    builder.CreateStore(nullPtr, builder.CreateStructGEP(wndClassTy, wndClass, 8));
    builder.CreateStore(className, builder.CreateStructGEP(wndClassTy, wndClass, 9));

    llvm::Value* registerResult = builder.CreateCall(registerClassFunc, {wndClass}, "register_result");

    // Convert UTF-8 title to wide char for CreateWindowExW
    llvm::Value* wtitlePtr = nullptr;
    llvm::Value* wclassNamePtr = nullptr;
    if (createWindowWFunc && multiByteToWideCharFunc) {
        // Convert title (UTF-8) to wide char
        llvm::AllocaInst* wtitleBuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wtitle_buffer");
        wtitlePtr = builder.CreateBitCast(wtitleBuffer, builder.getInt16Ty()->getPointerTo(), "wtitle_buffer_ptr");
        builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), title, builder.getInt32(-1), wtitlePtr, builder.getInt32(256)}, "title_to_wide");
        
        // className is ASCII, convert to wide char using MultiByteToWideChar
        llvm::AllocaInst* wclassNameBuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wclassname_buffer");
        wclassNamePtr = builder.CreateBitCast(wclassNameBuffer, builder.getInt16Ty()->getPointerTo(), "wclassname_buffer_ptr");
        builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), className, builder.getInt32(-1), wclassNamePtr, builder.getInt32(256)}, "classname_to_wide");
    }

    llvm::Value* hwnd = nullptr;
    if (createWindowWFunc && wtitlePtr && wclassNamePtr) {
        hwnd = builder.CreateCall(
            createWindowWFunc,
            {builder.getInt32(0), wclassNamePtr, wtitlePtr, builder.getInt32(0x10CF0000),
             builder.getInt32(-2147483648), builder.getInt32(-2147483648),
             builder.getInt32(windowDecl->width), builder.getInt32(windowDecl->height),
             nullPtr, nullPtr, hInstance, nullPtr},
            "hwnd");
    } else {
        hwnd = builder.CreateCall(
            createWindowFunc,
            {builder.getInt32(0), className, title, builder.getInt32(0x10CF0000),
             builder.getInt32(-2147483648), builder.getInt32(-2147483648),
             builder.getInt32(windowDecl->width), builder.getInt32(windowDecl->height),
             nullPtr, nullPtr, hInstance, nullPtr},
            "hwnd");
    }

    llvm::BasicBlock* creationOkBB = llvm::BasicBlock::Create(context, "window_create_ok", mainFunc);
    llvm::BasicBlock* creationFailBB = llvm::BasicBlock::Create(context, "window_create_fail", mainFunc);
    llvm::Value* registerOk = builder.CreateICmpNE(registerResult, builder.getInt16(0), "register_ok");
    llvm::Value* hwndOk = builder.CreateICmpNE(hwnd, nullPtr, "hwnd_ok");
    llvm::Value* windowReady = builder.CreateAnd(registerOk, hwndOk, "window_ready");
    builder.CreateCondBr(windowReady, creationOkBB, creationFailBB);

    builder.SetInsertPoint(creationFailBB);
    llvm::Value* errorTitle = builder.CreateGlobalStringPtr("xfawa window error", "window_error_title_" + std::to_string(windowId));
    llvm::Value* errorText = builder.CreateGlobalStringPtr("Failed to create xfawa window", "window_error_text_" + std::to_string(windowId));
    builder.CreateCall(messageBoxFunc, {nullPtr, errorText, errorTitle, builder.getInt32(0x10)});
    builder.CreateRetVoid();

    builder.SetInsertPoint(creationOkBB);
    builder.CreateCall(showWindowFunc, {hwnd, builder.getInt32(1)});
    builder.CreateCall(updateWindowFunc, {hwnd});

    for (size_t i = 0; i < windowDecl->buttons.size(); ++i) {
        const auto& button = windowDecl->buttons[i];
        llvm::Value* buttonClass = builder.CreateGlobalStringPtr("BUTTON", "button_class_name_" + std::to_string(windowId) + "_" + std::to_string(i));
        llvm::Value* buttonText = builder.CreateGlobalStringPtr(button->text, "button_text_" + std::to_string(windowId) + "_" + std::to_string(i));
        llvm::Value* buttonIdPtr = llvm::ConstantExpr::getIntToPtr(builder.getInt64(1000 + static_cast<int>(i)), llvm::cast<llvm::PointerType>(ptrTy));
        
        // Convert button text to wide char for CreateWindowExW
        if (createWindowWFunc && multiByteToWideCharFunc) {
            // Convert button text (UTF-8) to wide char
            llvm::AllocaInst* wbuttonTextBuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wbutton_text_buffer_" + std::to_string(i));
            llvm::Value* wbuttonTextPtr = builder.CreateBitCast(wbuttonTextBuffer, builder.getInt16Ty()->getPointerTo(), "wbutton_text_ptr_" + std::to_string(i));
            builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), buttonText, builder.getInt32(-1), wbuttonTextPtr, builder.getInt32(256)}, "button_text_to_wide_" + std::to_string(i));
            
            // buttonClass is ASCII "BUTTON", convert to wide char
            llvm::AllocaInst* wbuttonClassBuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wbutton_class_buffer_" + std::to_string(i));
            llvm::Value* wbuttonClassPtr = builder.CreateBitCast(wbuttonClassBuffer, builder.getInt16Ty()->getPointerTo(), "wbutton_class_ptr_" + std::to_string(i));
            builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), buttonClass, builder.getInt32(-1), wbuttonClassPtr, builder.getInt32(256)}, "button_class_to_wide_" + std::to_string(i));
            
            builder.CreateCall(
                createWindowWFunc,
                {builder.getInt32(0), wbuttonClassPtr, wbuttonTextPtr, builder.getInt32(0x50000000),
                 builder.getInt32(button->x), builder.getInt32(button->y),
                 builder.getInt32(button->width), builder.getInt32(button->height),
                 hwnd, buttonIdPtr, hInstance, nullPtr});
        } else {
            builder.CreateCall(
                createWindowFunc,
                {builder.getInt32(0), buttonClass, buttonText, builder.getInt32(0x50000000),
                 builder.getInt32(button->x), builder.getInt32(button->y),
                 builder.getInt32(button->width), builder.getInt32(button->height),
                 hwnd, buttonIdPtr, hInstance, nullPtr});
        }
    }

    for (size_t i = 0; i < windowDecl->texts.size(); ++i) {
        const auto& textItem = windowDecl->texts[i];
        llvm::Value* textClass = builder.CreateGlobalStringPtr("STATIC", "text_class_name_" + std::to_string(windowId) + "_" + std::to_string(i));
        llvm::Value* textValue = builder.CreateGlobalStringPtr(textItem->text, "text_value_" + std::to_string(windowId) + "_" + std::to_string(i));
        
        // Convert text to wide char for CreateWindowExW
        if (createWindowWFunc && multiByteToWideCharFunc) {
            // Convert text value (UTF-8) to wide char
            llvm::AllocaInst* wtextValueBuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wtext_value_buffer_" + std::to_string(i));
            llvm::Value* wtextValuePtr = builder.CreateBitCast(wtextValueBuffer, builder.getInt16Ty()->getPointerTo(), "wtext_value_ptr_" + std::to_string(i));
            builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), textValue, builder.getInt32(-1), wtextValuePtr, builder.getInt32(256)}, "text_value_to_wide_" + std::to_string(i));
            
            // textClass is ASCII "STATIC", convert to wide char
            llvm::AllocaInst* wtextClassBuffer = builder.CreateAlloca(builder.getInt16Ty(), builder.getInt32(256), "wtext_class_buffer_" + std::to_string(i));
            llvm::Value* wtextClassPtr = builder.CreateBitCast(wtextClassBuffer, builder.getInt16Ty()->getPointerTo(), "wtext_class_ptr_" + std::to_string(i));
            builder.CreateCall(multiByteToWideCharFunc, {builder.getInt32(65001), builder.getInt32(0), textClass, builder.getInt32(-1), wtextClassPtr, builder.getInt32(256)}, "text_class_to_wide_" + std::to_string(i));
            
            builder.CreateCall(
                createWindowWFunc,
                {builder.getInt32(0), wtextClassPtr, wtextValuePtr, builder.getInt32(0x50000000),
                 builder.getInt32(textItem->x), builder.getInt32(textItem->y),
                 builder.getInt32(textItem->width), builder.getInt32(textItem->height),
                 hwnd, nullPtr, hInstance, nullPtr});
        } else {
            builder.CreateCall(
                createWindowFunc,
                {builder.getInt32(0), textClass, textValue, builder.getInt32(0x50000000),
                 builder.getInt32(textItem->x), builder.getInt32(textItem->y),
                 builder.getInt32(textItem->width), builder.getInt32(textItem->height),
                 hwnd, nullPtr, hInstance, nullPtr});
        }
    }

    llvm::GlobalVariable* windowCount = getWindowCountGlobal();
    llvm::Value* currentCount = builder.CreateLoad(i32Ty, windowCount, "window_count");
    llvm::Value* nextCount = builder.CreateAdd(currentCount, builder.getInt32(1), "window_count_next");
    builder.CreateStore(nextCount, windowCount);
    builder.CreateRetVoid();

    return mainFunc;
}

llvm::Value* LLVMCodegen::codegen(WindowStatement* windowStmt) {
    if (!windowStmt) return nullptr;

    hasWindowStatements = true;
    int windowId = generatedWindowCount++;
    llvm::Type* ptrTy = builder.getInt8Ty()->getPointerTo();
    llvm::Function* createWindowFunc = module->getFunction("xr_create_window");
    llvm::Function* showWindowFunc = module->getFunction("xr_show_window");
    llvm::Function* loadStyleFunc = module->getFunction("xr_load_style");
    llvm::Function* pollEventsFunc = module->getFunction("xr_poll_events");
    llvm::Function* shouldCloseFunc = module->getFunction("xr_should_close");
    llvm::Function* beginFrameFunc = module->getFunction("xr_begin_frame");
    llvm::Function* endFrameFunc = module->getFunction("xr_end_frame");
    llvm::Function* drawButtonFunc = module->getFunction("xr_draw_button");
    llvm::Function* drawBoxFunc = module->getFunction("xr_draw_box");
    llvm::Function* drawTextFunc = module->getFunction("xr_draw_text");
    llvm::Function* drawRectFunc = module->getFunction("xr_draw_rect");
    llvm::Function* setClearColorFunc = module->getFunction("xr_set_clear_color");
    llvm::Function* currentFunction = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* loopCondBB = llvm::BasicBlock::Create(context, "xr_loop_cond_" + std::to_string(windowId), currentFunction);
    llvm::BasicBlock* loopBodyBB = llvm::BasicBlock::Create(context, "xr_loop_body_" + std::to_string(windowId), currentFunction);
    llvm::BasicBlock* loopEndBB = llvm::BasicBlock::Create(context, "xr_loop_end_" + std::to_string(windowId), currentFunction);

    for (const auto& input : windowStmt->inputs) {
        if (!input->varName.empty()) {
            std::string globalName = "__xfawa_input_" + input->varName;
            llvm::GlobalVariable* inputGlobal = new llvm::GlobalVariable(
                *module,
                llvm::PointerType::get(context, 0),
                false,
                llvm::GlobalValue::InternalLinkage,
                llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0)),
                globalName);
            windowInputGlobals[input->varName] = inputGlobal;
            windowInputTypes[input->varName] = VarType::STRING;
        }
    }

    std::vector<llvm::Function*> buttonHandlers;
    buttonHandlers.reserve(windowStmt->buttons.size());
    for (size_t i = 0; i < windowStmt->buttons.size(); ++i) {
        buttonHandlers.push_back(createButtonHandler(windowStmt->buttons[i].get(), windowId, 1000 + static_cast<int>(i), windowId));
    }

    llvm::Value* titlePtr = builder.CreateGlobalStringPtr(windowStmt->title, "xr_window_title_" + std::to_string(windowId));
    // Apply the xfawac.xfconf `xraphics_log` setting to the Xraphics runtime
    // before the window is created (controls [Backend]/[OpenCL] messages, the
    // per-frame profiler report, and debug log files).
    llvm::Function* setXrLogFunc = module->getFunction("xr_set_xraphics_log");
    if (setXrLogFunc) {
        builder.CreateCall(setXrLogFunc, {builder.getInt32(xraphicsLogEnabled ? 1 : 0)});
    }
    builder.CreateCall(createWindowFunc, {builder.getInt32(windowStmt->width), builder.getInt32(windowStmt->height), titlePtr});
    builder.CreateCall(setClearColorFunc, {builder.getInt32(resolveWindowColor(windowStmt->color))});

    if (!windowStmt->style.empty()) {
        llvm::Value* stylePtr = builder.CreateGlobalStringPtr(windowStmt->style, "xr_window_style_" + std::to_string(windowId));
        builder.CreateCall(loadStyleFunc, {stylePtr});
    }

    builder.CreateCall(showWindowFunc, {});

    // Emit camera creation (x3d.camera) once before the message loop.
    // If emitted inside the per-frame loop body, xr_camera_create would reset
    // the camera to its startup values every frame, discarding camera.move/look
    // deltas applied by the loop {} callback (camera "snaps back" bug).
    {
        int camIndex = 0;
        for (size_t ci = 0; ci < windowStmt->classes.size(); ++ci) {
            const auto& cls = windowStmt->classes[ci];
            for (size_t oi = 0; oi < cls->objects.size(); ++oi) {
                auto* obj = cls->objects[oi].get();
                if (obj->library == "x3d" && obj->preset == "camera") {
                    codegenXraphicsObject(obj, windowId, camIndex, true);
                }
                camIndex++;
            }
        }
    }

    // Register per-frame loop callbacks (api.txt loop {} blocks) once before the message loop
    for (size_t i = 0; i < windowStmt->loops.size(); ++i) {
        codegen(windowStmt->loops[i].get());
    }

    builder.CreateBr(loopCondBB);

    builder.SetInsertPoint(loopCondBB);
    llvm::Value* shouldClose = builder.CreateCall(shouldCloseFunc, {}, "xr_should_close");
    llvm::Value* continueLoop = builder.CreateICmpEQ(shouldClose, builder.getInt32(0), "xr_continue_loop");
    builder.CreateCondBr(continueLoop, loopBodyBB, loopEndBB);

    builder.SetInsertPoint(loopBodyBB);
    builder.CreateCall(pollEventsFunc, {});
    builder.CreateCall(beginFrameFunc, {});

    for (size_t i = 0; i < windowStmt->texts.size(); ++i) {
        const auto& textItem = windowStmt->texts[i];
        llvm::Value* textPtr = builder.CreateGlobalStringPtr(
            textItem->text,
            "xr_window_text_" + std::to_string(windowId) + "_" + std::to_string(i));
        builder.CreateCall(
            drawTextFunc,
            {
                builder.getInt32(textItem->x),
                builder.getInt32(textItem->y),
                textPtr,
                builder.getInt32(0x00000000)
            });
    }

    for (size_t i = 0; i < windowStmt->boxes.size(); ++i) {
        const auto& box = windowStmt->boxes[i];
        llvm::Value* boxIdPtr = builder.CreateGlobalStringPtr(
            box->id,
            "xr_window_box_id_" + std::to_string(windowId) + "_" + std::to_string(i));
        llvm::Value* boxTextPtr = builder.CreateGlobalStringPtr(
            box->text,
            "xr_window_box_text_" + std::to_string(windowId) + "_" + std::to_string(i));
        builder.CreateCall(
            drawBoxFunc,
            {
                builder.getInt32(box->x),
                builder.getInt32(box->y),
                builder.getInt32(box->width),
                builder.getInt32(box->height),
                boxIdPtr,
                boxTextPtr
            });
    }

    for (size_t i = 0; i < windowStmt->buttons.size(); ++i) {
        const auto& button = windowStmt->buttons[i];
        llvm::Value* buttonText = builder.CreateGlobalStringPtr(
            button->text,
            "xr_window_button_" + std::to_string(windowId) + "_" + std::to_string(i));
        llvm::Value* handlerPtr = builder.CreateBitCast(buttonHandlers[i], ptrTy);
        builder.CreateCall(
            drawButtonFunc,
            {
                builder.getInt32(button->x),
                builder.getInt32(button->y),
                builder.getInt32(button->width),
                builder.getInt32(button->height),
                buttonText,
                handlerPtr
            });
    }

    llvm::Function* drawInputFunc = module->getFunction("xr_draw_input");
    for (size_t i = 0; i < windowStmt->inputs.size(); ++i) {
        const auto& input = windowStmt->inputs[i];
        llvm::Value* inputIdPtr = builder.CreateGlobalStringPtr(
            input->id,
            "xr_window_input_id_" + std::to_string(windowId) + "_" + std::to_string(i));
        llvm::Value* inputVarPtr = builder.CreateGlobalStringPtr(
            input->varName,
            "xr_window_input_var_" + std::to_string(windowId) + "_" + std::to_string(i));
        llvm::Value* inputTextPtr = builder.CreateCall(
            drawInputFunc,
            {
                builder.getInt32(input->x),
                builder.getInt32(input->y),
                builder.getInt32(input->width),
                builder.getInt32(input->height),
                inputIdPtr,
                inputVarPtr
            });
        if (!input->varName.empty()) {
            auto globalIt = windowInputGlobals.find(input->varName);
            if (globalIt != windowInputGlobals.end()) {
                builder.CreateStore(inputTextPtr, globalIt->second);
            }
            llvm::AllocaInst* inputStorage = builder.CreateAlloca(
                llvm::PointerType::get(context, 0),
                nullptr,
                "input_storage_" + input->varName);
            builder.CreateStore(inputTextPtr, inputStorage);
            locals[input->varName] = inputStorage;
            localTypes[input->varName] = VarType::STRING;
        }
    }

    // Generate class block rendering (Xraphics objects: x3d.* / x2d.*)
    // x3d objects use Canvas3D engine for real 3D; x2d objects use 2D primitives
    {
        int objIndex = 0;
        for (size_t ci = 0; ci < windowStmt->classes.size(); ++ci) {
            const auto& cls = windowStmt->classes[ci];
            for (size_t oi = 0; oi < cls->objects.size(); ++oi) {
                codegenXraphicsObject(cls->objects[oi].get(), windowId, objIndex, false);
                objIndex++;
            }
        }
    }

    builder.CreateCall(endFrameFunc, {});
    builder.CreateBr(loopCondBB);

    builder.SetInsertPoint(loopEndBB);
    
    for (const auto& input : windowStmt->inputs) {
        if (!input->varName.empty()) {
            windowInputGlobals.erase(input->varName);
            windowInputTypes.erase(input->varName);
        }
    }
    
    return builder.getInt32(0);
}

llvm::Value* LLVMCodegen::codegen(ButtonStatement* stmt) {
    addError("button blocks can only appear inside window blocks");
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(TextStatement* stmt) {
    addError("text blocks can only appear inside window blocks");
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(BoxStatement* stmt) {
    addError("box blocks can only appear inside window blocks");
    return nullptr;
}

llvm::Value* LLVMCodegen::codegen(InputStatement* stmt) {
    addError("input blocks can only appear inside window blocks");
    return nullptr;
}

// Helper: evaluate an Expression as an integer (for position/size/radius/height params)
int LLVMCodegen::evalIntExpr(Expression* expr, int defaultValue) {
    if (!expr) return defaultValue;
    if (auto* num = dynamic_cast<NumberLiteral*>(expr)) {
        return static_cast<int>(num->value);
    }
    if (auto* flt = dynamic_cast<FloatLiteral*>(expr)) {
        return static_cast<int>(flt->value);
    }
    if (auto* un = dynamic_cast<UnaryOp*>(expr)) {
        if (un->op == UnaryOpType::NEGATE) {
            return -evalIntExpr(un->expr.get(), defaultValue);
        }
    }
    return defaultValue;
}

// Helper: evaluate a ColorLiteral expression as 0xRRGGBB unsigned int
unsigned int LLVMCodegen::evalColorExpr(Expression* expr, unsigned int defaultValue) {
    if (!expr) return defaultValue;
    if (auto* color = dynamic_cast<ColorLiteral*>(expr)) {
        const std::string& hex = color->value;
        if (hex.size() == 6) {
            unsigned int r = 0, g = 0, b = 0;
            if (std::sscanf(hex.c_str(), "%02x%02x%02x", &r, &g, &b) == 3) {
                return (r << 16) | (g << 8) | b;
            }
        } else if (hex.size() == 3) {
            // Expand #RGB to #RRGGBB
            std::string expanded;
            expanded += hex[0]; expanded += hex[0];
            expanded += hex[1]; expanded += hex[1];
            expanded += hex[2]; expanded += hex[2];
            unsigned int r = 0, g = 0, b = 0;
            if (std::sscanf(expanded.c_str(), "%02x%02x%02x", &r, &g, &b) == 3) {
                return (r << 16) | (g << 8) | b;
            }
        }
    }
    return defaultValue;
}

// Helper: evaluate an expression as a float (for rotation angles, etc.)
float LLVMCodegen::evalFloatExpr(Expression* expr, float defaultValue) {
    if (!expr) return defaultValue;
    if (auto* num = dynamic_cast<NumberLiteral*>(expr)) {
        return static_cast<float>(num->value);
    }
    if (auto* flt = dynamic_cast<FloatLiteral*>(expr)) {
        return static_cast<float>(flt->value);
    }
    if (auto* un = dynamic_cast<UnaryOp*>(expr)) {
        if (un->op == UnaryOpType::NEGATE) {
            return -evalFloatExpr(un->expr.get(), defaultValue);
        }
    }
    return defaultValue;
}

// Render a single Xraphics object:
// - x3d.* objects: invoke Canvas3D SDF ray marching engine via xr_draw_canvas3d
//   Each preset maps to its own scene type (box=2, sphere=3, plane=4, cylinder=5,
//   capsule=6, cone=7, torus=8, mesh=9). Color is passed through to the renderer.
// - x2d.* objects: render as 2D primitives via xr_draw_rect / xr_draw_text
void LLVMCodegen::codegenXraphicsObject(XraphicsObjectStatement* obj, int windowId, int index, bool emitCamera) {
    if (!obj) return;

    // Parse params with defaults
    int posX = 10 + (index % 8) * 70;
    int posY = 10 + (index / 8) * 70;
    int width = 50;
    int height = 50;
    unsigned int color = 0xAAAAAA;  // default gray
    int radius = 0;

    for (const auto& param : obj->params) {
        if (param.name == "position" && param.value) {
            if (auto* tuple = dynamic_cast<TupleExpression*>(param.value.get())) {
                if (tuple->elements.size() >= 2) {
                    posX = evalIntExpr(tuple->elements[0].get(), posX);
                    posY = evalIntExpr(tuple->elements[1].get(), posY);
                }
            }
        } else if (param.name == "size" && param.value) {
            if (auto* tuple = dynamic_cast<TupleExpression*>(param.value.get())) {
                if (tuple->elements.size() >= 2) {
                    width = evalIntExpr(tuple->elements[0].get(), width);
                    height = evalIntExpr(tuple->elements[1].get(), height);
                }
            }
        } else if (param.name == "color" && param.value) {
            color = evalColorExpr(param.value.get(), color);
        } else if (param.name == "radius" && param.value) {
            radius = evalIntExpr(param.value.get(), 0);
        }
    }

        // 3D objects (x3d.*): render using multi-object scene engine
        if (obj->library == "x3d") {
            // Special handling for camera preset: creates the active camera (api.txt)
            // Emitted once before the message loop (emitCamera=true); skipped in the
            // per-frame loop body so per-frame camera deltas are not reset.
            if (obj->preset == "camera") {
                if (!emitCamera) {
                    return;  // already created during window setup
                }
                float camX = 0.0f, camY = 1.6f, camZ = 5.0f;
                float camYaw = 0.0f, camPitch = 0.0f, camFov = 75.0f;
                for (const auto& param : obj->params) {
                    if (param.name == "position" && param.value) {
                        if (auto* tuple = dynamic_cast<TupleExpression*>(param.value.get())) {
                            if (tuple->elements.size() >= 3) {
                                camX = evalFloatExpr(tuple->elements[0].get(), camX);
                                camY = evalFloatExpr(tuple->elements[1].get(), camY);
                                camZ = evalFloatExpr(tuple->elements[2].get(), camZ);
                            }
                        }
                    } else if (param.name == "yaw" && param.value) {
                        camYaw = evalFloatExpr(param.value.get(), camYaw);
                    } else if (param.name == "pitch" && param.value) {
                        camPitch = evalFloatExpr(param.value.get(), camPitch);
                    } else if (param.name == "fov" && param.value) {
                        camFov = evalFloatExpr(param.value.get(), camFov);
                    }
                }
                llvm::Function* camCreateFunc = module->getFunction("xr_camera_create");
                if (camCreateFunc) {
                    builder.CreateCall(camCreateFunc, {
                        llvm::ConstantFP::get(builder.getFloatTy(), camX),
                        llvm::ConstantFP::get(builder.getFloatTy(), camY),
                        llvm::ConstantFP::get(builder.getFloatTy(), camZ),
                        llvm::ConstantFP::get(builder.getFloatTy(), camYaw),
                        llvm::ConstantFP::get(builder.getFloatTy(), camPitch),
                        llvm::ConstantFP::get(builder.getFloatTy(), camFov)
                    });
                }
                return;  // camera is not a renderable object
            }

            // Parse 3D world position as floats
            float worldX = 0.0f, worldY = 0.0f, worldZ = 0.0f;
            float sizeX = 1.0f, sizeY = 1.0f, sizeZ = 1.0f;
            for (const auto& param : obj->params) {
                if (param.name == "position" && param.value) {
                    if (auto* tuple = dynamic_cast<TupleExpression*>(param.value.get())) {
                        if (tuple->elements.size() >= 3) {
                            worldX = evalFloatExpr(tuple->elements[0].get(), 0.0f);
                            worldY = evalFloatExpr(tuple->elements[1].get(), 0.0f);
                            worldZ = evalFloatExpr(tuple->elements[2].get(), 0.0f);
                        } else if (tuple->elements.size() >= 2) {
                            worldX = evalFloatExpr(tuple->elements[0].get(), 0.0f);
                            worldY = evalFloatExpr(tuple->elements[1].get(), 0.0f);
                        }
                    }
                } else if (param.name == "size" && param.value) {
                    if (auto* tuple = dynamic_cast<TupleExpression*>(param.value.get())) {
                        if (tuple->elements.size() >= 3) {
                            sizeX = evalFloatExpr(tuple->elements[0].get(), 1.0f);
                            sizeY = evalFloatExpr(tuple->elements[1].get(), 1.0f);
                            sizeZ = evalFloatExpr(tuple->elements[2].get(), 1.0f);
                        } else if (tuple->elements.size() >= 2) {
                            sizeX = evalFloatExpr(tuple->elements[0].get(), 1.0f);
                            sizeY = evalFloatExpr(tuple->elements[1].get(), 1.0f);
                            sizeZ = 1.0f;
                        }
                    }
                } else if (param.name == "color" && param.value) {
                    color = evalColorExpr(param.value.get(), color);
                }
            }

            // Map preset to scene type
            int sceneType = 3;
            if (obj->preset == "box") sceneType = 2;
            else if (obj->preset == "sphere") sceneType = 3;
            else if (obj->preset == "plane") sceneType = 4;
            else if (obj->preset == "cylinder") sceneType = 5;
            else if (obj->preset == "capsule") sceneType = 6;
            else if (obj->preset == "cone") sceneType = 7;
            else if (obj->preset == "torus") sceneType = 8;
            else if (obj->preset == "mesh") sceneType = 9;

            float rotationDegrees = 0.0f;
            for (const auto& param : obj->params) {
                if (param.name == "rotation" && param.value) {
                    rotationDegrees = evalFloatExpr(param.value.get(), 0.0f);
                }
            }
            float rotationRadians = rotationDegrees * 3.14159265f / 180.0f;

            // Call xr_add_scene_object(sceneType, px, py, pz, sx, sy, sz, color, rotation)
            llvm::Function* addSceneObjFunc = module->getFunction("xr_add_scene_object");
            if (addSceneObjFunc) {
                builder.CreateCall(addSceneObjFunc, {
                    builder.getInt32(sceneType),
                    llvm::ConstantFP::get(builder.getFloatTy(), worldX),
                    llvm::ConstantFP::get(builder.getFloatTy(), worldY),
                    llvm::ConstantFP::get(builder.getFloatTy(), worldZ),
                    llvm::ConstantFP::get(builder.getFloatTy(), sizeX),
                    llvm::ConstantFP::get(builder.getFloatTy(), sizeY),
                    llvm::ConstantFP::get(builder.getFloatTy(), sizeZ),
                    builder.getInt32(color),
                    llvm::ConstantFP::get(builder.getFloatTy(), rotationRadians)
                });
            }
            return;
        }

    // x2d special cases
    if (obj->library == "x2d") {
        if (obj->preset == "line") {
            height = 2;  // thin line
        } else if (obj->preset == "circle" || obj->preset == "ellipse") {
            if (radius > 0) {
                width = radius * 2;
                height = radius * 2;
            }
        } else if (obj->preset == "text") {
            // Render text label using the object name
            llvm::Function* drawTextFunc = module->getFunction("xr_draw_text");
            if (drawTextFunc) {
                std::string textContent = obj->objectName;
                llvm::Value* textPtr = builder.CreateGlobalStringPtr(
                    textContent,
                    "xr_xraphics_text_" + std::to_string(windowId) + "_" + std::to_string(index));
                builder.CreateCall(drawTextFunc, {
                    builder.getInt32(posX),
                    builder.getInt32(posY),
                    textPtr,
                    builder.getInt32(color)
                });
                return;
            }
        }
    }

    // Default: render as a colored rectangle
    llvm::Function* drawRectFunc = module->getFunction("xr_draw_rect");
    if (drawRectFunc) {
        builder.CreateCall(drawRectFunc, {
            builder.getInt32(posX),
            builder.getInt32(posY),
            builder.getInt32(width),
            builder.getInt32(height),
            builder.getInt32(color)
        });
    }

    // Also render a text label showing the preset name for debugging
    llvm::Function* drawTextFunc = module->getFunction("xr_draw_text");
    if (drawTextFunc) {
        std::string label = obj->library + "." + obj->preset;
        llvm::Value* labelPtr = builder.CreateGlobalStringPtr(
            label,
            "xr_xraphics_label_" + std::to_string(windowId) + "_" + std::to_string(index));
        builder.CreateCall(drawTextFunc, {
            builder.getInt32(posX),
            builder.getInt32(posY + height + 2),
            labelPtr,
            builder.getInt32(0x000000)  // black label
        });
    }
}

llvm::Value* LLVMCodegen::codegen(Expression* expr) {
    if (auto* e = dynamic_cast<NumberLiteral*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<FloatLiteral*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<BooleanLiteral*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<StringLiteral*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<VariableExpression*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<OLiteralExpression*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<ParadoxExpression*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<GhostExpression*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<UnaryOp*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<BinaryOp*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<CallExpression*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<ArrayRangeExpression*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<ArrayLiteral*>(expr)) return codegen(e);
    if (auto* e = dynamic_cast<ArrayIndexExpression*>(expr)) return codegen(e);
    return nullptr;
}

bool LLVMCodegen::emitObjectFile(const std::string& filename) {
    return emitObjectFile(filename, false, false, "", "");
}

bool LLVMCodegen::emitObjectFile(const std::string& filename, bool keepLL, bool emitAsm, 
                                  const std::string& llOutputPath, const std::string& asmOutputPath) {
    runOptimizations();
    
    if (module->getTargetTriple().empty()) {
        module->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
    }
    
    std::string baseFile = filename;
    if (baseFile.length() > 2 && baseFile.substr(baseFile.length() - 2) == ".o") {
        baseFile = baseFile.substr(0, baseFile.length() - 2);
    }
    
    std::string llFile = llOutputPath.empty() ? (baseFile + ".ll") : llOutputPath;
    std::string objFile = filename;
    std::string asmFile = asmOutputPath.empty() ? (baseFile + ".asm") : asmOutputPath;

    std::error_code EC;
    llvm::raw_fd_ostream llOut(llFile, EC);
    if (EC) {
        addError("Could not open file: " + EC.message());
        return false;
    }
    module->print(llOut, nullptr);
    llOut.close();

    auto targetMachine = createTargetMachine(*module, optLevel, errors);
    if (!targetMachine) {
        if (!keepLL) {
            std::remove(llFile.c_str());
        }
        return false;
    }

    if (emitAsm) {
        if (!emitMachineCode(*module, *targetMachine, asmFile, llvm::CodeGenFileType::AssemblyFile, errors)) {
            addError("ASM file generation failed");
            return false;
        }

        if (!emitMachineCode(*module, *targetMachine, objFile, llvm::CodeGenFileType::ObjectFile, errors)) {
            addError("Object file generation from ASM failed");
            return false;
        }
        
        if (!keepLL) {
            std::remove(llFile.c_str());
        }
        
        return true;
    }

    if (!emitMachineCode(*module, *targetMachine, objFile, llvm::CodeGenFileType::ObjectFile, errors)) {
        addError("Object file generation failed");
        return false;
    }
    
    if (!keepLL) {
        std::remove(llFile.c_str());
    }
    
    return true;
}

bool LLVMCodegen::linkExecutable(const std::string& objFile, const std::string& outFile) {
    auto vcToolsDir = getEnvPath("VCToolsInstallDir");
    auto universalCrtDir = getEnvPath("UniversalCRTSdkDir");
    auto windowsSdkDir = getEnvPath("WindowsSdkDir");

    const char* ucrtVersionRaw = std::getenv("UCRTVersion");
    const char* windowsSdkVersionRaw = std::getenv("WindowsSDKLibVersion");

    std::vector<std::string> argStorage;
    argStorage.reserve(24);
    argStorage.push_back("lld-link");
    argStorage.push_back("/nologo");
    argStorage.push_back("/machine:x64");
    argStorage.push_back(hasWindowStatements ? "/subsystem:windows" : "/subsystem:console");
    argStorage.push_back("/entry:mainCRTStartup");
    argStorage.push_back("/out:" + outFile);
    argStorage.push_back(objFile);

    // If the program uses `bsod`, link the runtime helper that draws the overlay.
    if (hasBsod) {
        if (auto runtimeLib = getCompilerAdjacentRuntimeLib()) {
            argStorage.push_back(runtimeLib->string());
        } else {
            addError("xfawa runtime helper (bsod overlay) missing. Please reinstall xfawa.");
            return false;
        }
    }

    // If the user is already in a VS developer environment, use those paths.
    // Otherwise let embedded LLD auto-detect the MSVC and Windows SDK layout.
    if (vcToolsDir && universalCrtDir && windowsSdkDir && ucrtVersionRaw && windowsSdkVersionRaw) {
        std::filesystem::path vcLibDir = *vcToolsDir / "lib" / "x64";
        std::filesystem::path ucrtLibDir = *universalCrtDir / "Lib" / ucrtVersionRaw / "ucrt" / "x64";
        std::filesystem::path umLibDir = *windowsSdkDir / "Lib" / windowsSdkVersionRaw / "um" / "x64";

        if (!appendLibPath(argStorage, vcLibDir) ||
            !appendLibPath(argStorage, ucrtLibDir) ||
            !appendLibPath(argStorage, umLibDir)) {
            addError("Unable to locate required MSVC or Windows SDK library directories from the current environment");
            return false;
        }
    }

    argStorage.push_back("/defaultlib:libcmt");
    argStorage.push_back("/defaultlib:libvcruntime");
    argStorage.push_back("/defaultlib:libucrt");
    argStorage.push_back("/defaultlib:legacy_stdio_definitions");
    if (hasWindowStatements) {
        if (auto xraphicsLib = getCompilerAdjacentXraphicsLib()) {
            argStorage.push_back(xraphicsLib->string());
        } else {
            addError("Xraphics component missing. Please reinstall xfawa.");
            return false;
        }
        // OpenCL import library for GPU compute backend
        std::filesystem::path openclLib = "D:\\opencl\\lib\\OpenCL.lib";
        if (std::filesystem::exists(openclLib)) {
            argStorage.push_back(openclLib.string());
        }
    }
    argStorage.push_back("/defaultlib:kernel32");
    argStorage.push_back("/defaultlib:user32");
    argStorage.push_back("/defaultlib:gdi32");
    argStorage.push_back("/defaultlib:advapi32");
    argStorage.push_back("/defaultlib:opengl32");

    std::vector<const char*> args;
    args.reserve(argStorage.size());
    for (const auto& arg : argStorage) {
        args.push_back(arg.c_str());
    }

    std::string lldStdout;
    std::string lldStderr;
    llvm::raw_string_ostream stdoutStream(lldStdout);
    llvm::raw_string_ostream stderrStream(lldStderr);
    bool success = lld::coff::link(args, stdoutStream, stderrStream, false, false);
    stdoutStream.flush();
    stderrStream.flush();

    if (!success) {
        if (!lldStderr.empty()) {
            addError("LLD linking failed: " + lldStderr);
        } else if (!lldStdout.empty()) {
            addError("LLD linking failed: " + lldStdout);
        } else {
            addError("LLD linking failed with an unknown error");
        }
        return false;
    }

    return true;
}

bool LLVMCodegen::verifyModule() {
    std::string out;
    llvm::raw_string_ostream oss(out);
    if (llvm::verifyModule(*module, &oss)) {
        addError(oss.str());
        return false;
    }
    return true;
}

void LLVMCodegen::runOptimizations() {
    switch (optLevel) {
        case OptimizationLevel::O0:
            runO0Optimizations();
            break;
        case OptimizationLevel::O1:
            runO1Optimizations();
            break;
        case OptimizationLevel::O2:
            runO2Optimizations();
            break;
        case OptimizationLevel::O3:
            runO3Optimizations();
            break;
    }
}

void LLVMCodegen::runO0Optimizations() {
}

void LLVMCodegen::runO1Optimizations() {
    llvm::PassBuilder passBuilder;
    llvm::LoopAnalysisManager loopAnalysisManager;
    llvm::FunctionAnalysisManager functionAnalysisManager;
    llvm::CGSCCAnalysisManager cgsccAnalysisManager;
    llvm::ModuleAnalysisManager moduleAnalysisManager;
    
    passBuilder.registerModuleAnalyses(moduleAnalysisManager);
    passBuilder.registerCGSCCAnalyses(cgsccAnalysisManager);
    passBuilder.registerFunctionAnalyses(functionAnalysisManager);
    passBuilder.registerLoopAnalyses(loopAnalysisManager);
    passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager, cgsccAnalysisManager, moduleAnalysisManager);
    
    llvm::OptimizationLevel level = llvm::OptimizationLevel::O1;
    llvm::ModulePassManager modulePassManager = passBuilder.buildPerModuleDefaultPipeline(level);
    modulePassManager.run(*module, moduleAnalysisManager);
}

void LLVMCodegen::runO2Optimizations() {
    llvm::PassBuilder passBuilder;
    llvm::LoopAnalysisManager loopAnalysisManager;
    llvm::FunctionAnalysisManager functionAnalysisManager;
    llvm::CGSCCAnalysisManager cgsccAnalysisManager;
    llvm::ModuleAnalysisManager moduleAnalysisManager;
    
    passBuilder.registerModuleAnalyses(moduleAnalysisManager);
    passBuilder.registerCGSCCAnalyses(cgsccAnalysisManager);
    passBuilder.registerFunctionAnalyses(functionAnalysisManager);
    passBuilder.registerLoopAnalyses(loopAnalysisManager);
    passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager, cgsccAnalysisManager, moduleAnalysisManager);
    
    llvm::OptimizationLevel level = llvm::OptimizationLevel::O2;
    llvm::ModulePassManager modulePassManager = passBuilder.buildPerModuleDefaultPipeline(level);
    modulePassManager.run(*module, moduleAnalysisManager);
}

void LLVMCodegen::runO3Optimizations() {
    llvm::PassBuilder passBuilder;
    llvm::LoopAnalysisManager loopAnalysisManager;
    llvm::FunctionAnalysisManager functionAnalysisManager;
    llvm::CGSCCAnalysisManager cgsccAnalysisManager;
    llvm::ModuleAnalysisManager moduleAnalysisManager;
    
    passBuilder.registerModuleAnalyses(moduleAnalysisManager);
    passBuilder.registerCGSCCAnalyses(cgsccAnalysisManager);
    passBuilder.registerFunctionAnalyses(functionAnalysisManager);
    passBuilder.registerLoopAnalyses(loopAnalysisManager);
    passBuilder.crossRegisterProxies(loopAnalysisManager, functionAnalysisManager, cgsccAnalysisManager, moduleAnalysisManager);
    
    llvm::OptimizationLevel level = llvm::OptimizationLevel::O3;
    llvm::ModulePassManager modulePassManager = passBuilder.buildPerModuleDefaultPipeline(level);
    modulePassManager.run(*module, moduleAnalysisManager);
}

}
