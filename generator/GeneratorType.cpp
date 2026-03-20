//
// Created by Luke on 06/12/2025.
//

#include <llvm/Pass.h>

#include "Generator.h"
#include "../utils/Logger.h"


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
    const GenericContext* savedGenericCtx = _currentGenericCtx;
    _currentGenericCtx = &ctx;

    LOG_DEBUG("[monomorphize_struct] '%s' — ctx params: {", qualifiedName.c_str());
    for (const auto& [paramName, paramType] : ctx.substitutions)
    {
        LOG_DEBUG("[monomorphize_struct]   '%s' -> '%s'", paramName.c_str(), paramType.toHumanString().c_str());
    }
    LOG_DEBUG("[monomorphize_struct] } savedCtx=%p newCtx=%p", (void*)savedGenericCtx, (void*)&ctx);

    LOG_DEBUG("[monomorphize_struct] genericDef=%p fields=%zu methods=%zu", (void*)genericDef,
              genericDef->fields.size(), genericDef->methods.size());
    for (size_t fi = 0; fi < genericDef->fields.size(); ++fi)
    {
        LOG_DEBUG("[monomorphize_struct]   field[%zu]: name='%s' type='%s'", fi,
                  genericDef->fields[fi].first.c_str(),
                  genericDef->fields[fi].second.toHumanString().c_str());
    }

    // Validate generic constraints
    validate_generic_constraints(genericDef->genericParams, args, baseName);

    std::vector<llvm::Type*> fieldTypes;
    std::unordered_map<std::string, unsigned> fieldIndices;

    unsigned idx = 0;
    for (const auto& [fieldName, fieldType] : genericDef->fields)
    {
        Type substituted = ctx.substitute(fieldType);
        fieldTypes.push_back(generate_type(substituted));
        fieldIndices[fieldName] = idx++;
    }

    LOG_DEBUG("[monomorphize_struct] field loop done, building monoDef");

    const std::string mangledName = Mangler::mangle_generic_struct(qualifiedName, typeArgs);
    llvm::StructType* structType = llvm::StructType::create(*context, fieldTypes, mangledName);

    StructDef monoDef(mangledName, false);
    monoDef.isMonomorphized = true;
    monoDef.llvmType = structType;
    monoDef.fieldIndices = std::move(fieldIndices);
    monoDef.attributes = genericDef->attributes;

    for (const auto& [fieldName, fieldType] : genericDef->fields)
    {
        monoDef.fields.emplace_back(fieldName, ctx.substitute(fieldType));
    }

    // Copy properties and methods BEFORE define_struct, because inserting into
    // the unordered_map can trigger a rehash that invalidates the genericDef pointer.
    auto properties = genericDef->properties;
    auto methods = genericDef->methods;

    LOG_DEBUG("[monomorphize_struct] about to define_struct '%s'", mangledName.c_str());
    currentScope->define_struct(mangledName, std::move(monoDef));
    LOG_DEBUG("[monomorphize_struct] define_struct done");
    // NOTE: genericDef is potentially INVALID after this point due to unordered_map rehash!

    llvm::BasicBlock* savedBlock = builder->GetInsertBlock();
    llvm::Function* savedFunction = currentFunction;

    // Save async/coroutine state — monomorphized method bodies must not
    // inherit the async state of the calling function (e.g., Console.write)
    const bool savedInAsync = inAsyncFunction;
    llvm::Value* savedAsyncCoroId = asyncCoroId;
    llvm::Value* savedAsyncCoroHandle = asyncCoroHandle;
    llvm::Value* savedAsyncPromisePtr = asyncPromisePtr;
    llvm::BasicBlock* savedAsyncFinalSuspendBB = asyncFinalSuspendBB;
    llvm::BasicBlock* savedAsyncCleanupBB = asyncCleanupBB;
    llvm::BasicBlock* savedAsyncSuspendBB = asyncSuspendBB;
    llvm::Type* savedAsyncReturnType = asyncReturnType;

    inAsyncFunction = false;
    asyncCoroId = nullptr;
    asyncCoroHandle = nullptr;
    asyncPromisePtr = nullptr;
    asyncFinalSuspendBB = nullptr;
    asyncCleanupBB = nullptr;
    asyncSuspendBB = nullptr;
    asyncReturnType = nullptr;

    LOG_DEBUG("[monomorphize_struct] starting properties (%zu)", properties.size());
    for (const auto& prop : properties)
    {
        monomorphize_property(*prop, structType, ctx, mangledName);
    }

    // Two-pass approach: forward-declare all methods first, then generate bodies.
    // This allows methods to call each other regardless of declaration order.

    // Pass 1: Forward-declare all method functions
    LOG_DEBUG("[monomorphize_struct] starting forward_declare for %zu methods", methods.size());
    for (const auto& method : methods)
    {
        LOG_DEBUG("[monomorphize_struct]   forward_declare method '%s' ret='%s' params=%zu",
                  method->name.c_str(), method->returnType.toHumanString().c_str(), method->paramTypes.size());
        forward_declare_monomorphized_method(*method, structType, ctx, mangledName);
    }

    // Pass 2: Generate method bodies
    for (const auto& method : methods)
    {
        monomorphize_method(*method, structType, ctx, mangledName);
    }

    if (savedBlock)
    {
        builder->SetInsertPoint(savedBlock);
    }
    currentFunction = savedFunction;

    // Restore async/coroutine state
    inAsyncFunction = savedInAsync;
    asyncCoroId = savedAsyncCoroId;
    asyncCoroHandle = savedAsyncCoroHandle;
    asyncPromisePtr = savedAsyncPromisePtr;
    asyncFinalSuspendBB = savedAsyncFinalSuspendBB;
    asyncCleanupBB = savedAsyncCleanupBB;
    asyncSuspendBB = savedAsyncSuspendBB;
    asyncReturnType = savedAsyncReturnType;

    LOG_DEBUG("[monomorphize_struct] restoring ctx: %p -> %p (was processing '%s')",
              (void*)_currentGenericCtx, (void*)savedGenericCtx, qualifiedName.c_str());
    _currentGenericCtx = savedGenericCtx;

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

    // Validate generic constraints
    validate_generic_constraints(genericDef->genericParams, args, baseName);

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

