//
// Created by Luke on 02/01/2026.
//
#include "../Generator.h"


void Generator::forward_declare_struct(const StructSymbol& struct_symbol)
{
    StructDef def(struct_symbol.name, struct_symbol.isGeneric());
    def.isTransparent = struct_symbol.isTransparent();
    def.attributes = struct_symbol.attributes;

    unsigned idx = 0;
    for (const auto& field : struct_symbol.fields)
    {
        def.fields.emplace_back(field.name, field.type);
        def.fieldIndices[field.name] = idx++;
    }

    for (const auto& method : struct_symbol.methods)
    {
        def.methods.emplace_back(method);
    }
    for (const auto& prop : struct_symbol.properties)
    {
        def.properties.push_back(prop);
    }

    if (struct_symbol.isGeneric())
    {
        def.genericParams = {};
        for (const auto& param : struct_symbol.genericParams)
        {
            auto gp = GenericParam(SourceIdentifier(param.name));
            gp.constraints = param.constraints;
            def.genericParams.params.push_back(std::move(gp));
        }
    }

    if (struct_symbol.isTransparent())
    {
        def.transparentUnderlying = generate_type(*struct_symbol.baseType);
        def.llvmType = llvm::StructType::create(*context, struct_symbol.name);
    }
    else
    {
        def.llvmType = llvm::StructType::create(*context, struct_symbol.name);
    }

    if (!def.isGeneric)
    {
        declaredTypes.emplace_back(def.llvmType);
    }

    currentScope->define_struct(struct_symbol.name, std::move(def));
}

void Generator::resolve_struct_body(const StructSymbol& struct_symbol)
{
    StructDef* def = currentScope->lookup_struct(struct_symbol.name);
    if (!def)
    {
        throw std::runtime_error("Struct not forward declared: " + struct_symbol.name);
    }

    if (def->isGeneric)
    {
        return;
    }

    if (def->isTransparent)
    {
        if (def->llvmType && def->llvmType->isOpaque())
        {
            def->llvmType->setBody({def->transparentUnderlying});
        }
        return;
    }

    std::vector<llvm::Type*> fieldTypes;
    for (const auto& field : struct_symbol.fields)
    {
        fieldTypes.push_back(generate_type(field.type));
    }

    if (def->llvmType && def->llvmType->isOpaque())
    {
        def->llvmType->setBody(fieldTypes);
    }
}

void Generator::generate_struct_methods(const StructSymbol& struct_symbol)
{
    if (struct_symbol.isGeneric())
    {
        return;
    }

    const StructDef* def = currentScope->lookup_struct(struct_symbol.name);
    if (!def || !def->llvmType)
    {
        throw std::runtime_error("Struct not found for method generation: " + struct_symbol.name);
    }

    for (const auto& prop : struct_symbol.properties)
    {
        generate_property(struct_symbol, *prop);
    }

    for (const auto& method : struct_symbol.methods)
    {
        generate_method(struct_symbol, *method);
    }
}

void Generator::generate_property(const StructSymbol& struc, const PropertySymbol& prop)
{
    StructDef* def = currentScope->lookup_struct(struc.name);
    if (!def) return;

    PropertyInfo propInfo;
    propInfo.name = prop.name;
    propInfo.hasGetter = prop.hasGetter;
    propInfo.hasSetter = prop.hasSetter;

    if (prop.isAutoProperty())
    {
        propInfo.backingFieldName = prop.backingFieldName();
        if (const auto it = def->fieldIndices.find(propInfo.backingFieldName); it != def->fieldIndices.end())
        {
            propInfo.backingFieldIndex = it->second;
        }
    }

    def->propertyInfos[prop.name] = propInfo;

    // Generate getter
    if (prop.hasGetter && (prop.getterBody || prop.getterExpr))
    {
        push_scope();

        const auto getterName = struc.name + "__get_" + prop.name;
        llvm::Type* returnType = generate_type(prop.type);

        std::vector<llvm::Type*> paramTypes;
        paramTypes.push_back(llvm::PointerType::get(def->llvmType, 0));

        auto* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
        auto* llvmFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, getterName, *module);

        functions[getterName] = llvmFunc;
        def->methodFunctions["get_" + prop.name] = llvmFunc;
        currentFunction = llvmFunc;

        auto* entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
        builder->SetInsertPoint(entry);

        const auto argIt = llvmFunc->arg_begin();
        argIt->setName("this");
        auto* thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, struc.name);

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

    // Generate setter
    if (prop.hasSetter && (prop.setterBody || prop.setterExpr))
    {
        push_scope();

        const auto setterName = struc.name + "__set_" + prop.name;
        llvm::Type* valueType = generate_type(prop.type);

        std::vector<llvm::Type*> paramTypes;
        paramTypes.push_back(llvm::PointerType::get(def->llvmType, 0));
        paramTypes.push_back(valueType);

        auto* funcType = llvm::FunctionType::get(builder->getVoidTy(), paramTypes, false);
        auto* llvmFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, setterName, *module);

        functions[setterName] = llvmFunc;
        def->methodFunctions["set_" + prop.name] = llvmFunc;
        currentFunction = llvmFunc;

        auto* entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
        builder->SetInsertPoint(entry);

        auto argIt = llvmFunc->arg_begin();
        argIt->setName("this");
        auto* thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, struc.name);
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

