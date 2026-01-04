//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"


void Generator::generate_function(const FunctionDeclaration &func) {
    push_scope();

    llvm::Type *returnType = this->generate_type(*func.returnType);

    std::vector<llvm::Type *> paramTypes{};
    for (const auto &param: func.parameters) {
        paramTypes.emplace_back(generate_type(*param.type));
    }

    const auto funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    const auto llvmFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        func.name,
        *module
    );
    functions[func.name] = llvmFunc;
    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    size_t idx = 0;
    for (auto &arg: llvmFunc->args()) {
        const auto &param = func.parameters[idx];
        arg.setName(param.name);

        auto *alloca = builder->CreateAlloca(arg.getType(), nullptr, param.name);
        builder->CreateStore(&arg, alloca);
        std::string structTypeName = param.type->kind == TypeKind::STRUCT ? param.type->structName : "";
        currentScope->define_variable(param.name, alloca, structTypeName);
        idx++;
    }

    if (func.body) {
        for (const auto &stmt: func.body->statements) {
            generate_statement(*stmt);
        }
    }

    if (builder->GetInsertBlock()->getTerminator()) {
        pop_scope();
        return;
    }

    if (returnType->isVoidTy()) {
        builder->CreateRetVoid();
        pop_scope();
        return;
    }

    builder->CreateRet(llvm::Constant::getNullValue(returnType));
    pop_scope();
}

void Generator::generate_extern_function(const ExternFunctionDeclaration &decl) {
    std::vector<llvm::Type *> paramTypes;
    for (const auto &param: decl.parameters) {
        paramTypes.push_back(generate_type(*param.type));
    }

    llvm::Type *returnType = generate_type(*decl.returnType);

    llvm::FunctionType *funcType = llvm::FunctionType::get(
        returnType,
        paramTypes,
        decl.isVariadic
    );

    llvm::Function *func = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        decl.name,
        *module
    );

    functions[decl.name] = func;

    if (decl.abi == "C") {
        func->setCallingConv(llvm::CallingConv::C);
    }
}