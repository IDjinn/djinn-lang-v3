//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

#include <cassert>
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Transforms/Coroutines/CoroEarly.h"
#include "llvm/Transforms/Coroutines/CoroSplit.h"
#include "llvm/Transforms/Coroutines/CoroElide.h"
#include "llvm/Transforms/Coroutines/CoroCleanup.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Bitcode/BitcodeReader.h"

#include "../binder/SymbolTable.h"
#include "../utils/Logger.h"

Generator::Generator(DiagnosticEngine& diagnostics, const std::shared_ptr<ScopedSymbolTable>& symbols)
    : _diagnostics(diagnostics),
      symbols(symbols),
      context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("djinn_module", *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)),
      currentScope(std::make_shared<GeneratorScope>())
{
}

void Generator::push_scope()
{
    currentScope = std::make_shared<GeneratorScope>(currentScope);
}

void Generator::pop_scope()
{
    if (currentScope->parent)
    {
        currentScope = currentScope->parent;
    }
}

void Generator::register_intrinsic_constants()
{
    auto def = [this](const std::string& structName, const std::string& field, ConstValue val)
    {
        std::string qualified = "std::sys::" + structName + "." + field;
        std::string shortName = structName + "." + field;
        constEvaluator.defineConstant(qualified, val);
        constEvaluator.defineConstant(shortName, val);
        LOG_DEBUG("[generator] intrinsic: '%s' (alias '%s')", qualified.c_str(), shortName.c_str());
    };
    auto defBool = [&def](const std::string& s, const std::string& f, bool val)
    {
        def(s, f, ConstValue::makeInt(val ? 1 : 0, 1, false));
    };
    auto defInt = [&def](const std::string& s, const std::string& f, int64_t val)
    {
        def(s, f, ConstValue::makeInt(val));
    };

#ifdef _WIN32
    defBool("Platform", "Windows", true);
    defBool("Platform", "Linux", false);
    defBool("Platform", "MacOs", false);
#elif __linux__
    defBool("Platform", "Windows", false); defBool("Platform", "Linux", true); defBool("Platform", "MacOs", false);
#elif __APPLE__
    defBool("Platform", "Windows", false); defBool("Platform", "Linux", false); defBool("Platform", "MacOs", true);
#else
    defBool("Platform", "Windows", false); defBool("Platform", "Linux", false); defBool("Platform", "MacOs", false);
#endif

#if defined(__x86_64__) || defined(_M_X64)
    defBool("Arch", "X64", true);
    defBool("Arch", "X86", false);
    defBool("Arch", "Arm64", false);
#elif defined(__i386__) || defined(_M_IX86)
    defBool("Arch", "X64", false); defBool("Arch", "X86", true); defBool("Arch", "Arm64", false);
#elif defined(__aarch64__) || defined(_M_ARM64)
    defBool("Arch", "X64", false); defBool("Arch", "X86", false); defBool("Arch", "Arm64", true);
#else
    defBool("Arch", "X64", false); defBool("Arch", "X86", false); defBool("Arch", "Arm64", false);
#endif

#ifdef NDEBUG
    defBool("Build", "Debug", false); defBool("Build", "Release", true);
#else
    defBool("Build", "Debug", true);
    defBool("Build", "Release", false);
#endif

    defInt("Runtime", "PointerSize", sizeof(void*));

    LOG_DEBUG("[generator] intrinsic constants registered");
}

