//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

// ============================================================================
// Monomorphization - creates specialized versions of generic types
// ============================================================================

llvm::StructType *Generator::monomorphize_struct(const std::string &baseName, const std::vector<Type> &typeArgs) {
    // Resolve the base name (handle aliases)
    const std::string qualifiedName = currentScope->resolve_alias(baseName);

    // Check if already monomorphized
    if (StructDef *existing = currentScope->lookup_monomorphized(qualifiedName, typeArgs)) {
        return existing->llvmType;
    }

    // Lookup the generic struct definition
    StructDef *genericDef = currentScope->lookup_struct(qualifiedName);
    if (!genericDef || !genericDef->isGeneric) {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                           "generic struct not found: " + baseName);
    }

    // Create the generic context with type substitutions
    GenericArgs args;
    for (const auto &argType: typeArgs) {
        args.add(argType);
    }
    const GenericContext ctx = GenericContext::create(genericDef->genericParams, args);

    // Generate field types with substituted generics
    std::vector<llvm::Type *> fieldTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    unsigned idx = 0;
    for (const auto &[fieldName, fieldType]: genericDef->fields) {
        Type substituted = ctx.substitute(fieldType);
        fieldTypes.push_back(generate_type(substituted));
        fieldIndices[fieldName] = idx++;
    }

    // Create the mangled name and LLVM struct type
    const std::string mangledName = Mangler::mangle_generic_struct(qualifiedName, typeArgs);
    llvm::StructType *structType = llvm::StructType::create(*context, fieldTypes, mangledName);

    // Create monomorphized StructDef
    StructDef monoDef(mangledName, false);
    monoDef.isMonomorphized = true;
    monoDef.llvmType = structType;
    monoDef.fieldIndices = std::move(fieldIndices);

    // Copy substituted fields
    for (const auto &[fieldName, fieldType]: genericDef->fields) {
        monoDef.fields.emplace_back(fieldName, ctx.substitute(fieldType));
    }

    currentScope->define_struct(mangledName, std::move(monoDef));

    // Save current builder state before generating methods/properties
    llvm::BasicBlock *savedBlock = builder->GetInsertBlock();
    llvm::Function *savedFunction = currentFunction;

    // Generate properties for this monomorphized type
    for (const auto *prop: genericDef->properties) {
        monomorphize_property(*prop, structType, ctx, mangledName);
    }

    // Generate methods for this monomorphized type
    for (const auto *method: genericDef->methods) {
        monomorphize_method(*method, *genericDef, structType, ctx, mangledName);
    }

    // Restore builder state after generating methods/properties
    if (savedBlock) {
        builder->SetInsertPoint(savedBlock);
    }
    currentFunction = savedFunction;

    return structType;
}

void Generator::monomorphize_method(const StructMethodDeclaration &method,
                                    const StructDef &genericDef,
                                    llvm::StructType *monomorphizedType,
                                    const GenericContext &ctx,
                                    const std::string &mangledStructName) {
    push_scope();

    // Method name: MangledStructName__methodName
    const std::string mangledMethodName = mangledStructName + "__" + method.name;

    // Substitute generic types in return type
    Type substitutedReturn = ctx.substitute(*method.returnType);
    llvm::Type *returnType = generate_type(substitutedReturn);

    std::vector<llvm::Type *> paramTypes;

    // Static methods don't have 'this' parameter
    const bool isStatic = method.isStatic();
    if (!isStatic) {
        paramTypes.push_back(llvm::PointerType::get(monomorphizedType, 0));
    }

    // Substitute generic types in parameters
    for (const auto &param: method.parameters) {
        Type substitutedParam = ctx.substitute(*param.type);
        paramTypes.push_back(generate_type(substitutedParam));
    }

    const auto funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    const auto llvmFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        mangledMethodName,
        *module
    );

    functions[mangledMethodName] = llvmFunc;

    // Store method in the monomorphized struct def
    if (StructDef *monoDef = currentScope->lookup_struct(mangledStructName)) {
        monoDef->methodFunctions[method.name] = llvmFunc;
    }

    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    auto argIt = llvmFunc->arg_begin();

    // Define 'this' parameter only for non-static methods
    if (!isStatic) {
        argIt->setName("this");
        auto *thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, mangledStructName);
        ++argIt;
    }

    // Define other parameters
    size_t paramIdx = 0;
    while (argIt != llvmFunc->arg_end()) {
        const auto &param = method.parameters[paramIdx];
        argIt->setName(param.name);

        auto *alloca = builder->CreateAlloca(argIt->getType(), nullptr, param.name);
        builder->CreateStore(&*argIt, alloca);

        Type substitutedParam = ctx.substitute(*param.type);
        std::string paramStructType = substitutedParam.kind == TypeKind::STRUCT
                                          ? substitutedParam.structName
                                          : "";
        currentScope->define_variable(param.name, alloca, paramStructType);

        ++argIt;
        ++paramIdx;
    }

    // Generate method body
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

