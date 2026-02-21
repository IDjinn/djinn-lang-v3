//
// Created by Luke on 06/12/2025.
//

#include <llvm/Pass.h>

#include "Generator.h"


llvm::StructType* Generator::monomorphize_struct(const std::string& baseName, const std::vector<Type>& typeArgs)
{
    const auto qualifiedName = currentScope->resolve_alias(baseName);
    if (StructDef* existing = currentScope->lookup_monomorphized(qualifiedName, typeArgs))
    {
        return existing->llvmType;
    }

    StructDef* genericDef = currentScope->lookup_struct(qualifiedName);
    if (!genericDef || !genericDef->isGeneric)
    {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                           "generic struct not found: " + baseName);
    }

    GenericArgs args;
    for (const auto& argType : typeArgs)
    {
        args.add(argType);
    }
    const GenericContext ctx = GenericContext::create(genericDef->genericParams, args);

    std::vector<llvm::Type*> fieldTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    unsigned idx = 0;
    for (const auto& [fieldName, fieldType] : genericDef->fields)
    {
        Type substituted = ctx.substitute(fieldType);
        fieldTypes.push_back(generate_type(substituted));
        fieldIndices[fieldName] = idx++;
    }

    const std::string mangledName = Mangler::mangle_generic_struct(qualifiedName, typeArgs);
    llvm::StructType* structType = llvm::StructType::create(*context, fieldTypes, mangledName);

    StructDef monoDef(mangledName, false);
    monoDef.isMonomorphized = true;
    monoDef.llvmType = structType;
    monoDef.fieldIndices = std::move(fieldIndices);

    for (const auto& [fieldName, fieldType] : genericDef->fields)
    {
        monoDef.fields.emplace_back(fieldName, ctx.substitute(fieldType));
    }

    currentScope->define_struct(mangledName, std::move(monoDef));

    llvm::BasicBlock* savedBlock = builder->GetInsertBlock();
    llvm::Function* savedFunction = currentFunction;

    for (const auto& prop : genericDef->properties)
    {
        monomorphize_property(*prop, structType, ctx, mangledName);
    }

    // Two-pass approach: forward-declare all methods first, then generate bodies.
    // This allows methods to call each other regardless of declaration order.

    // Pass 1: Forward-declare all method functions
    for (const auto& method : genericDef->methods)
    {
        forward_declare_monomorphized_method(*method, structType, ctx, mangledName);
    }

    // Pass 2: Generate method bodies
    for (const auto& method : genericDef->methods)
    {
        monomorphize_method(*method, structType, ctx, mangledName);
    }

    if (savedBlock)
    {
        builder->SetInsertPoint(savedBlock);
    }
    currentFunction = savedFunction;

    return structType;
}

llvm::StructType* Generator::monomorphize_enum(const std::string& baseName, const std::vector<Type>& typeArgs)
{
    const auto qualifiedName = currentScope->resolve_alias(baseName);
    if (EnumDef* existing = currentScope->lookup_monomorphized_enum(qualifiedName, typeArgs))
    {
        return existing->llvmType;
    }

    EnumDef* genericDef = currentScope->lookup_enum(qualifiedName);
    if (!genericDef || !genericDef->isGeneric)
    {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                           "generic enum not found: " + baseName);
    }

    GenericArgs args;
    for (const auto& argType : typeArgs)
    {
        args.add(argType);
    }
    const GenericContext ctx = GenericContext::create(genericDef->genericParams, args);

    size_t maxPayloadSize = 0;
    for (const auto& variant : genericDef->variants)
    {
        size_t variantPayloadSize = 0;

        for (const auto& type : variant.associatedTypes)
        {
            Type substituted = ctx.substitute(type);
            if (llvm::Type* llvmType = generate_type(substituted))
            {
                const auto& dataLayout = module->getDataLayout();
                variantPayloadSize += dataLayout.getTypeAllocSize(llvmType);
            }
        }

        maxPayloadSize = std::max(maxPayloadSize, variantPayloadSize);
    }

    const size_t variantCount = genericDef->variants.size();
    llvm::Type* tagType;
    if (variantCount <= 256)
    {
        tagType = builder->getInt8Ty();
    }
    else if (variantCount <= 65536)
    {
        tagType = builder->getInt16Ty();
    }
    else
    {
        tagType = builder->getInt32Ty();
    }

    std::vector<llvm::Type*> enumFields;
    enumFields.push_back(tagType);

    if (maxPayloadSize > 0)
    {
        enumFields.push_back(llvm::ArrayType::get(builder->getInt8Ty(), maxPayloadSize));
    }

    const std::string mangledName = Mangler::mangle_generic_enum(qualifiedName, typeArgs);
    llvm::StructType* enumType = llvm::StructType::create(*context, enumFields, mangledName);

    EnumDef monoDef(mangledName, false);
    monoDef.isMonomorphized = true;
    monoDef.llvmType = enumType;
    monoDef.tagType = tagType;
    monoDef.maxPayloadSize = maxPayloadSize;

    for (const auto& variant : genericDef->variants)
    {
        std::vector<Type> substitutedTypes;
        for (const auto& type : variant.associatedTypes)
        {
            substitutedTypes.push_back(ctx.substitute(type));
        }
        monoDef.addVariant(variant.name.token_name, substitutedTypes);
    }

    currentScope->define_enum(mangledName, std::move(monoDef));

    return enumType;
}

