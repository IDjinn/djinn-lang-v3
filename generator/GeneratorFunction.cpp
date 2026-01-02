//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

void Generator::generate_struct(const StructDeclaration &structDecl) {
    std::vector<llvm::Type *> fieldTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    unsigned idx = 0;
    for (const auto &field: structDecl.fields) {
        fieldTypes.push_back(generate_type(*field.type));
        fieldIndices[field.name] = idx++;
    }

    llvm::StructType *structType = llvm::StructType::create(
        *context,
        fieldTypes,
        structDecl.name
    );

    structTypes[structDecl.name] = structType;
    structFieldIndices[structDecl.name] = std::move(fieldIndices);
}

void Generator::generate_function(const FunctionDeclaration &func) {
    namedValues.clear();
    variableStructTypes.clear();

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
        namedValues[param.name] = alloca;
        if (param.type->kind == TypeKind::STRUCT) {
            variableStructTypes[param.name] = param.type->structName;
        }
        idx++;
    }

    if (func.body) {
        for (const auto &stmt: func.body->statements) {
            generate_statement(*stmt);
        }
    }

    if (builder->GetInsertBlock()->getTerminator()) return;

    if (returnType->isVoidTy()) {
        builder->CreateRetVoid();
        return;
    }

    builder->CreateRet(llvm::Constant::getNullValue(returnType));
}