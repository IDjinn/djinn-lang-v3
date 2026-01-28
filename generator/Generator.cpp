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
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/SourceMgr.h"
#include <iostream>

#include "../binder/SymbolTable.h"

Generator::Generator(DiagnosticEngine &diagnostics, const std::shared_ptr<ScopedSymbolTable> &symbols)
    : _diagnostics(diagnostics),
      symbols(symbols),
      context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("djinn_module", *context)),
      builder(std::make_unique<llvm::IRBuilder<> >(*context)),
      currentScope(std::make_shared<GeneratorScope>()) {
}

void Generator::push_scope() {
    currentScope = std::make_shared<GeneratorScope>(currentScope);
}

void Generator::pop_scope() {
    if (currentScope->parent) {
        currentScope = currentScope->parent;
    }
}

void Generator::generate() {
    // PASS 1: Forward declare all structs (create opaque types)
    for (const auto &sym: symbols->get_all_structs()) {
        forward_declare_struct(*std::dynamic_pointer_cast<StructSymbol>(sym));
        generatedStructs++;
    }

    // PASS 2: Generate all enums (generic ones are just registered for monomorphization)
    for (const auto &sym: symbols->get_all_enums()) {
        generate_enum(*std::dynamic_pointer_cast<EnumSymbol>(sym));
    }

    // PASS 3: Generate extern functions
    for (const auto &sym: symbols->get_all_extern_functions()) {
        generate_extern_function(*std::dynamic_pointer_cast<ExternFunctionSymbol>(sym));
        generatedExternFunctions++;
    }

    // PASS 4: Resolve struct bodies (fill in field types)
    for (const auto &sym: symbols->get_all_structs()) {
        resolve_struct_body(*std::dynamic_pointer_cast<StructSymbol>(sym));
    }

    // PASS 5: Generate struct methods and properties
    for (const auto &sym: symbols->get_all_structs()) {
        generate_struct_methods(*std::dynamic_pointer_cast<StructSymbol>(sym));
    }

    // PASS 6: Generate global functions
    for (const auto &sym: symbols->get_all_functions()) {
        generate_function(*std::dynamic_pointer_cast<FunctionSymbol>(sym));
        generatedFunctions++;
    }

    // PASS 7: Verify all symbols were generated
    verify_all_symbols_generated();

    // PASS 8: Force emission of used declarations
    // emit_used_declarations();
}

void Generator::verify_all_symbols_generated() const {
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
    for (const auto &sym: symbols->get_all_functions()) {
        const auto funcSym = std::dynamic_pointer_cast<FunctionSymbol>(sym);
        assert(functions.count(funcSym->name) > 0 &&
               ("Function not found in LLVM module: " + funcSym->name).c_str());
    }

    for (const auto &sym: symbols->get_all_extern_functions()) {
        const auto externSym = std::dynamic_pointer_cast<ExternFunctionSymbol>(sym);
        assert(functions.count(externSym->name) > 0 &&
               ("Extern function not found in LLVM module: " + externSym->name).c_str());
    }

    std::cout << "[Generator] Verification passed: "
            << generatedFunctions << " functions, "
            << generatedExternFunctions << " extern functions, "
            << generatedStructs << " structs generated." << std::endl;
}


void Generator::generate_default_main() {
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

void Generator::optimize() const {
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

    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    MPM.run(*module, MAM);
}

std::string Generator::print() const {
    std::string errorStr;
    llvm::raw_string_ostream errorStream(errorStr);

    if (llvm::verifyModule(*module, &errorStream)) {
        return "Erro: módulo inválido\n" + errorStr;
    }

    std::string str;
    llvm::raw_string_ostream stream(str);
    module->print(stream, nullptr);
    return str;
}

bool Generator::linkModules(const std::vector<std::filesystem::path> &llPaths) const {
    for (const auto &path: llPaths) {
        if (!std::filesystem::exists(path)) {
            std::cerr << "Link error: file not found: " << path << std::endl;
            return false;
        }

        llvm::SMDiagnostic err;
        auto linkedModule = llvm::parseIRFile(path.string(), err, *context);

        if (!linkedModule) {
            std::cerr << "Link error: failed to parse " << path << std::endl;
            err.print("djinn", llvm::errs());
            return false;
        }

        if (llvm::Linker::linkModules(*module, std::move(linkedModule))) {
            std::cerr << "Link error: failed to link " << path << std::endl;
            return false;
        }
    }
    return true;
}