void Generator::validate_generic_constraints(const GenericParams& params, const GenericArgs& args,
                                             const std::string& contextName)
{
    for (size_t i = 0; i < params.size() && i < args.size(); ++i)
    {
        const auto& param = params.params[i];
        if (param.constraints.empty()) continue;

        const auto& argType = args.args[i];
        for (const auto& constraintName : param.constraints)
        {
            if (!type_satisfies_constraint(argType, constraintName))
            {
                GENERATOR_ERROR(DiagnosticCode::GENERIC_CONSTRAINT_VIOLATION,
                                "type '" + argType.toHumanString() + "' does not satisfy constraint '" +
                                constraintName + "' required by generic parameter '" +
                                param.name.token_name + "' in '" + contextName + "'",
                                argType.location);
            }
        }
    }
}

bool Generator::type_satisfies_constraint(const Type& type, const std::string& interfaceName)
{
    // For struct types, look up by struct name; for primitives, use the human-readable
    // name to find synthetic structs created by impl blocks (e.g. "impl Hashable for i32")
    const std::string typeName = (type.kind == TypeKind::STRUCT)
                                     ? type.structName
                                     : type.toHumanString();

    const auto structSym = symbols->lookupStruct(typeName);
    if (!structSym) return false;

    for (const auto& impl : structSym->implements)
    {
        if (impl == interfaceName) return true;
    }
    return false;
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

    if (method.hasAttribute("force-inline"))
    {
        llvmFunc->addFnAttr(llvm::Attribute::AlwaysInline);
    }

    functions[mangledMethodName] = llvmFunc;
    if (StructDef* monoDef = currentScope->lookup_struct(mangledStructName))
    {
        monoDef->methodFunctions[method.name] = llvmFunc;
        monoDef->methodFunctions[mangledMethodName] = llvmFunc;
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
        if (substitutedParam.kind == TypeKind::INTEGER)
        {
            currentScope->set_variable_signed(paramName, substitutedParam.sign);
        }

        ++argIt;
        ++paramIdx;
    }

    const GenericContext* savedGenericCtx = _currentGenericCtx;
    _currentGenericCtx = &ctx;

    LOG_DEBUG("[monomorphize_method] '%s' on '%s' — ctx params: {", method.name.c_str(), mangledStructName.c_str());
    for (const auto& [paramName, paramType] : ctx.substitutions)
    {
        LOG_DEBUG("[monomorphize_method]   '%s' -> '%s'", paramName.c_str(), paramType.toHumanString().c_str());
    }
    LOG_DEBUG("[monomorphize_method] } savedCtx=%p newCtx=%p", (void*)savedGenericCtx, (void*)&ctx);

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

    LOG_DEBUG("[monomorphize_method] restoring ctx: %p -> %p (was method '%s')",
              (void*)_currentGenericCtx, (void*)savedGenericCtx, method.name.c_str());
    _currentGenericCtx = savedGenericCtx;

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

    const GenericContext* savedGenericCtx = _currentGenericCtx;
    _currentGenericCtx = &ctx;

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

    _currentGenericCtx = savedGenericCtx;
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
            // Check if this is a generic parameter that needs substitution (e.g., Key, Value in method bodies)
            if (_currentGenericCtx)
            {
                if (const Type* resolved = _currentGenericCtx->resolve(type.structName))
                {
                    LOG_DEBUG("[generate_type] STRUCT '%s' resolved via ctx to '%s'",
                              type.structName.c_str(), resolved->toHumanString().c_str());
                    return generate_type(*resolved);
                }
                else
                {
                    LOG_DEBUG("[generate_type] STRUCT '%s' NOT in ctx (ctx=%p, has %zu entries)",
                              type.structName.c_str(), (void*)_currentGenericCtx,
                              _currentGenericCtx->substitutions.size());
                    for (const auto& [k, v] : _currentGenericCtx->substitutions)
                    {
                        LOG_DEBUG("[generate_type]   ctx entry: '%s' -> '%s'", k.c_str(), v.toHumanString().c_str());
                    }
                }
            }
            else
            {
                LOG_DEBUG("[generate_type] STRUCT '%s' — no ctx active", type.structName.c_str());
            }

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
                LOG_DEBUG("[generate_type] STRUCT NOT FOUND: '%s' — _currentGenericCtx=%p",
                          type.structName.c_str(), (void*)_currentGenericCtx);
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

llvm::Value* Generator::cast_value(llvm::Value* value, llvm::Type* targetType, bool isSigned) const
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
            if (isSigned)
                return builder->CreateSExt(value, targetType);
            else
                return builder->CreateZExt(value, targetType);
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
        return isSigned
                   ? builder->CreateSIToFP(value, targetType, "sitofp")
                   : builder->CreateUIToFP(value, targetType, "uitofp");
    }

    if (srcType->isFloatingPointTy() && targetType->isIntegerTy())
    {
        return isSigned
                   ? builder->CreateFPToSI(value, targetType, "fptosi")
                   : builder->CreateFPToUI(value, targetType, "fptoui");
    }

    // Load struct from alloca when target expects struct by value
    if (srcType->isPointerTy() && targetType->isStructTy())
    {
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
        {
            if (alloca->getAllocatedType() == targetType)
            {
                return builder->CreateLoad(targetType, alloca, "struct_load");
            }
        }
    }

    if (srcType->isPointerTy() && targetType->isPointerTy())
    {
        return builder->CreatePointerCast(value, targetType, "ptrcast");
    }

    if (srcType->isPointerTy() && targetType->isIntegerTy())
    {
        return builder->CreatePtrToInt(value, targetType, "ptrtoint");
    }

    if (srcType->isIntegerTy() && targetType->isPointerTy())
    {
        return builder->CreateIntToPtr(value, targetType, "inttoptr");
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