void Generator::forward_declare_monomorphized_method(const MethodSymbol& method,
                                                     llvm::StructType* monomorphizedType,
                                                     const GenericContext& ctx,
                                                     const std::string& mangledStructName)
{
    const auto mangledMethodName = mangledStructName + "__" + method.name;

    // Skip if already declared
    if (functions.count(mangledMethodName)) return;

    Type substitutedReturn = ctx.substitute(method.returnType);
    llvm::Type* returnType = generate_type(substitutedReturn);

    std::vector<llvm::Type*> paramTypes;
    if (!method.isStatic)
    {
        paramTypes.push_back(llvm::PointerType::get(monomorphizedType, 0));
    }

    for (const auto& paramType : method.paramTypes)
    {
        Type substitutedParam = ctx.substitute(paramType);
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
    if (StructDef* monoDef = currentScope->lookup_struct(mangledStructName))
    {
        monoDef->methodFunctions[method.name] = llvmFunc;
    }
}

void Generator::monomorphize_method(const MethodSymbol& method,
                                    llvm::StructType* monomorphizedType,
                                    const GenericContext& ctx,
                                    const std::string& mangledStructName)
{
    push_scope();

    const auto mangledMethodName = mangledStructName + "__" + method.name;

    // Function was already forward-declared
    llvm::Function* llvmFunc = functions[mangledMethodName];
    Type substitutedReturn = ctx.substitute(method.returnType);
    llvm::Type* returnType = generate_type(substitutedReturn);
    const bool isStatic = method.isStatic;

    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    auto argIt = llvmFunc->arg_begin();
    if (!isStatic)
    {
        argIt->setName("this");
        auto* thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, mangledStructName);
        ++argIt;
    }

    size_t paramIdx = 0;
    while (argIt != llvmFunc->arg_end())
    {
        const auto& paramName = method.paramNames[paramIdx];
        const auto& paramType = method.paramTypes[paramIdx];
        argIt->setName(paramName);

        auto* alloca = builder->CreateAlloca(argIt->getType(), nullptr, paramName);
        builder->CreateStore(&*argIt, alloca);

        Type substitutedParam = ctx.substitute(paramType);
        std::string paramStructType = substitutedParam.kind == TypeKind::STRUCT
                                          ? substitutedParam.structName
                                          : "";
        currentScope->define_variable(paramName, alloca, paramStructType);

        ++argIt;
        ++paramIdx;
    }

    _currentGenericCtx = &ctx;

    if (method.body)
    {
        for (const auto& stmt : method.body->statements)
        {
            generate_statement(*stmt);
        }
    }
    else if (method.expressionBody)
    {
        llvm::Value* result = generate_expression(*method.expressionBody);
        if (!returnType->isVoidTy())
        {
            builder->CreateRet(result);
        }
        else
        {
            builder->CreateRetVoid();
        }
    }

    _currentGenericCtx = nullptr;

    if (!builder->GetInsertBlock()->getTerminator())
    {
        emit_scope_cleanup();
        if (returnType->isVoidTy())
        {
            builder->CreateRetVoid();
        }
        else
        {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    pop_scope();
}

void Generator::monomorphize_property(const PropertySymbol& prop,
                                      llvm::StructType* monomorphizedType,
                                      const GenericContext& ctx,
                                      const std::string& mangledStructName)
{
    StructDef* monoDef = currentScope->lookup_struct(mangledStructName);
    if (!monoDef) return;

    PropertyInfo propInfo;
    propInfo.name = prop.name;
    propInfo.hasGetter = prop.hasGetter;
    propInfo.hasSetter = prop.hasSetter;

    if (prop.isAutoProperty())
    {
        propInfo.backingFieldName = prop.backingFieldName();
        if (auto it = monoDef->fieldIndices.find(propInfo.backingFieldName); it != monoDef->fieldIndices.end())
        {
            propInfo.backingFieldIndex = it->second;
        }
    }

    monoDef->propertyInfos[prop.name] = propInfo;
    Type substitutedType = ctx.substitute(prop.type);

    if (prop.hasGetter && (prop.getterBody || prop.getterExpr))
    {
        push_scope();

        const std::string getterName = mangledStructName + "__get_" + prop.name;
        llvm::Type* returnType = generate_type(substitutedType);

        std::vector<llvm::Type*> paramTypes;
        paramTypes.push_back(llvm::PointerType::get(monomorphizedType, 0));

        auto* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        auto* llvmFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            getterName,
            *module
        );

        functions[getterName] = llvmFunc;
        monoDef->methodFunctions["get_" + prop.name] = llvmFunc;
        currentFunction = llvmFunc;

        auto* entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
        builder->SetInsertPoint(entry);

        auto argIt = llvmFunc->arg_begin();
        argIt->setName("this");
        auto* thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, mangledStructName);

        if (prop.getterBody)
        {
            for (const auto& stmt : prop.getterBody->statements)
            {
                generate_statement(*stmt);
            }
        }
        else if (prop.getterExpr)
        {
            llvm::Value* result = generate_expression(*prop.getterExpr);
            builder->CreateRet(result);
        }

        if (!builder->GetInsertBlock()->getTerminator())
        {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }

        pop_scope();
    }

    if (prop.hasSetter && (prop.setterBody || prop.setterExpr))
    {
        push_scope();

        const std::string setterName = mangledStructName + "__set_" + prop.name;
        llvm::Type* valueType = generate_type(substitutedType);

        std::vector<llvm::Type*> paramTypes;
        paramTypes.push_back(llvm::PointerType::get(monomorphizedType, 0));
        paramTypes.push_back(valueType);

        auto* funcType = llvm::FunctionType::get(builder->getVoidTy(), paramTypes, false);
        auto* llvmFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            setterName,
            *module
        );

        functions[setterName] = llvmFunc;
        monoDef->methodFunctions["set_" + prop.name] = llvmFunc;
        currentFunction = llvmFunc;

        auto* entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
        builder->SetInsertPoint(entry);

        auto argIt = llvmFunc->arg_begin();
        argIt->setName("this");
        auto* thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, mangledStructName);
        ++argIt;

        argIt->setName("value");
        auto* valueAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "value");
        builder->CreateStore(&*argIt, valueAlloca);
        currentScope->define_variable("value", valueAlloca);

        if (prop.setterBody)
        {
            for (const auto& stmt : prop.setterBody->statements)
            {
                generate_statement(*stmt);
            }
        }
        else if (prop.setterExpr)
        {
            generate_expression(*prop.setterExpr);
        }

        if (!builder->GetInsertBlock()->getTerminator())
        {
            builder->CreateRetVoid();
        }

        pop_scope();
    }
}

