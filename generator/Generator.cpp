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
      builder(std::make_unique<llvm::IRBuilder<> >(*context)),
      currentScope(std::make_shared<GeneratorScope>()) {
    declare_extern_functions();
}

void Generator::push_scope() {
    currentScope = std::make_shared<GeneratorScope>(currentScope);
}

void Generator::pop_scope() {
    if (currentScope->parent) {
        currentScope = currentScope->parent;
    }
}

void Generator::declare_extern_functions() {
    // functions["printf"] = llvm::Function::Create(
    //     llvm::FunctionType::get(
    //         builder->getInt32Ty(),
    //         {builder->getPtrTy()},
    //         true
    //     ),
    //     llvm::Function::ExternalLinkage,
    //     "printf",
    //     *module
    // );
}

void Generator::generate(const Program &program) {
    for (const auto &externFunc: program.externFunctions) {
        generate_extern_function(*externFunc);
    }

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

llvm::Function *Generator::generate_function(
    const std::string &name,
    const Type &returnType,
    const std::vector<std::pair<Type, std::string> > &parameters
) {
    const auto return_value = generate_type(returnType);
    const auto llvmFunc = llvm::Function::Create(
        llvm::FunctionType::get(return_value, false),
        llvm::Function::ExternalLinkage,
        name,
        *module
    );
    functions[name] = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    push_scope();
    size_t idx = 0;
    for (auto &arg: llvmFunc->args()) {
        const auto &param = parameters[idx];
        arg.setName(param.second);

        auto *alloca = builder->CreateAlloca(arg.getType(), nullptr, param.second);
        builder->CreateStore(&arg, alloca);
        std::string structTypeName = param.first.kind == TypeKind::STRUCT ? param.first.structName : "";
        currentScope->define_variable(param.second, alloca, structTypeName);
        idx++;
    }
    if (builder->GetInsertBlock()->getTerminator()) {
        pop_scope();
        return llvmFunc;
    }

    if (return_value->isVoidTy()) {
        builder->CreateRetVoid();
        pop_scope();
        return llvmFunc;
    }

    builder->CreateRet(llvm::Constant::getNullValue(return_value));
    pop_scope();
    return llvmFunc;
}

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