void Generator::generate()
{
    if (!moduleName.empty())
    {
        module->setModuleIdentifier(moduleName);
        module->setSourceFileName(moduleName);
    }

    // PASS 0: Register short-name aliases for namespaced symbols
    LOG_DEBUG("[generator] PASS 0: registering aliases from %zu symbols", symbols->symbols().size());
    for (const auto& [name, sym] : symbols->symbols())
    {
        if (const auto pos = name.rfind("::"); pos != std::string::npos)
        {
            const std::string shortName = name.substr(pos + 2);
            LOG_DEBUG("[generator]   alias: '%s' -> '%s'", shortName.c_str(), name.c_str());
            currentScope->define_alias(shortName, name);
        }
    }

    // PASS 0a: Define intrinsic constants (compiler-provided values)
    register_intrinsic_constants();

    // PASS 0b: Evaluate constexpr declarations and register in the compile-time evaluator
    for (const auto& [name, entry] : symbols->constExprConstants)
    {
        if (!entry.value)
            continue; // intrinsic — already defined in PASS 0a

        ConstValue val = constEvaluator.evaluate(*entry.value);
        if (!val.isError())
        {
            constEvaluator.defineConstant(name, val);
            if (const auto pos = name.rfind("::"); pos != std::string::npos)
            {
                constEvaluator.defineConstant(name.substr(pos + 2), val);
            }
            LOG_DEBUG("[generator] constexpr '%s' evaluated to int=%lld", name.c_str(), val.intVal);
        }
        else
        {
            GENERATOR_ERROR(DiagnosticCode::TYPE_MISMATCH,
                            "constexpr '" + name + "' could not be evaluated at compile time",
                            entry.value->location);
        }
    }

    // PASS 0c: Generate global static variables
    for (const auto& [name, entry] : symbols->staticVars)
    {
        if (name.find("::") != std::string::npos && name != name.substr(name.rfind("::") + 2))
            continue;

        llvm::Type* llvmType = generate_type(entry.type);
        llvm::Constant* initVal = nullptr;

        if (entry.initializer)
        {
            ConstValue cv = constEvaluator.evaluate(*entry.initializer);
            if (!cv.isError())
            {
                if (llvmType->isIntegerTy())
                    initVal = llvm::ConstantInt::get(llvmType, cv.intVal, true);
                else if (llvmType->isFloatingPointTy())
                    initVal = llvm::ConstantFP::get(llvmType, cv.floatVal);
            }
        }

        if (!initVal)
            initVal = llvm::Constant::getNullValue(llvmType);

        auto* gv = new llvm::GlobalVariable(
            *module,
            llvmType,
            !entry.isMutable,
            llvm::GlobalValue::InternalLinkage,
            initVal,
            name
        );

        LOG_DEBUG("[generator] static var '%s' (mut=%d)", name.c_str(), entry.isMutable);
    }

    // PASS 1: Forward declare all structs (create opaque types)
    // Library structs are included so the generator scope knows the types
    const auto allStructs = symbols->get_all_structs();
    LOG_DEBUG("[generator] PASS 1: forward declaring %zu structs", allStructs.size());
    for (const auto& sym : allStructs)
    {
        LOG_DEBUG("[generator]   forward declare struct: '%s'%s", sym->name.c_str(),
                  sym->isFromLibrary ? " [lib]" : "");
        forward_declare_struct(*std::dynamic_pointer_cast<StructSymbol>(sym));
        generatedStructs++;
    }

    // PASS 2: Generate all enums (generic ones are just registered for monomorphization)
    for (const auto& sym : symbols->get_all_enums())
    {
        generate_enum(*std::dynamic_pointer_cast<EnumSymbol>(sym));
    }

    // PASS 3: Generate extern function declarations
    // Library externs included — they are just declarations, linker resolves them
    for (const auto& sym : symbols->get_all_extern_functions())
    {
        generate_extern_function(*std::dynamic_pointer_cast<ExternFunctionSymbol>(sym));
        generatedExternFunctions++;
    }

    // PASS 4: Resolve struct bodies (fill in field types)
    // Library structs are included so the generator can access fields
    for (const auto& sym : symbols->get_all_structs())
    {
        resolve_struct_body(*std::dynamic_pointer_cast<StructSymbol>(sym));
    }

    // PASS 5a: Forward-declare all struct methods (so methods can reference each other)
    // Library structs included — declarations needed so user code can call them
    for (const auto& sym : symbols->get_all_structs())
    {
        const auto& structSym = *std::dynamic_pointer_cast<StructSymbol>(sym);
        if (!is_primitive_impl(structSym))
        {
            forward_declare_struct_methods(structSym);
        }
    }

    // In std-decl mode, skip all body generation — only declarations and struct layouts
    if (stdDeclOnly)
    {
        // Forward-declare global functions too (no bodies)
        for (const auto& sym : symbols->get_all_functions())
        {
            auto fSym = std::dynamic_pointer_cast<FunctionSymbol>(sym);
            forward_declare_function(*fSym);
        }
        verify_all_symbols_generated();
        return;
    }

    // PASS 5b: Generate struct method bodies and properties
    for (const auto& sym : symbols->get_all_structs())
    {
        if (sym->isFromLibrary) continue;
        const auto& structSym = *std::dynamic_pointer_cast<StructSymbol>(sym);
        if (is_primitive_impl(structSym))
        {
            // Primitive impl — generate methods with 'this' passed by value
            for (const auto& method : structSym.methods)
            {
                generate_primitive_impl_method(structSym, *method);
            }
        }
        else
        {
            generate_struct_methods(structSym);
        }
    }

    // PASS 6a: Forward declare all global functions
    // Library functions included — declarations needed so user code can call them
    for (const auto& sym : symbols->get_all_functions())
    {
        auto fSym = std::dynamic_pointer_cast<FunctionSymbol>(sym);
        forward_declare_function(*fSym);
        if (fSym->isAsync)
            hasAsyncFunctions = true;
    }

    // PASS 6b: Generate global function bodies
    for (const auto& sym : symbols->get_all_functions())
    {
        if (sym->isFromLibrary) continue;
        generate_function_body(*std::dynamic_pointer_cast<FunctionSymbol>(sym));
        generatedFunctions++;
    }

    // PASS 6c: Generate reflection data (TypeInfoExt) for structs
    if (reflectionMode != "none")
        generate_reflection_data();

    // PASS 7: Generate coro wrappers + runtime support (skip in library mode)
    if (!libraryMode)
    {
        hasAsyncFunctions = true; // we set program as async by default!
        ensure_malloc_free_declared();
        generate_coro_wrappers();
        generate_runtime_declarations();
    }

    if (auto mainSym = !libraryMode ? symbols->lookupFunction("main") : nullptr)
    {
        if (mainSym->isAsync)
        {
            // main() was generated as a coroutine returning ptr
            // Create a real "main" wrapper that calls it and extracts the return value

            // Rename the async main to __djinn_async_main
            llvm::Function* asyncMainFn = functions["main"];
            asyncMainFn->setName("__djinn_async_main");
            functions.erase("main");
            functions["__djinn_async_main"] = asyncMainFn;

            // Create real main() -> i32
            auto* mainFuncType = llvm::FunctionType::get(builder->getInt32Ty(), false);
            auto* realMainFn = llvm::Function::Create(
                mainFuncType, llvm::Function::ExternalLinkage, "main", *module);
            apply_implicit_attributes(realMainFn);
            functions["main"] = realMainFn;

            auto* entryBB = llvm::BasicBlock::Create(*context, "entry", realMainFn);
            builder->SetInsertPoint(entryBB);
            currentFunction = realMainFn;

            // Init runtime with 4 worker threads (default)
            auto* initFn = module->getFunction("__djinn_runtime_init");
            builder->CreateCall(initFn, {builder->getInt32(4)});

            // Call the async main to get the coroutine handle
            llvm::Value* handle = builder->CreateCall(asyncMainFn, {}, "async.hdl");

            // Run the event loop — schedules tasks, processes I/O completions,
            // resumes coroutines after yield. Returns when main coroutine finishes.
            auto* eventLoopFn = module->getFunction("__djinn_event_loop_run");
            builder->CreateCall(eventLoopFn, {handle});

            // Extract result from the coroutine promise in IR
            // (can't do this in C because @llvm.coro.promise doesn't lower
            // properly in non-coroutine functions)
            llvm::Type* mainRetType = generate_type(mainSym->returnType);
            llvm::Value* result = nullptr;
            if (mainRetType && !mainRetType->isVoidTy())
            {
                auto* coroPromiseFn = llvm::Intrinsic::getOrInsertDeclaration(
                    module.get(), llvm::Intrinsic::coro_promise);
                unsigned align = module->getDataLayout().getABITypeAlign(mainRetType).value();
                llvm::Value* promisePtr = builder->CreateCall(coroPromiseFn, {
                                                                  handle,
                                                                  builder->getInt32(align),
                                                                  builder->getFalse()
                                                              }, "main.promise");
                result = builder->CreateLoad(mainRetType, promisePtr, "main.result");
            }
            else
            {
                result = builder->getInt32(0);
            }

            // Destroy the main coroutine frame
            auto* coroDestroyFn = llvm::Intrinsic::getOrInsertDeclaration(
                module.get(), llvm::Intrinsic::coro_destroy);
            builder->CreateCall(coroDestroyFn, {handle});

            // Shutdown runtime
            auto* shutdownFn = module->getFunction("__djinn_runtime_shutdown");
            builder->CreateCall(shutdownFn);

            builder->CreateRet(result);
        }
        else
        {
            // Sync main — always wrap with runtime init/shutdown (async by default)
            // Runtime is needed for memory tracing (__djinn_malloc/__djinn_free)
            // and async infrastructure used by any called functions.
            llvm::Function* syncMainFn = functions["main"];
            syncMainFn->setName("__djinn_sync_main");
            functions.erase("main");
            functions["__djinn_sync_main"] = syncMainFn;

            auto* mainFuncType = llvm::FunctionType::get(builder->getInt32Ty(), false);
            auto* realMainFn = llvm::Function::Create(
                mainFuncType, llvm::Function::ExternalLinkage, "main", *module);
            apply_implicit_attributes(realMainFn);
            functions["main"] = realMainFn;

            auto* entryBB = llvm::BasicBlock::Create(*context, "entry", realMainFn);
            builder->SetInsertPoint(entryBB);
            currentFunction = realMainFn;

            auto* initFn = module->getFunction("__djinn_runtime_init");
            builder->CreateCall(initFn, {builder->getInt32(4)});

            llvm::Value* result;
            if (syncMainFn->getReturnType()->isVoidTy())
            {
                builder->CreateCall(syncMainFn, {});
                result = builder->getInt32(0);
            }
            else
            {
                result = builder->CreateCall(syncMainFn, {}, "sync.result");
            }

            auto* shutdownFn = module->getFunction("__djinn_runtime_shutdown");
            builder->CreateCall(shutdownFn);

            builder->CreateRet(result);
        }
        // NOTE: Old code had a path where main ran without runtime wrapping
        // when no async functions existed. Now runtime is always initialized
        // (async by default). To restore optional non-async mode, add a
        // compiler flag and guard the else branch with `if (!alwaysAsync)`.
    }

    // PASS 8: Verify all symbols were generated
    verify_all_symbols_generated();

    // NOTE: Coroutine and optimization passes are run externally via run_passes()

    // PASS 10: Force emission of used declarations
    // emit_used_declarations();
}

