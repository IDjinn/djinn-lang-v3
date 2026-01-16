//
// Created by Luke on 02/01/2026.
//
#include "../Generator.h"


void Generator::forward_declare_struct(const StructSymbol &struct_symbol) {
    return;
    // StructDef def(qualifiedName, struct_symbol.isGeneric());
    // def.isTransparent = struct_symbol.isTransparent();
    // def.genericParams = struct_symbol.genericParams;
    //
    // unsigned idx = 0;
    // for (const auto &field: struct_symbol.fields) {
    //     def.fields.emplace_back(field.name.token_name, *field.type);
    //     def.fieldIndices[field.name.token_name] = idx++;
    // }
    //
    // for (const auto &method: struct_symbol.methods) {
    //     def.methods.push_back(method.get());
    // }
    // for (const auto &prop: struct_symbol.properties) {
    //     def.properties.push_back(&prop);
    // }
    //
    // if (struct_symbol.isTransparent()) {
    //     def.transparentUnderlying = generate_type(*struct_symbol.baseType);
    //     def.llvmType = llvm::StructType::create(*context, qualifiedName);
    // } else {
    //     def.llvmType = llvm::StructType::create(*context, qualifiedName);
    // }
    //
    // if (!def.isGeneric) {
    //     declaredTypes.push_back(def.llvmType);
    // }
    //
    // currentScope->define_struct(qualifiedName, std::move(def));
}

void Generator::resolve_struct_body(const StructDeclaration &struct_declaration, const std::string &prefix) {
    // const std::string qualifiedName = prefix.empty()
    //                                       ? struct_declaration.name.token_name
    //                                       : prefix + "::" + struct_declaration.name.token_name;
    //
    // StructDef *def = currentScope->lookup_struct(qualifiedName);
    // if (!def) {
    //     throw std::runtime_error("Struct not forward declared: " + qualifiedName);
    // }
    //
    // if (def->isGeneric) {
    //     return;
    // }
    //
    // if (def->isTransparent) {
    //     if (def->llvmType && def->llvmType->isOpaque()) {
    //         def->llvmType->setBody({def->transparentUnderlying});
    //     }
    //     return;
    // }
    //
    // std::vector<llvm::Type *> fieldTypes;
    // for (const auto &[fieldName, fieldType]: def->fields) {
    //     fieldTypes.push_back(generate_type(fieldType));
    // }
    //
    // def->llvmType->setBody(fieldTypes);
}

void Generator::generate_struct_methods(const StructDeclaration &struct_declaration, const std::string &prefix) {
    if (struct_declaration.isGeneric()) {
        return;
    }

    const std::string qualifiedName = prefix.empty()
                                          ? struct_declaration.name.token_name
                                          : prefix + "::" + struct_declaration.name.token_name;

    const StructDef *def = currentScope->lookup_struct(qualifiedName);
    if (!def || !def->llvmType) {
        throw std::runtime_error("Struct not found for method generation: " + qualifiedName);
    }

    for (const auto &prop: struct_declaration.properties) {
        generate_property(prop, struct_declaration, def->llvmType, qualifiedName);
    }

    for (const auto &method: struct_declaration.methods) {
        generate_method(*method, struct_declaration, def->llvmType);
    }
}

void Generator::generate_property(const StructProperty &prop, const StructDeclaration &struc,
                                  llvm::StructType *structType, const std::string &qualifiedName) {
    StructDef *def = currentScope->lookup_struct(qualifiedName);
    if (!def) return;

    PropertyInfo propInfo;
    propInfo.name = prop.name.token_name;
    propInfo.hasGetter = prop.hasGetter;
    propInfo.hasSetter = prop.hasSetter;

    if (prop.isAutoProperty()) {
        propInfo.backingFieldName = prop.backingFieldName();
        if (const auto it = def->fieldIndices.find(propInfo.backingFieldName); it != def->fieldIndices.end()) {
            propInfo.backingFieldIndex = it->second;
        }
    }

    def->propertyInfos[prop.name.token_name] = propInfo;

    if (prop.hasGetter && (prop.getterBody || prop.getterExpr)) {
        push_scope();

        const auto getterName = qualifiedName + "__get_" + prop.name.token_name;
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

        const auto argIt = llvmFunc->arg_begin();
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

    if (prop.hasSetter && (prop.setterBody || prop.setterExpr)) {
        push_scope();

        const auto setterName = qualifiedName + "__set_" + prop.name.token_name;
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

void Generator::forward_declare_namespace_structs(const NamespaceDeclaration &ns, const std::string &prefix) {
    const std::string qualifiedPrefix = prefix.empty() ? ns.name.token_name : prefix + "::" + ns.name.token_name;

    for (const auto &structDecl: ns.structs) {
        // forward_declare_struct(TODO);
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


void Generator::generate_struct(const StructDeclaration &struct_declaration, const std::string &prefix) {
    // const std::string qualifiedName = prefix.empty()
    //                                       ? struct_declaration.name.token_name
    //                                       : prefix + "::" + struct_declaration.name.token_name;
    //
    // StructDef def(qualifiedName, struct_declaration.isGeneric());
    //
    // unsigned idx = 0;
    // for (const auto &field: struct_declaration.fields) {
    //     def.fields.emplace_back(field.name.token_name, *field.type);
    //     def.fieldIndices[field.name.token_name] = idx++;
    // }
    //
    // for (const auto &method: struct_declaration.methods) {
    //     def.methods.push_back(method.get());
    // }
    // for (const auto &prop: struct_declaration.properties) {
    //     def.properties.push_back(&prop);
    // }
    //
    // if (def.isGeneric) {
    //     def.genericParams = struct_declaration.genericParams;
    //     currentScope->define_struct(qualifiedName, std::move(def));
    //     return;
    // }
    //
    // if (struct_declaration.isTransparent()) {
    //     def.isTransparent = true;
    //     def.transparentUnderlying = generate_type(*struct_declaration.baseType);
    //     def.llvmType = llvm::StructType::create(*context, {def.transparentUnderlying}, qualifiedName);
    //     currentScope->define_struct(qualifiedName, std::move(def));
    //
    //     const StructDef *storedDef = currentScope->lookup_struct(qualifiedName);
    //     for (const auto &method: struct_declaration.methods) {
    //         generate_method(*method, struct_declaration, storedDef->llvmType);
    //     }
    //     return;
    // }
    //
    // std::vector<llvm::Type *> fieldTypes;
    // for (const auto &[fieldName, fieldType]: def.fields) {
    //     fieldTypes.push_back(generate_type(fieldType));
    // }
    //
    // def.llvmType = llvm::StructType::create(*context, fieldTypes, qualifiedName);
    // currentScope->define_struct(qualifiedName, std::move(def));
    //
    // const StructDef *storedDef = currentScope->lookup_struct(qualifiedName);
    // for (const auto &method: struct_declaration.methods) {
    //     generate_method(*method, struct_declaration, storedDef->llvmType);
    // }
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