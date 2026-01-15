//
// Created by Luke on 02/01/2026.
//
#include "../Generator.h"

// ============================================================================
// Multi-pass struct generation (like C#)
// Phase 1: Forward declare all struct types as opaque
// Phase 2: Resolve struct bodies (fill in fields)
// Phase 3: Generate methods and properties
// ============================================================================

void Generator::forward_declare_struct(const StructDeclaration &struct_declaration, const std::string &prefix) {
    const std::string qualifiedName = prefix.empty()
                                          ? struct_declaration.name.token_name
                                          : prefix + "::" + struct_declaration.name.token_name;

    StructDef def(qualifiedName, struct_declaration.isGeneric());
    def.isTransparent = struct_declaration.isTransparent();
    def.genericParams = struct_declaration.genericParams;

    // Build field indices
    unsigned idx = 0;
    for (const auto &field: struct_declaration.fields) {
        def.fields.emplace_back(field.name.token_name, *field.type);
        def.fieldIndices[field.name.token_name] = idx++;
    }

    // Store methods and properties for later generation
    for (const auto &method: struct_declaration.methods) {
        def.methods.push_back(method.get());
    }
    for (const auto &prop: struct_declaration.properties) {
        def.properties.push_back(&prop);
    }

    // Handle transparent types
    if (struct_declaration.isTransparent()) {
        def.transparentUnderlying = generate_type(*struct_declaration.baseType);
        def.llvmType = llvm::StructType::create(*context, qualifiedName);
    } else {
        // Create opaque struct type
        def.llvmType = llvm::StructType::create(*context, qualifiedName);
    }

    // Track non-generic types for forced emission
    if (!def.isGeneric) {
        declaredTypes.push_back(def.llvmType);
    }

    currentScope->define_struct(qualifiedName, std::move(def));
}

void Generator::resolve_struct_body(const StructDeclaration &struct_declaration, const std::string &prefix) {
    const std::string qualifiedName = prefix.empty()
                                          ? struct_declaration.name.token_name
                                          : prefix + "::" + struct_declaration.name.token_name;

    StructDef *def = currentScope->lookup_struct(qualifiedName);
    if (!def) {
        throw std::runtime_error("Struct not forward declared: " + qualifiedName);
    }

    // Generic structs are resolved during monomorphization
    if (def->isGeneric) {
        return;
    }

    // Transparent types
    if (def->isTransparent) {
        if (def->llvmType && def->llvmType->isOpaque()) {
            def->llvmType->setBody({def->transparentUnderlying});
        }
        return;
    }

    // Build field types
    std::vector<llvm::Type *> fieldTypes;
    for (const auto &[fieldName, fieldType]: def->fields) {
        fieldTypes.push_back(generate_type(fieldType));
    }

    // Set the struct body
    def->llvmType->setBody(fieldTypes);
}

void Generator::generate_struct_methods(const StructDeclaration &struct_declaration, const std::string &prefix) {
    // Generic structs: methods are generated during monomorphization
    if (struct_declaration.isGeneric()) {
        return;
    }

    const std::string qualifiedName = prefix.empty()
                                          ? struct_declaration.name.token_name
                                          : prefix + "::" + struct_declaration.name.token_name;

    StructDef *def = currentScope->lookup_struct(qualifiedName);
    if (!def || !def->llvmType) {
        throw std::runtime_error("Struct not found for method generation: " + qualifiedName);
    }

    // Generate properties (getters/setters)
    for (const auto &prop: struct_declaration.properties) {
        generate_property(prop, struct_declaration, def->llvmType, qualifiedName);
    }

    // Generate methods
    for (const auto &method: struct_declaration.methods) {
        generate_method(*method, struct_declaration, def->llvmType);
    }
}

