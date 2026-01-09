//
// Created by Luke on 02/01/2026.
//
#include "../Generator.h"

// ============================================================================
// Multi-pass struct generation (like C#)
// Phase 1: Forward declare all struct types as opaque
// Phase 2: Resolve struct bodies (fill in fields)
// Phase 3: Generate methods
// ============================================================================

void Generator::forward_declare_struct(const StructDeclaration &struct_declaration, const std::string &prefix) {
    const std::string qualifiedName = prefix.empty()
                                          ? struct_declaration.name
                                          : prefix + "::" + struct_declaration.name;

    if (struct_declaration.isTransparent()) {
        llvm::Type *underlyingType = generate_type(*struct_declaration.baseType);
        currentScope->define_transparent_type(qualifiedName, underlyingType);

        // Create opaque wrapper struct for method resolution
        llvm::StructType *wrapperType = llvm::StructType::create(*context, qualifiedName);
        std::unordered_map<std::string, unsigned> fieldIndices;
        currentScope->define_struct(qualifiedName, wrapperType, std::move(fieldIndices));
        return;
    }

    unsigned idx = 0;
    std::unordered_map<std::string, unsigned> fieldIndices;
    for (const auto &field: struct_declaration.fields) {
        fieldIndices[field.name] = idx++;
    }

    llvm::StructType *structType = llvm::StructType::create(*context, qualifiedName);
    currentScope->define_struct(qualifiedName, structType, fieldIndices);

    if (struct_declaration.isGeneric()) {
        GenericStructDef genericDef;
        genericDef.name = qualifiedName;
        genericDef.params = struct_declaration.genericParams;
        genericDef.llvmType = structType;
        genericDef.fieldIndices = fieldIndices;

        for (const auto &field: struct_declaration.fields) {
            genericDef.fields.push_back({field.name, *field.type});
        }

        for (const auto &method: struct_declaration.methods) {
            genericDef.methods.push_back(method.get());
        }

        currentScope->define_generic_struct(qualifiedName, std::move(genericDef));
    }
}

void Generator::resolve_struct_body(const StructDeclaration &struct_declaration, const std::string &prefix) {
    const std::string qualifiedName = prefix.empty()
                                          ? struct_declaration.name
                                          : prefix + "::" + struct_declaration.name;

    // Generic structs are resolved during monomorphization
    if (struct_declaration.isGeneric()) {
        return;
    }

    // Transparent types: set the wrapper body
    if (struct_declaration.isTransparent()) {
        llvm::StructType *wrapperType = currentScope->lookup_struct(qualifiedName);
        if (wrapperType && wrapperType->isOpaque()) {
            llvm::Type *underlyingType = currentScope->lookup_transparent_type(qualifiedName);
            wrapperType->setBody({underlyingType});
        }
        return;
    }

    // Get the opaque struct and fill its body
    llvm::StructType *structType = currentScope->lookup_struct(qualifiedName);
    if (!structType) {
        throw std::runtime_error("Struct not forward declared: " + qualifiedName);
    }

    std::vector<llvm::Type *> fieldTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    unsigned idx = 0;
    for (const auto &field: struct_declaration.fields) {
        fieldTypes.push_back(generate_type(*field.type));
        fieldIndices[field.name] = idx++;
    }

    // Set the struct body
    structType->setBody(fieldTypes);

    // Update field indices in scope
    currentScope->structFieldIndices[qualifiedName] = std::move(fieldIndices);
}

void Generator::generate_struct_methods(const StructDeclaration &struct_declaration, const std::string &prefix) {
    // Generic structs: methods are generated during monomorphization
    if (struct_declaration.isGeneric()) {
        return;
    }

    const std::string qualifiedName = prefix.empty()
                                          ? struct_declaration.name
                                          : prefix + "::" + struct_declaration.name;

    llvm::StructType *structType = currentScope->lookup_struct(qualifiedName);
    if (!structType) {
        throw std::runtime_error("Struct not found for method generation: " + qualifiedName);
    }

    for (const auto &method: struct_declaration.methods) {
        generate_method(*method, struct_declaration, structType);
    }
}

// Namespace helpers for multi-pass
void Generator::forward_declare_namespace_structs(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    for (const auto &structDecl: ns.structs) {
        forward_declare_struct(*structDecl, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        forward_declare_namespace_structs(*nestedNs, qualifiedPrefix);
    }
}

void Generator::resolve_namespace_struct_bodies(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    for (const auto &structDecl: ns.structs) {
        resolve_struct_body(*structDecl, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        resolve_namespace_struct_bodies(*nestedNs, qualifiedPrefix);
    }
}

void Generator::generate_namespace_struct_methods(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name : prefix + "::" + ns.name;

    for (const auto &structDecl: ns.structs) {
        generate_struct_methods(*structDecl, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        generate_namespace_struct_methods(*nestedNs, qualifiedPrefix);
    }
}

// ============================================================================
// Original single-pass method (kept for compatibility)
// ============================================================================

void Generator::generate_struct(const StructDeclaration &struct_declaration, const std::string &prefix) {
    // Nome qualificado da struct
    const std::string qualifiedName = prefix.empty()
                                          ? struct_declaration.name
                                          : prefix + "::" + struct_declaration.name;

    if (struct_declaration.isGeneric()) {
        GenericStructDef genericDef;
        genericDef.name = qualifiedName;
        genericDef.params = struct_declaration.genericParams;

        for (const auto &field: struct_declaration.fields) {
            genericDef.fields.push_back({field.name, *field.type});
        }

        currentScope->define_generic_struct(qualifiedName, std::move(genericDef));
        return;
    }

    // Handle transparent types: struct Size : i32; (no fields, inherits from primitive)
    if (struct_declaration.isTransparent()) {
        llvm::Type *underlyingType = generate_type(*struct_declaration.baseType);
        currentScope->define_transparent_type(qualifiedName, underlyingType);

        // Generate methods for transparent type
        // For methods, we create a wrapper struct type just for 'this' pointer semantics
        llvm::StructType *wrapperType = llvm::StructType::create(
            *context,
            {underlyingType},
            qualifiedName
        );

        // Also register as regular struct for method resolution
        std::unordered_map<std::string, unsigned> fieldIndices;
        currentScope->define_struct(qualifiedName, wrapperType, std::move(fieldIndices));

        for (const auto &method: struct_declaration.methods) {
            generate_method(*method, struct_declaration, wrapperType);
        }
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
        qualifiedName
    );

    currentScope->define_struct(qualifiedName, structType, std::move(fieldIndices));

    // Generate methods
    for (const auto &method: struct_declaration.methods) {
        generate_method(*method, struct_declaration, structType);
    }
}

void Generator::generate_method(const StructMethodDeclaration &method, const StructDeclaration &struc,
                                llvm::StructType *structType) {
    push_scope();

    const auto structName = struc.name;
    // Method name: StructName__methodName
    const std::string mangledName = structName + "__" + method.name;

    // For non-generic structs, return type is always concrete
    llvm::Type *returnType = generate_type(*method.returnType);

    std::vector<llvm::Type *> paramTypes;

    // Static methods don't have 'this' parameter
    const bool isStatic = method.isStatic();
    if (!isStatic) {
        paramTypes.push_back(llvm::PointerType::get(structType, 0)); // this pointer
    }

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

    auto argIt = llvmFunc->arg_begin();

    // Define 'this' parameter only for non-static methods
    if (!isStatic) {
        argIt->setName("this");
        auto *thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, structName);
        ++argIt;
    }

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