void Generator::monomorphize_property(const StructProperty &prop,
                                      llvm::StructType *monomorphizedType,
                                      const GenericContext &ctx,
                                      const std::string &mangledStructName) {
    StructDef *monoDef = currentScope->lookup_struct(mangledStructName);
    if (!monoDef) return;

    // Register property info
    PropertyInfo propInfo;
    propInfo.name = prop.name;
    propInfo.hasGetter = prop.hasGetter;
    propInfo.hasSetter = prop.hasSetter;

    // For auto-properties, find the backing field
    if (prop.isAutoProperty()) {
        propInfo.backingFieldName = prop.backingFieldName();
        if (auto it = monoDef->fieldIndices.find(propInfo.backingFieldName); it != monoDef->fieldIndices.end()) {
            propInfo.backingFieldIndex = it->second;
        }
    }

    monoDef->propertyInfos[prop.name] = propInfo;

    // Substitute generic types in property type
    Type substitutedType = ctx.substitute(*prop.type);

    // Generate getter only for computed properties (with explicit body/expression)
    // Auto-properties access the field directly
    if (prop.hasGetter && (prop.getterBody || prop.getterExpr)) {
        push_scope();

        const std::string getterName = mangledStructName + "__get_" + prop.name;
        llvm::Type *returnType = generate_type(substitutedType);

        std::vector<llvm::Type *> paramTypes;
        paramTypes.push_back(llvm::PointerType::get(monomorphizedType, 0));

        auto *funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        auto *llvmFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            getterName,
            *module
        );

        functions[getterName] = llvmFunc;
        monoDef->methodFunctions["get_" + prop.name] = llvmFunc;
        currentFunction = llvmFunc;

        auto *entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
        builder->SetInsertPoint(entry);

        auto argIt = llvmFunc->arg_begin();
        argIt->setName("this");
        auto *thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, mangledStructName);

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

    // Generate setter only for computed properties (with explicit body/expression)
    // Auto-properties access the field directly
    if (prop.hasSetter && (prop.setterBody || prop.setterExpr)) {
        push_scope();

        const std::string setterName = mangledStructName + "__set_" + prop.name;
        llvm::Type *valueType = generate_type(substitutedType);

        std::vector<llvm::Type *> paramTypes;
        paramTypes.push_back(llvm::PointerType::get(monomorphizedType, 0));
        paramTypes.push_back(valueType);

        auto *funcType = llvm::FunctionType::get(builder->getVoidTy(), paramTypes, false);
        auto *llvmFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            setterName,
            *module
        );

        functions[setterName] = llvmFunc;
        monoDef->methodFunctions["set_" + prop.name] = llvmFunc;
        currentFunction = llvmFunc;

        auto *entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
        builder->SetInsertPoint(entry);

        auto argIt = llvmFunc->arg_begin();
        argIt->setName("this");
        auto *thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, mangledStructName);
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

llvm::Type *Generator::generate_type_with_context(const Type &type, const GenericContext *ctx) {
    // If we have a context, try to substitute first
    if (ctx) {
        Type substituted = ctx->substitute(type);
        return generate_type(substituted);
    }
    return generate_type(type);
}

llvm::Type *Generator::generate_type(const Type &type) {
    switch (type.kind) {
        case TypeKind::INTEGER: return builder->getIntNTy(type.size);
        case TypeKind::STRING: return builder->getPtrTy();
        case TypeKind::VOID: return builder->getVoidTy();
        case TypeKind::F16: return builder->getHalfTy();
        case TypeKind::F32: return builder->getFloatTy();
        case TypeKind::F64: return builder->getDoubleTy();
        case TypeKind::F128: return llvm::Type::getFP128Ty(*context);
        case TypeKind::ARRAY: {
            if (!type.elementType) {
                throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo array deve ter tipo de elemento");
            }
            llvm::Type *elemType = generate_type(*type.elementType);
            return llvm::PointerType::get(elemType, 0);
        }
        case TypeKind::POINTER: {
            if (!type.elementType) {
                throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo ponteiro deve ter tipo de elemento");
            }
            llvm::Type *pointeeType = generate_type(*type.elementType);
            return llvm::PointerType::get(pointeeType, 0);
        }
        case TypeKind::STRUCT: {
            // Check for transparent type first
            if (llvm::Type *transparentType = currentScope->get_transparent_type(type.structName)) {
                return transparentType;
            }

            // Handle generic structs with type arguments - trigger monomorphization
            if (type.hasGenericArgs()) {
                return monomorphize_struct(type.structName, type.genericArgs);
            }

            // Check if this is a reference to a generic parameter (unresolved T)
            // This should only happen if we're generating code for a generic struct
            // without proper context - which is an error
            if (currentScope->is_generic(type.structName)) {
                throw CompileError(DiagnosticCode::INVALID_TYPE,
                                   "generic struct '" + type.structName + "' requires type arguments");
            }

            // Regular non-generic struct lookup
            llvm::StructType *structType = currentScope->get_llvm_struct(type.structName);
            if (!structType) {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct not found: " + type.structName);
            }
            return structType;
        }
        case TypeKind::AUTO:
            throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo auto deve ser inferido antes da geração de código");
        default: throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo inválido");
    }
}

llvm::Value *Generator::cast_value(llvm::Value *value, llvm::Type *targetType) const {
    if (!value || !targetType) return value;

    const llvm::Type *srcType = value->getType();
    if (srcType == targetType) return value;

    if (srcType->isIntegerTy() && targetType->isIntegerTy()) {
        const unsigned srcBits = srcType->getIntegerBitWidth();
        const unsigned dstBits = targetType->getIntegerBitWidth();

        if (srcBits < dstBits) {
            return builder->CreateSExt(value, targetType, "sext");
        }
        if (srcBits > dstBits) {
            return builder->CreateTrunc(value, targetType, "trunc");
        }
    }

    if (srcType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        if (srcType->getPrimitiveSizeInBits() < targetType->getPrimitiveSizeInBits()) {
            return builder->CreateFPExt(value, targetType, "fpext");
        }
        return builder->CreateFPTrunc(value, targetType, "fptrunc");
    }

    if (srcType->isIntegerTy() && targetType->isFloatingPointTy()) {
        return builder->CreateSIToFP(value, targetType, "sitofp");
    }

    if (srcType->isFloatingPointTy() && targetType->isIntegerTy()) {
        return builder->CreateFPToSI(value, targetType, "fptosi");
    }

    return value;
}