// Generate a C#-style property with getter and/or setter
void Generator::generate_property(const StructProperty &prop, const StructDeclaration &struc,
                                  llvm::StructType *structType, const std::string &qualifiedName) {
    StructDef *def = currentScope->lookup_struct(qualifiedName);
    if (!def) return;

    // Register property info
    PropertyInfo propInfo;
    propInfo.name = prop.name.token_name;
    propInfo.hasGetter = prop.hasGetter;
    propInfo.hasSetter = prop.hasSetter;

    // For auto-properties, find backing field
    if (prop.isAutoProperty()) {
        propInfo.backingFieldName = prop.backingFieldName();
        if (auto it = def->fieldIndices.find(propInfo.backingFieldName); it != def->fieldIndices.end()) {
            propInfo.backingFieldIndex = it->second;
        }
    }

    def->propertyInfos[prop.name.token_name] = propInfo;

    // Generate getter method only for computed properties (with explicit body/expression)
    // Auto-properties access the field directly
    if (prop.hasGetter && (prop.getterBody || prop.getterExpr)) {
        push_scope();

        const std::string getterName = qualifiedName + "__get_" + prop.name.token_name;
        llvm::Type *returnType = generate_type(*prop.type);

        std::vector<llvm::Type *> paramTypes;
        paramTypes.push_back(llvm::PointerType::get(structType, 0));

        auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        auto *llvmFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, getterName, *module);

        functions[getterName] = llvmFunc;
        def->methodFunctions["get_" + prop.name.token_name] = llvmFunc;
        currentFunction = llvmFunc;

        auto *entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
        builder->SetInsertPoint(entry);

        auto argIt = llvmFunc->arg_begin();
        argIt->setName("this");
        auto *thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, qualifiedName);

        if (prop.getterBody) {
            for (const auto &stmt: prop.getterBody->statements) {
                generate_statement(*stmt);
            }
        } else if (prop.getterExpr) {
            llvm::Value *result = generate_expression(*prop.getterExpr);
            builder->CreateRet(result);
        }

        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }

        pop_scope();
    }

    // Generate setter method only for computed properties (with explicit body/expression)
    // Auto-properties access the field directly
    if (prop.hasSetter && (prop.setterBody || prop.setterExpr)) {
        push_scope();

        const std::string setterName = qualifiedName + "__set_" + prop.name.token_name;
        llvm::Type *valueType = generate_type(*prop.type);

        std::vector<llvm::Type *> paramTypes;
        paramTypes.push_back(llvm::PointerType::get(structType, 0));
        paramTypes.push_back(valueType);

        auto *funcType = llvm::FunctionType::get(builder->getVoidTy(), paramTypes, false);
        auto *llvmFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, setterName, *module);

        functions[setterName] = llvmFunc;
        def->methodFunctions["set_" + prop.name.token_name] = llvmFunc;
        currentFunction = llvmFunc;

        auto *entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
        builder->SetInsertPoint(entry);

        auto argIt = llvmFunc->arg_begin();
        argIt->setName("this");
        auto *thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, qualifiedName);
        ++argIt;

        argIt->setName("value");
        auto *valueAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "value");
        builder->CreateStore(&*argIt, valueAlloca);
        currentScope->define_variable("value", valueAlloca);

        if (prop.setterBody) {
            for (const auto &stmt: prop.setterBody->statements) {
                generate_statement(*stmt);
            }
        } else if (prop.setterExpr) {
            generate_expression(*prop.setterExpr);
        }

        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateRetVoid();
        }

        pop_scope();
    }
}

