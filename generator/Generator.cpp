//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/CGSCCPassManager.h"

Generator::Generator()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("djinn", *context)),
      builder(std::make_unique<llvm::IRBuilder<> >(*context)) {
    declare_extern_functions();
}

void Generator::declare_extern_functions() {
    functions["printf"] = llvm::Function::Create(
        llvm::FunctionType::get(
            builder->getInt32Ty(),
            {builder->getPtrTy()},
            true
        ),
        llvm::Function::ExternalLinkage,
        "printf",
        *module
    );
}

void Generator::generate(const Program &program) {
    for (const auto &structDecl: program.structs) {
        generate_struct(*structDecl);
    }

    for (const auto &func: program.functions) {
        generate_function(*func);
    }

    if (!functions.contains("main")) {
        generate_default_main();
    }
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

void Generator::optimize() {
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