void Generator::verify_all_symbols_generated()
{
    if (!functions.contains("main") && !libraryMode && !stdDeclOnly)
    {
        generate_default_main();
    }

    if (!stdDeclOnly)
    {
        const size_t expectedFunctions = symbols->get_all_functions().size();
        const size_t expectedExternFunctions = symbols->get_all_extern_functions().size();
        const size_t expectedStructs = symbols->get_all_structs().size();

        assert(generatedFunctions == expectedFunctions &&
            "Not all functions were generated!");
        assert(generatedExternFunctions == expectedExternFunctions &&
            "Not all extern functions were generated!");
        assert(generatedStructs == expectedStructs &&
            "Not all structs were generated!");
    }

    // Verify all generated functions exist in the LLVM module
    for (const auto& sym : symbols->get_all_functions())
    {
        const auto funcSym = std::dynamic_pointer_cast<FunctionSymbol>(sym);
        assert(functions.count(funcSym->name) > 0 &&
            ("Function not found in LLVM module: " + funcSym->name).c_str());
    }

    for (const auto& sym : symbols->get_all_extern_functions())
    {
        const auto externSym = std::dynamic_pointer_cast<ExternFunctionSymbol>(sym);
        assert(functions.count(externSym->name) > 0 &&
            ("Extern function not found in LLVM module: " + externSym->name).c_str());
    }

    LOG_INFO("verification passed: %zu functions, %zu extern functions, %zu structs generated.",
             generatedFunctions, generatedExternFunctions, generatedStructs);
}