// Namespace helpers for multi-pass
void Generator::forward_declare_namespace_structs(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name.token_name : prefix + "::" + ns.name.token_name;

    for (const auto &structDecl: ns.structs) {
        forward_declare_struct(*structDecl, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        forward_declare_namespace_structs(*nestedNs, qualifiedPrefix);
    }
}

void Generator::resolve_namespace_struct_bodies(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name.token_name : prefix + "::" + ns.name.token_name;

    for (const auto &structDecl: ns.structs) {
        resolve_struct_body(*structDecl, qualifiedPrefix);
    }

    for (const auto &nestedNs: ns.namespaces) {
        resolve_namespace_struct_bodies(*nestedNs, qualifiedPrefix);
    }
}

void Generator::generate_namespace_struct_methods(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name.token_name : prefix + "::" + ns.name.token_name;

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
    const std::string qualifiedName = prefix.empty()
                                          ? struct_declaration.name.token_name
                                          : prefix + "::" + struct_declaration.name.token_name;

    StructDef def(qualifiedName, struct_declaration.isGeneric());

    // Build field info
    unsigned idx = 0;
    for (const auto &field: struct_declaration.fields) {
        def.fields.emplace_back(field.name.token_name, *field.type);
        def.fieldIndices[field.name.token_name] = idx++;
    }

    // Store methods and properties
    for (const auto &method: struct_declaration.methods) {
        def.methods.push_back(method.get());
    }
    for (const auto &prop: struct_declaration.properties) {
        def.properties.push_back(&prop);
    }

    if (def.isGeneric) {
        def.genericParams = struct_declaration.genericParams;
        currentScope->define_struct(qualifiedName, std::move(def));
        return;
    }

    // Handle transparent types
    if (struct_declaration.isTransparent()) {
        def.isTransparent = true;
        def.transparentUnderlying = generate_type(*struct_declaration.baseType);
        def.llvmType = llvm::StructType::create(*context, {def.transparentUnderlying}, qualifiedName);
        currentScope->define_struct(qualifiedName, std::move(def));

        StructDef *storedDef = currentScope->lookup_struct(qualifiedName);
        for (const auto &method: struct_declaration.methods) {
            generate_method(*method, struct_declaration, storedDef->llvmType);
        }
        return;
    }

    // Regular struct
    std::vector<llvm::Type *> fieldTypes;
    for (const auto &[fieldName, fieldType]: def.fields) {
        fieldTypes.push_back(generate_type(fieldType));
    }

    def.llvmType = llvm::StructType::create(*context, fieldTypes, qualifiedName);
    currentScope->define_struct(qualifiedName, std::move(def));

    StructDef *storedDef = currentScope->lookup_struct(qualifiedName);
    for (const auto &method: struct_declaration.methods) {
        generate_method(*method, struct_declaration, storedDef->llvmType);
    }
}

void Generator::generate_method(const StructMethodDeclaration &method, const StructDeclaration &struc,
                                llvm::StructType *structType) {
    push_scope();

    const auto structName = struc.name.token_name;
    const std::string mangledName = structName + "__" + method.name.token_name;

    llvm::Type *returnType = generate_type(*method.returnType);

    std::vector<llvm::Type *> paramTypes;
    const bool isStatic = method.isStatic();
    if (!isStatic) {
        paramTypes.push_back(llvm::PointerType::get(structType, 0));
    }

    for (const auto &param: method.parameters) {
        paramTypes.push_back(generate_type(*param.type));
    }

    const auto funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    const auto llvmFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, mangledName, *module);

    functions[mangledName] = llvmFunc;

    // Store in struct def
    if (StructDef *def = currentScope->lookup_struct(structName)) {
        def->methodFunctions[method.name.token_name] = llvmFunc;
    }

    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    auto argIt = llvmFunc->arg_begin();

    if (!isStatic) {
        argIt->setName("this");
        auto *thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, structName);
        ++argIt;
    }

    size_t paramIdx = 0;
    while (argIt != llvmFunc->arg_end()) {
        const auto &param = method.parameters[paramIdx];
        argIt->setName(param.name.token_name);

        auto *alloca = builder->CreateAlloca(argIt->getType(), nullptr, param.name.token_name);
        builder->CreateStore(&*argIt, alloca);
        std::string paramStructType = param.type->kind == TypeKind::STRUCT ? param.type->structName : "";
        currentScope->define_variable(param.name.token_name, alloca, paramStructType);

        ++argIt;
        ++paramIdx;
    }

    if (method.body) {
        for (const auto &stmt: method.body->statements) {
            generate_statement(*stmt);
        }
    } else if (method.expression) {
        llvm::Value *result = generate_expression(*method.expression);
        if (!returnType->isVoidTy()) {
            builder->CreateRet(result);
        } else {
            builder->CreateRetVoid();
        }
    }

    if (!builder->GetInsertBlock()->getTerminator()) {
        if (returnType->isVoidTy()) {
            builder->CreateRetVoid();
        } else {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    pop_scope();
}