llvm::Type* Generator::generate_type_with_context(const Type& type, const GenericContext* ctx)
{
    if (ctx)
    {
        const auto substituted = ctx->substitute(type);
        return generate_type(substituted);
    }
    return generate_type(type);
}

llvm::Type* Generator::generate_type(const Type& type)
{
    switch (type.kind)
    {
    case TypeKind::INTEGER: return builder->getIntNTy(type.size);
    case TypeKind::VOID: return builder->getVoidTy();
    case TypeKind::F16: return builder->getHalfTy();
    case TypeKind::F32: return builder->getFloatTy();
    case TypeKind::F64: return builder->getDoubleTy();
    case TypeKind::F128: return llvm::Type::getFP128Ty(*context);
    case TypeKind::ARRAY:
        {
            if (!type.elementType)
            {
                throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo array deve ter tipo de elemento");
            }
            // Try to monomorphize arr<T> slice
            const std::string arrName = currentScope->resolve_alias("arr");
            if (currentScope->lookup_struct(arrName))
            {
                std::vector<Type> typeArgs = {*type.elementType};
                return monomorphize_struct(arrName, typeArgs);
            }
            // Fallback: raw pointer
            llvm::Type* elemType = generate_type(*type.elementType);
            return llvm::PointerType::get(elemType, 0);
        }
    case TypeKind::POINTER:
        {
            if (!type.elementType)
            {
                throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo ponteiro deve ter tipo de elemento");
            }
            if (type.elementType->kind == TypeKind::VOID)
            {
                return builder->getPtrTy();
            }
            llvm::Type* pointeeType = generate_type(*type.elementType);
            return llvm::PointerType::get(pointeeType, 0);
        }
    case TypeKind::STRUCT:
        {
            if (llvm::Type* transparentType = currentScope->get_transparent_type(type.structName))
            {
                return transparentType;
            }

            if (const EnumDef* enumDef = currentScope->lookup_enum(type.structName))
            {
                if (type.hasGenericArgs())
                {
                    return monomorphize_enum(type.structName, type.genericArgs);
                }

                if (enumDef->llvmType)
                {
                    return enumDef->llvmType;
                }

                if (enumDef->isGeneric)
                {
                    throw CompileError(DiagnosticCode::INVALID_TYPE,
                                       "generic enum '" + type.structName + "' requires type arguments");
                }
            }

            if (type.hasGenericArgs())
            {
                return monomorphize_struct(type.structName, type.genericArgs);
            }

            if (currentScope->is_generic(type.structName))
            {
                throw CompileError(DiagnosticCode::INVALID_TYPE,
                                   "generic struct '" + type.structName + "' requires type arguments");
            }

            llvm::StructType* structType = currentScope->get_llvm_struct(type.structName);
            if (!structType)
            {
                GENERATOR_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "struct not found: " + type.structName,
                                type.location);
            }
            return structType;
        }
    case TypeKind::AUTO:
        throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo auto deve ser inferido antes da geração de código");
    default: throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo inválido");
    }
}

