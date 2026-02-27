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

void Generator::generate()
{
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

    // PASS 1: Forward declare all structs (create opaque types)
    const auto allStructs = symbols->get_all_structs();
    LOG_DEBUG("[generator] PASS 1: forward declaring %zu structs", allStructs.size());
    for (const auto& sym : allStructs)
    {
        LOG_DEBUG("[generator]   forward declare struct: '%s'", sym->name.c_str());
        forward_declare_struct(*std::dynamic_pointer_cast<StructSymbol>(sym));
        generatedStructs++;
    }

    // PASS 2: Generate all enums (generic ones are just registered for monomorphization)
    for (const auto& sym : symbols->get_all_enums())
    {
        generate_enum(*std::dynamic_pointer_cast<EnumSymbol>(sym));
    }

    // PASS 3: Generate extern functions
    for (const auto& sym : symbols->get_all_extern_functions())
    {
        generate_extern_function(*std::dynamic_pointer_cast<ExternFunctionSymbol>(sym));
        generatedExternFunctions++;
    }

    // PASS 4: Resolve struct bodies (fill in field types)
    for (const auto& sym : symbols->get_all_structs())
    {
        resolve_struct_body(*std::dynamic_pointer_cast<StructSymbol>(sym));
    }

    // PASS 5: Generate struct methods and properties
    for (const auto& sym : symbols->get_all_structs())
    {
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
    for (const auto& sym : symbols->get_all_functions())
    {
        forward_declare_function(*std::dynamic_pointer_cast<FunctionSymbol>(sym));
    }

    // PASS 6b: Generate global function bodies
    for (const auto& sym : symbols->get_all_functions())
    {
        generate_function_body(*std::dynamic_pointer_cast<FunctionSymbol>(sym));
        generatedFunctions++;
    }

    // PASS 7: Generate async main wrapper if main is async
    if (auto mainSym = symbols->lookupFunction("main"))
    {
        if (mainSym->isAsync)
        {
            // main() was generated as a coroutine returning ptr
            // Create a real "main" wrapper that calls it and extracts the return value

            hasAsyncFunctions = true;
            ensure_malloc_free_declared();

            // Generate coro wrappers and runtime declarations for the event loop
            generate_coro_wrappers();
            generate_runtime_declarations();

            // Rename the async main to __djinn_async_main
            llvm::Function* asyncMainFn = functions["main"];
            asyncMainFn->setName("__djinn_async_main");
            functions.erase("main");
            functions["__djinn_async_main"] = asyncMainFn;

            // Create real main() -> i32
            auto* mainFuncType = llvm::FunctionType::get(builder->getInt32Ty(), false);
            auto* realMainFn = llvm::Function::Create(
                mainFuncType, llvm::Function::ExternalLinkage, "main", *module);
            functions["main"] = realMainFn;

            auto* entryBB = llvm::BasicBlock::Create(*context, "entry", realMainFn);
            builder->SetInsertPoint(entryBB);
            currentFunction = realMainFn;

            // Init runtime with 4 worker threads (default)
            auto* initFn = module->getFunction("__djinn_runtime_init");
            builder->CreateCall(initFn, {builder->getInt32(4)});

            // Call the async main to get the coroutine handle
            llvm::Value* handle = builder->CreateCall(asyncMainFn, {}, "async.hdl");

            // Use the proven await loop to resume + extract result directly in IR
            // (avoids C runtime needing to call @llvm.coro.promise which may not
            // lower properly in non-coroutine wrapper functions)
            llvm::Type* mainRetType = generate_type(mainSym->returnType);
            llvm::Value* result = generate_await_loop(handle, mainRetType);

            // Shutdown runtime
            auto* shutdownFn = module->getFunction("__djinn_runtime_shutdown");
            builder->CreateCall(shutdownFn);

            builder->CreateRet(result);
        }
    }

    // PASS 8: Verify all symbols were generated
    verify_all_symbols_generated();

    // NOTE: Coroutine and optimization passes are run externally via run_passes()

    // PASS 10: Force emission of used declarations
    // emit_used_declarations();
}

void Generator::verify_all_symbols_generated()
{
    if (!functions.contains("main"))
    {
        generate_default_main();
    }

    const size_t expectedFunctions = symbols->get_all_functions().size();
    const size_t expectedExternFunctions = symbols->get_all_extern_functions().size();
    const size_t expectedStructs = symbols->get_all_structs().size();

    assert(generatedFunctions == expectedFunctions &&
        "Not all functions were generated!");
    assert(generatedExternFunctions == expectedExternFunctions &&
        "Not all extern functions were generated!");
    assert(generatedStructs == expectedStructs &&
        "Not all structs were generated!");

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

void Generator::run_passes(bool optimize) const
{
    if (!optimize) return;

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

    // O2 pipeline includes CoroEarly/CoroSplit/CoroElide/CoroCleanup.
    // When not optimizing, we output pre-split IR with presplitcoroutine
    // attribute and let clang handle coroutine lowering.
    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    MPM.run(*module, MAM);
}

std::string Generator::print() const
{
    std::string errorStr;
    llvm::raw_string_ostream errorStream(errorStr);

    if (llvm::verifyModule(*module, &errorStream))
    {
        LOG_ERROR("LLVM module verification failed:\n%s", errorStr.c_str());
        return "Erro: módulo inválido\n" + errorStr;
    }

    std::string str;
    llvm::raw_string_ostream stream(str);
    module->print(stream, nullptr);
    return str;
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