void Generator::generate_default_main()
{
    const auto mainFunc = llvm::Function::Create(
        llvm::FunctionType::get(builder->getInt32Ty(), false),
        llvm::Function::ExternalLinkage,
        "main",
        *module
    );
    apply_implicit_attributes(mainFunc);
    functions["main"] = mainFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", mainFunc);
    builder->SetInsertPoint(entry);
    builder->CreateRet(builder->getInt32(0));
}

// llvm::Function *Generator::generate_function(
//     const std::string &name,
//     const Type &returnType,
//     const std::vector<std::pair<Type, std::string> > &parameters
// ) {
//     const auto return_value = generate_type(returnType);
//     const auto llvmFunc = llvm::Function::Create(
//         llvm::FunctionType::get(return_value, false),
//         llvm::Function::ExternalLinkage,
//         name,
//         *module
//     );
//     functions[name] = llvmFunc;
//
//     const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
//     builder->SetInsertPoint(entry);
//
//     push_scope();
//     size_t idx = 0;
//     for (auto &arg: llvmFunc->args()) {
//         const auto &param = parameters[idx];
//         arg.setName(param.second);
//
//         auto *alloca = builder->CreateAlloca(arg.getType(), nullptr, param.second);
//         builder->CreateStore(&arg, alloca);
//         std::string structTypeName = param.first.kind == TypeKind::STRUCT ? param.first.structName : "";
//         currentScope->define_variable(param.second, alloca, structTypeName);
//         idx++;
//     }
//     if (builder->GetInsertBlock()->getTerminator()) {
//         pop_scope();
//         return llvmFunc;
//     }
//
//     if (return_value->isVoidTy()) {
//         builder->CreateRetVoid();
//         pop_scope();
//         return llvmFunc;
//     }
//
//     builder->CreateRet(llvm::Constant::getNullValue(return_value));
//     pop_scope();
//     return llvmFunc;
// }

