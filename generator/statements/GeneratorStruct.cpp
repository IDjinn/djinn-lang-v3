//
// Created by Luke on 02/01/2026.
//
#include "../Generator.h"

void Generator::generate_struct(const StructDeclaration &struct_declaration) {
    if (struct_declaration.isGeneric()) {
        GenericStructDef genericDef;
        genericDef.name = struct_declaration.name;
        genericDef.params = struct_declaration.genericParams;

        for (const auto &field: struct_declaration.fields) {
            genericDef.fields.push_back({field.name, *field.type});
        }

        currentScope->define_generic_struct(struct_declaration.name, std::move(genericDef));
        return;
    }

    std::vector<llvm::Type *> fieldTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    unsigned idx = 0;
    for (const auto &field: struct_declaration.fields) {
        fieldTypes.push_back(generate_type(*field.type));
        fieldIndices[field.name] = idx++;
    }

    llvm::StructType *structType = llvm::StructType::create(
        *context,
        fieldTypes,
        struct_declaration.name
    );

    currentScope->define_struct(struct_declaration.name, structType, std::move(fieldIndices));

    // Generate methods
    for (const auto &method: struct_declaration.methods) {
        generate_method(*method, struct_declaration.name, structType);
    }
}

void Generator::generate_method(const StructMethodDeclaration &method, const std::string &structName,
                                llvm::StructType *structType) {
    push_scope();

    // Method name: StructName__methodName
    const std::string mangledName = structName + "__" + method.name;

    llvm::Type *returnType = generate_type(*method.returnType);

    // First parameter is always 'this' (pointer to struct)
    std::vector<llvm::Type *> paramTypes;
    paramTypes.push_back(llvm::PointerType::get(structType, 0)); // this pointer

    for (const auto &param: method.parameters) {
        paramTypes.push_back(generate_type(*param.type));
    }

    const auto funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    const auto llvmFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        mangledName,
        *module
    );

    functions[mangledName] = llvmFunc;
    currentScope->define_method(structName, method.name, llvmFunc);
    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    // Define 'this' parameter
    auto argIt = llvmFunc->arg_begin();
    argIt->setName("this");
    auto *thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
    builder->CreateStore(&*argIt, thisAlloca);
    currentScope->define_variable("this", thisAlloca, structName);
    ++argIt;

    // Define other parameters
    size_t paramIdx = 0;
    while (argIt != llvmFunc->arg_end()) {
        const auto &param = method.parameters[paramIdx];
        argIt->setName(param.name);

        auto *alloca = builder->CreateAlloca(argIt->getType(), nullptr, param.name);
        builder->CreateStore(&*argIt, alloca);
        std::string paramStructType = param.type->kind == TypeKind::STRUCT ? param.type->structName : "";
        currentScope->define_variable(param.name, alloca, paramStructType);

        ++argIt;
        ++paramIdx;
    }

    // Generate method body
    if (method.body) {
        // Block body: { ... }
        for (const auto &stmt: method.body->statements) {
            generate_statement(*stmt);
        }
    } else if (method.expression) {
        // Expression body: => expr;
        llvm::Value *result = generate_expression(*method.expression);
        if (!returnType->isVoidTy()) {
            builder->CreateRet(result);
        } else {
            builder->CreateRetVoid();
        }
    }

    // Add default return if needed
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (returnType->isVoidTy()) {
            builder->CreateRetVoid();
        } else {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    pop_scope();
}