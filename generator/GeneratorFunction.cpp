//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"


void Generator::forward_declare_function(const FunctionSymbol& func)
{
    llvm::Type* returnType = generate_type(func.returnType);

    std::vector<llvm::Type*> paramTypes;
    for (const auto& paramType : func.paramTypes)
    {
        paramTypes.push_back(generate_type(paramType));
    }

    // Async functions return ptr (coroutine handle) instead of their declared return type
    llvm::Type* actualReturnType = func.isAsync
                                       ? llvm::PointerType::getUnqual(*context)
                                       : returnType;

    const auto funcType = llvm::FunctionType::get(actualReturnType, paramTypes, false);
    const auto llvmFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        func.name,
        *module
    );

    functions[func.name] = llvmFunc;
}

void Generator::generate_function_body(const FunctionSymbol& func)
{
    if (func.isAsync)
    {
        generate_async_function_body(func);
        return;
    }

    push_scope();

    llvm::Function* llvmFunc = functions[func.name];
    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    size_t idx = 0;
    for (auto& arg : llvmFunc->args())
    {
        const auto& paramName = func.paramNames[idx];
        const auto& paramType = func.paramTypes[idx];
        arg.setName(paramName);

        auto* alloca = builder->CreateAlloca(arg.getType(), nullptr, paramName);
        builder->CreateStore(&arg, alloca);
        std::string structTypeName = paramType.kind == TypeKind::STRUCT ? paramType.structName : "";
        currentScope->define_variable(paramName, alloca, structTypeName);
        idx++;
    }

    if (func.body)
    {
        for (const auto& stmt : func.body->statements)
        {
            generate_statement(*stmt);
        }
    }

    if (builder->GetInsertBlock()->getTerminator())
    {
        pop_scope();
        return;
    }

    emit_scope_cleanup();

    llvm::Type* returnType = llvmFunc->getReturnType();
    if (returnType->isVoidTy())
    {
        builder->CreateRetVoid();
        pop_scope();
        return;
    }

    builder->CreateRet(llvm::Constant::getNullValue(returnType));
    pop_scope();
}

void Generator::ensure_malloc_free_declared()
{
    auto* ptrTy = llvm::PointerType::getUnqual(*context);
    auto* i64Ty = builder->getInt64Ty();
    auto* voidTy = builder->getVoidTy();

    if (!module->getFunction("malloc"))
    {
        auto* mallocTy = llvm::FunctionType::get(ptrTy, {i64Ty}, false);
        auto* mallocFn = llvm::Function::Create(mallocTy, llvm::Function::ExternalLinkage, "malloc", *module);
        mallocFn->setCallingConv(llvm::CallingConv::C);
    }
    if (!module->getFunction("free"))
    {
        auto* freeTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
        auto* freeFn = llvm::Function::Create(freeTy, llvm::Function::ExternalLinkage, "free", *module);
        freeFn->setCallingConv(llvm::CallingConv::C);
    }
}