void Generator::run_passes(bool skipCoroPasses) const
{
    {
        std::string errorStr;
        llvm::raw_string_ostream errorStream(errorStr);
        if (llvm::verifyModule(*module, &errorStream))
        {
            LOG_ERROR("[run_passes] LLVM module verification FAILED before passes:\n%s", errorStr.c_str());
            for (auto& fn : *module)
            {
                if (fn.isDeclaration()) continue;
                std::string fnErr;
                llvm::raw_string_ostream fnStream(fnErr);
                if (llvm::verifyFunction(fn, &fnStream))
                    LOG_ERROR("[run_passes]   broken function: '%s': %s", fn.getName().str().c_str(), fnErr.c_str());
            }
        }
    }

    if (skipCoroPasses) return;

    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM;
    MPM.addPass(llvm::CoroEarlyPass());

    llvm::CGSCCPassManager CGPM;
    CGPM.addPass(llvm::CoroSplitPass());
    MPM.addPass(llvm::createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPM)));

    MPM.addPass(llvm::CoroCleanupPass());
    MPM.run(*module, MAM);
}

std::string Generator::print() const
{
    std::string str;
    llvm::raw_string_ostream stream(str);
    module->print(stream, nullptr);
    return str;
}

std::pair<std::unique_ptr<llvm::Module>, std::unique_ptr<llvm::LLVMContext>> Generator::takeModule()
{
    builder.reset(); // IRBuilder borrows module/context; drop it before moving them out
    return {std::move(module), std::move(context)};
}

bool Generator::verify() const
{
    std::string errorStr;
    llvm::raw_string_ostream errorStream(errorStr);
    if (llvm::verifyModule(*module, &errorStream))
    {
        LOG_ERROR("LLVM module verification failed:\n%s", errorStr.c_str());
        for (auto& fn : *module)
        {
            if (fn.isDeclaration()) continue;
            std::string fnErr;
            llvm::raw_string_ostream fnStream(fnErr);
            if (llvm::verifyFunction(fn, &fnStream))
                LOG_ERROR("  broken function: '%s': %s", fn.getName().str().c_str(), fnErr.c_str());
        }
        return false;
    }
    return true;
}

bool Generator::linkModules(const std::vector<std::filesystem::path>& llPaths) const
{
    for (const auto& path : llPaths)
    {
        if (!std::filesystem::exists(path))
        {
            LOG_ERROR("Link error: file not found: %s", path.string().c_str());
            return false;
        }

        llvm::SMDiagnostic err;
        auto linkedModule = llvm::parseIRFile(path.string(), err, *context);

        if (!linkedModule)
        {
            LOG_ERROR("Link error: failed to parse %s", path.string().c_str());
            err.print("djinn", llvm::errs());
            return false;
        }

        if (llvm::Linker::linkModules(*module, std::move(linkedModule)))
        {
            LOG_ERROR("Link error: failed to link %s", path.string().c_str());
            return false;
        }
    }
    return true;
}

bool Generator::linkBitcode(const std::string& bitcodeData) const
{
    auto buffer = llvm::MemoryBuffer::getMemBuffer(
        llvm::StringRef(bitcodeData.data(), bitcodeData.size()), "", false);

    auto moduleOrErr = llvm::parseBitcodeFile(buffer->getMemBufferRef(), *context);
    if (!moduleOrErr)
    {
        LOG_ERROR("Link error: failed to parse djlib bitcode");
        return false;
    }

    if (llvm::Linker::linkModules(*module, std::move(*moduleOrErr)))
    {
        LOG_ERROR("Link error: failed to link djlib bitcode");
        return false;
    }

    return true;
}