void Generator::generate_method(const StructSymbol& struc, const MethodSymbol& method)
{
    push_scope();

    StructDef* def = currentScope->lookup_struct(struc.name);
    if (!def || !def->llvmType)
    {
        pop_scope();
        throw std::runtime_error("Struct not found for method: " + struc.name);
    }

    const std::string mangledName = struc.name + "__" + method.name;

    llvm::Type* returnType = generate_type(method.returnType);

    // Async methods return ptr (coroutine handle) instead of their declared return type
    llvm::Type* actualReturnType = method.isAsync
                                       ? llvm::PointerType::getUnqual(*context)
                                       : returnType;

    std::vector<llvm::Type*> paramTypes;
    const bool isStatic = method.isStatic;
    if (!isStatic)
    {
        paramTypes.push_back(llvm::PointerType::get(def->llvmType, 0));
    }

    for (const auto& paramType : method.paramTypes)
    {
        paramTypes.push_back(generate_type(paramType));
    }

    const auto funcType = llvm::FunctionType::get(actualReturnType, paramTypes, method.isVariadic);
    const auto llvmFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, mangledName, *module);

    functions[mangledName] = llvmFunc;
    def->methodFunctions[method.name] = llvmFunc;

    currentFunction = llvmFunc;

    if (method.isAsync)
    {
        generate_async_method_body(struc, method, llvmFunc, def);
        pop_scope();
        return;
    }

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    auto argIt = llvmFunc->arg_begin();

    if (!isStatic)
    {
        argIt->setName("this");
        auto* thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, struc.name);
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
        std::string paramStructType = paramType.kind == TypeKind::STRUCT ? paramType.structName : "";
        currentScope->define_variable(paramName, alloca, paramStructType);

        ++argIt;
        ++paramIdx;
    }

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

    if (!builder->GetInsertBlock()->getTerminator())
    {
        emit_scope_cleanup();
        if (method.isConstructor || returnType->isVoidTy())
        {
            // Constructors always return void - the allocation happens at call site
            builder->CreateRetVoid();
        }
        else
        {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    pop_scope();
}

void Generator::generate_async_method_body(const StructSymbol& struc, const MethodSymbol& method,
                                           llvm::Function* llvmFunc, StructDef* def)
{
    hasAsyncFunctions = true;
    ensure_malloc_free_declared();

    // Mark as presplitcoroutine so CoroSplitPass knows to process this function
    llvmFunc->addFnAttr(llvm::Attribute::PresplitCoroutine);

    llvm::Type* origReturnType = generate_type(method.returnType);

    auto* ptrTy = llvm::PointerType::getUnqual(*context);
    auto* i64Ty = builder->getInt64Ty();

    auto* entryBB = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    auto* allocBB = llvm::BasicBlock::Create(*context, "coro.alloc", llvmFunc);
    auto* beginBB = llvm::BasicBlock::Create(*context, "coro.begin", llvmFunc);
    auto* finalSuspendBB = llvm::BasicBlock::Create(*context, "coro.final", llvmFunc);
    auto* cleanupBB = llvm::BasicBlock::Create(*context, "coro.cleanup", llvmFunc);
    auto* suspendBB = llvm::BasicBlock::Create(*context, "coro.suspend", llvmFunc);
    auto* trapBB = llvm::BasicBlock::Create(*context, "coro.trap", llvmFunc);

    builder->SetInsertPoint(entryBB);

    // Create promise alloca BEFORE coro.id — LLVM requires this so it can
    // place the promise in the coroutine frame (accessible via @llvm.coro.promise)
    // NOTE: Do NOT store to promise here — after CoroSplit the address depends on
    // coro.begin which isn't available yet. Zero-init happens after coro.begin.
    llvm::Value* promisePtr = nullptr;
    if (!origReturnType->isVoidTy())
    {
        promisePtr = builder->CreateAlloca(origReturnType, nullptr, "coro.promise");
    }

    // Pass promise alloca as 2nd arg to coro.id
    auto* coroIdFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_id);
    llvm::Value* promiseArg = promisePtr
                                  ? promisePtr
                                  : static_cast<llvm::Value*>(llvm::ConstantPointerNull::get(ptrTy));
    unsigned promiseAlign = promisePtr
                                ? module->getDataLayout().getABITypeAlign(origReturnType).value()
                                : 0;
    llvm::Value* coroId = builder->CreateCall(coroIdFn, {
                                                  builder->getInt32(promiseAlign),
                                                  promiseArg,
                                                  llvm::ConstantPointerNull::get(ptrTy),
                                                  llvm::ConstantPointerNull::get(ptrTy)
                                              }, "coro.id");

    auto* coroAllocFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_alloc);
    llvm::Value* needAlloc = builder->CreateCall(coroAllocFn, {coroId}, "need.alloc");
    builder->CreateCondBr(needAlloc, allocBB, beginBB);

    builder->SetInsertPoint(allocBB);
    auto* coroSizeFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_size, {i64Ty});
    llvm::Value* coroSize = builder->CreateCall(coroSizeFn, {}, "coro.size");
    auto* mallocFn = module->getFunction("__djinn_malloc");
    llvm::Value* mem = builder->CreateCall(mallocFn, {coroSize}, "coro.mem");
    builder->CreateBr(beginBB);

    builder->SetInsertPoint(beginBB);
    auto* phiMem = builder->CreatePHI(ptrTy, 2, "coro.mem.phi");
    phiMem->addIncoming(mem, allocBB);
    phiMem->addIncoming(llvm::ConstantPointerNull::get(ptrTy), entryBB);

    auto* coroBeginFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_begin);
    llvm::Value* coroHandle = builder->CreateCall(coroBeginFn, {coroId, phiMem}, "coro.hdl");

    // Save/set async state
    bool prevInAsync = inAsyncFunction;
    llvm::Value* prevCoroId = asyncCoroId;
    llvm::Value* prevCoroHandle = asyncCoroHandle;
    llvm::Value* prevPromisePtr = asyncPromisePtr;
    llvm::BasicBlock* prevFinalSuspendBB = asyncFinalSuspendBB;
    llvm::BasicBlock* prevCleanupBB = asyncCleanupBB;
    llvm::BasicBlock* prevSuspendBB = asyncSuspendBB;
    llvm::Type* prevAsyncReturnType = asyncReturnType;

    inAsyncFunction = true;
    asyncCoroId = coroId;
    asyncCoroHandle = coroHandle;
    asyncPromisePtr = promisePtr;
    asyncFinalSuspendBB = finalSuspendBB;
    asyncCleanupBB = cleanupBB;
    asyncSuspendBB = suspendBB;
    asyncReturnType = origReturnType;

    // Store parameters
    auto argIt = llvmFunc->arg_begin();
    if (!method.isStatic)
    {
        argIt->setName("this");
        auto* thisAlloca = builder->CreateAlloca(argIt->getType(), nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, struc.name);
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
        std::string paramStructType = paramType.kind == TypeKind::STRUCT ? paramType.structName : "";
        currentScope->define_variable(paramName, alloca, paramStructType);

        ++argIt;
        ++paramIdx;
    }

    // Generate method body
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
        if (promisePtr && !origReturnType->isVoidTy())
        {
            result = cast_value(result, origReturnType);
            builder->CreateStore(result, promisePtr);
        }
        builder->CreateBr(finalSuspendBB);
    }

    if (!builder->GetInsertBlock()->getTerminator())
    {
        if (promisePtr && !origReturnType->isVoidTy())
        {
            builder->CreateStore(llvm::Constant::getNullValue(origReturnType), promisePtr);
        }
        builder->CreateBr(finalSuspendBB);
    }

    // Final suspend, trap, cleanup, suspend blocks
    builder->SetInsertPoint(finalSuspendBB);
    auto* coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_suspend);
    llvm::Value* finalSuspend = builder->CreateCall(coroSuspendFn, {
                                                        llvm::ConstantTokenNone::get(*context),
                                                        builder->getTrue()
                                                    }, "coro.final.suspend");
    auto* switchInst = builder->CreateSwitch(finalSuspend, suspendBB, 2);
    switchInst->addCase(builder->getInt8(0), trapBB);
    switchInst->addCase(builder->getInt8(1), cleanupBB);

    builder->SetInsertPoint(trapBB);
    auto* trapFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::trap);
    builder->CreateCall(trapFn);
    builder->CreateUnreachable();

    builder->SetInsertPoint(cleanupBB);
    auto* coroFreeFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_free);
    llvm::Value* freeMem = builder->CreateCall(coroFreeFn, {coroId, coroHandle}, "coro.free.mem");
    auto* freeDoFreeBB = llvm::BasicBlock::Create(*context, "coro.free.do", llvmFunc);
    llvm::Value* isNull = builder->CreateICmpEQ(freeMem, llvm::ConstantPointerNull::get(ptrTy), "is.null");
    builder->CreateCondBr(isNull, suspendBB, freeDoFreeBB);

    builder->SetInsertPoint(freeDoFreeBB);
    auto* freeFn = module->getFunction("__djinn_free");
    builder->CreateCall(freeFn, {freeMem});
    builder->CreateBr(suspendBB);

    builder->SetInsertPoint(suspendBB);
    auto* coroEndFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_end);
    builder->CreateCall(coroEndFn, {
                            coroHandle,
                            builder->getFalse(),
                            llvm::ConstantTokenNone::get(*context)
                        });
    builder->CreateRet(coroHandle);

    // Restore async state
    inAsyncFunction = prevInAsync;
    asyncCoroId = prevCoroId;
    asyncCoroHandle = prevCoroHandle;
    asyncPromisePtr = prevPromisePtr;
    asyncFinalSuspendBB = prevFinalSuspendBB;
    asyncCleanupBB = prevCleanupBB;
    asyncSuspendBB = prevSuspendBB;
    asyncReturnType = prevAsyncReturnType;
}