void Generator::generate_async_function_body(const FunctionSymbol& func)
{
    hasAsyncFunctions = true;
    ensure_malloc_free_declared();

    push_scope();

    llvm::Function* llvmFunc = functions[func.name];
    currentFunction = llvmFunc;

    // Mark as presplitcoroutine so CoroSplitPass knows to process this function
    llvmFunc->addFnAttr(llvm::Attribute::PresplitCoroutine);

    // The original return type (what the user declared)
    llvm::Type* origReturnType = generate_type(func.returnType);

    auto* ptrTy = llvm::PointerType::getUnqual(*context);
    auto* i64Ty = builder->getInt64Ty();

    // Create basic blocks
    auto* entryBB = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    auto* allocBB = llvm::BasicBlock::Create(*context, "coro.alloc", llvmFunc);
    auto* beginBB = llvm::BasicBlock::Create(*context, "coro.begin", llvmFunc);
    auto* finalSuspendBB = llvm::BasicBlock::Create(*context, "coro.final", llvmFunc);
    auto* cleanupBB = llvm::BasicBlock::Create(*context, "coro.cleanup", llvmFunc);
    auto* suspendBB = llvm::BasicBlock::Create(*context, "coro.suspend", llvmFunc);
    auto* trapBB = llvm::BasicBlock::Create(*context, "coro.trap", llvmFunc);

    // --- entry block ---
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

    // %id = call token @llvm.coro.id(i32 <align>, ptr <promise>, ptr null, ptr null)
    // Pass promise alloca as 2nd arg so LLVM places it in the coroutine frame
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

    // %need.alloc = call i1 @llvm.coro.alloc(token %id)
    auto* coroAllocFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_alloc);
    llvm::Value* needAlloc = builder->CreateCall(coroAllocFn, {coroId}, "need.alloc");
    builder->CreateCondBr(needAlloc, allocBB, beginBB);

    // --- alloc block ---
    builder->SetInsertPoint(allocBB);

    // %size = call i64 @llvm.coro.size.i64()
    auto* coroSizeFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_size, {i64Ty});
    llvm::Value* coroSize = builder->CreateCall(coroSizeFn, {}, "coro.size");

    // %mem = call ptr @malloc(i64 %size)
    auto* mallocFn = module->getFunction("malloc");
    llvm::Value* mem = builder->CreateCall(mallocFn, {coroSize}, "coro.mem");
    builder->CreateBr(beginBB);

    // --- begin block ---
    builder->SetInsertPoint(beginBB);

    // PHI for memory: either from alloc or null (when no alloc needed)
    auto* phiMem = builder->CreatePHI(ptrTy, 2, "coro.mem.phi");
    phiMem->addIncoming(mem, allocBB);
    phiMem->addIncoming(llvm::ConstantPointerNull::get(ptrTy), entryBB);

    // %hdl = call ptr @llvm.coro.begin(token %id, ptr %mem)
    auto* coroBeginFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_begin);
    llvm::Value* coroHandle = builder->CreateCall(coroBeginFn, {coroId, phiMem}, "coro.hdl");

    // Save async state
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

    // Store function parameters
    size_t idx = 0;
    for (auto& arg : llvmFunc->args())
    {
        const auto& paramName = func.paramNames[idx];
        const auto& paramType = func.paramTypes[idx];
        arg.setName(paramName);

        auto* alloca = builder->CreateAlloca(arg.getType(), nullptr, paramName);
        builder->CreateStore(&arg, alloca);
        std::string structTypeName = paramType.kind == TypeKind::STRUCT ? paramType.structName : "";
        currentScope->define_variable(paramName, alloca, structTypeName);
        idx++;
    }

    // Generate the function body
    if (func.body)
    {
        for (const auto& stmt : func.body->statements)
        {
            generate_statement(*stmt);
        }
    }

    // If the body didn't terminate (no return), branch to final suspend
    if (!builder->GetInsertBlock()->getTerminator())
    {
        if (promisePtr && !origReturnType->isVoidTy())
        {
            // Store default value
            builder->CreateStore(llvm::Constant::getNullValue(origReturnType), promisePtr);
        }
        builder->CreateBr(finalSuspendBB);
    }

    // --- final suspend block ---
    builder->SetInsertPoint(finalSuspendBB);
    auto* coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_suspend);
    llvm::Value* finalSuspend = builder->CreateCall(coroSuspendFn, {
                                                        llvm::ConstantTokenNone::get(*context),
                                                        builder->getTrue() // is_final = true
                                                    }, "coro.final.suspend");
    auto* switchInst = builder->CreateSwitch(finalSuspend, suspendBB, 2);
    switchInst->addCase(builder->getInt8(0), trapBB);
    switchInst->addCase(builder->getInt8(1), cleanupBB);

    // --- trap block (resumed after final suspend = undefined behavior) ---
    builder->SetInsertPoint(trapBB);
    auto* trapFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::trap);
    builder->CreateCall(trapFn);
    builder->CreateUnreachable();

    // --- cleanup block ---
    builder->SetInsertPoint(cleanupBB);
    auto* coroFreeFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_free);
    llvm::Value* freeMem = builder->CreateCall(coroFreeFn, {coroId, coroHandle}, "coro.free.mem");

    // Check if freeMem is null before calling free
    auto* freeDoFreeBB = llvm::BasicBlock::Create(*context, "coro.free.do", llvmFunc);
    llvm::Value* isNull = builder->CreateICmpEQ(freeMem, llvm::ConstantPointerNull::get(ptrTy), "is.null");
    builder->CreateCondBr(isNull, suspendBB, freeDoFreeBB);

    builder->SetInsertPoint(freeDoFreeBB);
    auto* freeFn = module->getFunction("free");
    builder->CreateCall(freeFn, {freeMem});
    builder->CreateBr(suspendBB);

    // --- suspend block ---
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

    pop_scope();
}

void Generator::generate_extern_function(const ExternFunctionSymbol& func)
{
    std::vector<llvm::Type*> paramTypes;
    for (const auto& paramType : func.paramTypes)
    {
        paramTypes.push_back(generate_type(paramType));
    }

    llvm::Type* returnType = generate_type(func.returnType);

    llvm::FunctionType* funcType = llvm::FunctionType::get(
        returnType,
        paramTypes,
        func.isVariadic
    );

    llvm::Function* llvmFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        func.name,
        *module
    );

    functions[func.name] = llvmFunc;
    externFunctions.push_back(llvmFunc);

    if (func.abi == "C")
    {
        llvmFunc->setCallingConv(llvm::CallingConv::C);
    }
}

void Generator::emit_used_declarations()
{
    auto* i8PtrTy = llvm::PointerType::getUnqual(*context);
    std::vector<llvm::Constant*> usedItems;

    for (auto* func : externFunctions)
    {
        usedItems.push_back(llvm::ConstantExpr::getBitCast(func, i8PtrTy));
    }

    int typeIdx = 0;
    for (auto* structType : declaredTypes)
    {
        if (structType->isOpaque()) continue;

        auto* dummy = new llvm::GlobalVariable(
            *module,
            structType,
            false,
            llvm::GlobalValue::ExternalLinkage,
            llvm::Constant::getNullValue(structType),
            "__djinn_type_" + std::to_string(typeIdx++)
        );
        dummy->setVisibility(llvm::GlobalValue::HiddenVisibility);
        usedItems.push_back(llvm::ConstantExpr::getBitCast(dummy, i8PtrTy));
    }

    if (usedItems.empty()) return;

    auto* arrayTy = llvm::ArrayType::get(i8PtrTy, usedItems.size());
    auto* usedArray = llvm::ConstantArray::get(arrayTy, usedItems);

    auto* gv = new llvm::GlobalVariable(
        *module,
        arrayTy,
        false,
        llvm::GlobalValue::AppendingLinkage,
        usedArray,
        "llvm.compiler.used"
    );
    gv->setSection("llvm.metadata");
}