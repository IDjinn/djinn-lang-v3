//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"


void Generator::generate_function(const FunctionDeclaration &func, const std::string &prefix) {
    push_scope();

    const std::string qualifiedName = prefix.empty() ? func.name.token_name : prefix + "::" + func.name.token_name;

    llvm::Type *returnType = this->generate_type(*func.returnType);

    std::vector<llvm::Type *> paramTypes{};
    for (const auto &param: func.parameters) {
        paramTypes.emplace_back(generate_type(*param.type));
    }

    const auto funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    const auto llvmFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        func.name.token_name,
        *module
    );
    functions[qualifiedName] = llvmFunc;
    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    size_t idx = 0;
    for (auto &arg: llvmFunc->args()) {
        const auto &param = func.parameters[idx];
        arg.setName(param.name.token_name);

        auto *alloca = builder->CreateAlloca(arg.getType(), nullptr, param.name.token_name);
        builder->CreateStore(&arg, alloca);
        std::string structTypeName = param.type->kind == TypeKind::STRUCT ? param.type->structName : "";
        currentScope->define_variable(param.name.token_name, alloca, structTypeName);
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
        decl.name.token_name,
        *module
    );

    functions[decl.name.token_name] = func;

    if (decl.abi == "C") {
        func->setCallingConv(llvm::CallingConv::C);
    }
}