llvm::Value* Generator::cast_value(llvm::Value* value, llvm::Type* targetType) const
{
    if (!value || !targetType) return value;

    const llvm::Type* srcType = value->getType();
    if (srcType == targetType) return value;

    if (srcType->isIntegerTy() && targetType->isIntegerTy())
    {
        const unsigned srcBits = srcType->getIntegerBitWidth();
        const unsigned dstBits = targetType->getIntegerBitWidth();

        if (srcBits < dstBits)
        {
            return builder->CreateSExt(value, targetType, "sext");
        }
        if (srcBits > dstBits)
        {
            return builder->CreateTrunc(value, targetType, "trunc");
        }
    }

    if (srcType->isFloatingPointTy() && targetType->isFloatingPointTy())
    {
        if (srcType->getPrimitiveSizeInBits() < targetType->getPrimitiveSizeInBits())
        {
            return builder->CreateFPExt(value, targetType, "fpext");
        }
        return builder->CreateFPTrunc(value, targetType, "fptrunc");
    }

    if (srcType->isIntegerTy() && targetType->isFloatingPointTy())
    {
        return builder->CreateSIToFP(value, targetType, "sitofp");
    }

    if (srcType->isFloatingPointTy() && targetType->isIntegerTy())
    {
        return builder->CreateFPToSI(value, targetType, "fptosi");
    }

    return value;
}

bool Generator::is_slice_struct(llvm::StructType* st)
{
    if (!st || !st->hasName()) return false;
    auto name = st->getName();
    // Match both short names (str, _ZN3arr...) and qualified names (std::types::str, _ZN15std::types::arrI...E)
    if (name == "str" || name == "std::types::str" || name.ends_with("::str"))
        return true;
    // Mangled arr<T>: _ZN3arrI...E (unqualified) or _ZN15std::types::arrI...E (qualified)
    if (name.starts_with("_ZN") && name.contains("arrI"))
        return true;
    return false;
}

llvm::Value* Generator::coerce_str_to_ptr(llvm::Value* value)
{
    // Case 1: alloca to slice struct (str, arr<T>)
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
    {
        if (auto* st = llvm::dyn_cast<llvm::StructType>(alloca->getAllocatedType()))
        {
            if (Generator::is_slice_struct(st))
            {
                auto* gep = builder->CreateStructGEP(st, alloca, 0, "slice.data");
                return builder->CreateLoad(builder->getPtrTy(), gep, "slice_ptr");
            }
        }
    }
    // Case 2: loaded struct value (from generate_identifier)
    if (auto* st = llvm::dyn_cast<llvm::StructType>(value->getType()))
    {
        if (Generator::is_slice_struct(st))
        {
            auto* tmp = builder->CreateAlloca(st, nullptr, "slice_tmp");
            builder->CreateStore(value, tmp);
            auto* gep = builder->CreateStructGEP(st, tmp, 0, "slice.data");
            return builder->CreateLoad(builder->getPtrTy(), gep, "slice_ptr");
        }
    }
    return value;
}