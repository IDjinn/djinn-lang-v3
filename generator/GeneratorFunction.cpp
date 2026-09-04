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

    apply_implicit_attributes(llvmFunc);
    functions[func.name] = llvmFunc;
}

void Generator::generate_function_body(const FunctionSymbol& func)
{
    currentFunctionThrows = func.isThrowing();
    currentContracts_.clear();
    contractReturnAlloca = nullptr;

    if (func.isAsync)
    {
        generate_async_function_body(func);
        currentFunctionThrows = false;
        return;
    }

    push_scope();

    llvm::Function* llvmFunc = functions[func.name];
    currentFunction = llvmFunc;

    const auto entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    debugInfo->begin_function(llvmFunc, func.name, func.location);

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
        if (paramType.kind == TypeKind::INTEGER)
        {
            currentScope->set_variable_signed(paramName, paramType.sign);
            currentScope->set_variable_non_zero(paramName, paramType.nonZero);
        }
        emit_non_zero_param_check(paramName, paramType);
        idx++;
    }

    // Contracts: require checks at entry + `return` binding for ensure clauses
    setup_contracts(func.contracts, llvmFunc);

    if (func.body)
    {
        for (const auto& stmt : func.body->statements)
        {
            generate_statement(*stmt);
        }
    }

    currentContracts_.clear();
    contractReturnAlloca = nullptr;

    if (builder->GetInsertBlock()->getTerminator())
    {
        debugInfo->end_function();
        pop_scope();
        currentFunctionThrows = false;
        return;
    }

    emit_scope_cleanup();

    llvm::Type* returnType = llvmFunc->getReturnType();
    if (returnType->isVoidTy())
    {
        builder->CreateRetVoid();
        debugInfo->end_function();
        pop_scope();
        currentFunctionThrows = false;
        return;
    }

    builder->CreateRet(llvm::Constant::getNullValue(returnType));
    debugInfo->end_function();
    pop_scope();
    currentFunctionThrows = false;
}

// Promise layout: { err_tag, err_message, err_type_name, value } — the error
// slot comes first so the runtime reads it at fixed offsets (awaiters and the
// event loop transfer it across suspend points; see djinn_runtime.h).
llvm::StructType* Generator::promise_type(llvm::Type* valueType)
{
    if (!valueType || valueType->isVoidTy())
    {
        return llvm::StructType::get(*context,
                                     {builder->getInt32Ty(), builder->getPtrTy(), builder->getPtrTy()});
    }
    return llvm::StructType::get(*context, {
                                     builder->getInt32Ty(), builder->getPtrTy(), builder->getPtrTy(), valueType
                                 });
}

// Frames are not zero-initialized: the error slot must read "no error" until
// the body decides otherwise.
void Generator::emit_promise_error_slot_zero(llvm::StructType* promiseTy, llvm::Value* promisePtr)
{
    builder->CreateStore(builder->getInt32(0),
                         builder->CreateStructGEP(promiseTy, promisePtr, 0, "err.zero.tag"));
    builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()),
                         builder->CreateStructGEP(promiseTy, promisePtr, 1, "err.zero.msg"));
    builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()),
                         builder->CreateStructGEP(promiseTy, promisePtr, 2, "err.zero.type"));
}

void Generator::ensure_malloc_free_declared()
{
    auto* ptrTy = llvm::PointerType::getUnqual(*context);
    auto* i64Ty = builder->getInt64Ty();
    auto* voidTy = builder->getVoidTy();

    // Standard malloc/free for coro frames (CoroSplit expects these)
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

    // __djinn_malloc/__djinn_free from runtime for user-level allocations (new, arrays)
    if (!module->getFunction("__djinn_malloc"))
    {
        auto* mallocTy = llvm::FunctionType::get(ptrTy, {i64Ty}, false);
        auto* mallocFn = llvm::Function::Create(mallocTy, llvm::Function::ExternalLinkage, "__djinn_malloc", *module);
        mallocFn->setCallingConv(llvm::CallingConv::C);
    }
    if (!module->getFunction("__djinn_free"))
    {
        auto* freeTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
        auto* freeFn = llvm::Function::Create(freeTy, llvm::Function::ExternalLinkage, "__djinn_free", *module);
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
    auto* promiseTy = promise_type(origReturnType);

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

    debugInfo->begin_function(llvmFunc, func.name, func.location);

    // Create promise alloca BEFORE coro.id — LLVM requires this so it can
    // place the promise in the coroutine frame (accessible via @llvm.coro.promise)
    // NOTE: Do NOT store to promise here — after CoroSplit the address depends on
    // coro.begin which isn't available yet. Zero-init happens after coro.begin.
    llvm::Value* promisePtr = builder->CreateAlloca(promiseTy, nullptr, "coro.promise");

    // %id = call token @llvm.coro.id(i32 <align>, ptr <promise>, ptr null, ptr null)
    // Pass promise alloca as 2nd arg so LLVM places it in the coroutine frame
    auto* coroIdFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_id);
    constexpr unsigned promiseAlign = 16; // DJINN_PROMISE_ALIGN — runtime reads the slot
    llvm::Value* coroId = builder->CreateCall(coroIdFn, {
                                                  builder->getInt32(promiseAlign),
                                                  promisePtr,
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
    // NOTE: coro frames use standard malloc/free — CoroSplit expects these names
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
    llvm::Value* prevErrSlotPtr = asyncErrSlotPtr;
    llvm::Type* prevPromiseType = asyncPromiseType;
    llvm::BasicBlock* prevFinalSuspendBB = asyncFinalSuspendBB;
    llvm::BasicBlock* prevCleanupBB = asyncCleanupBB;
    llvm::BasicBlock* prevSuspendBB = asyncSuspendBB;
    llvm::Type* prevAsyncReturnType = asyncReturnType;

    inAsyncFunction = true;
    asyncCoroId = coroId;
    asyncCoroHandle = coroHandle;
    asyncPromisePtr = origReturnType->isVoidTy()
                          ? nullptr
                          : builder->CreateStructGEP(promiseTy, promisePtr, 3, "coro.promise.value");
    asyncErrSlotPtr = promisePtr;
    asyncPromiseType = promiseTy;
    asyncFinalSuspendBB = finalSuspendBB;
    asyncCleanupBB = cleanupBB;
    asyncSuspendBB = suspendBB;
    asyncReturnType = origReturnType;

    // Native mode: throwing calls in the body unwind to the body pad, which
    // mirrors the error into the promise slot and resumes at the final
    // suspend — unwinding cannot cross suspend points
    NativeLanding bodyLanding;
    const bool bodyLandingActive = nativeExceptions && currentFunctionThrows;
    if (bodyLandingActive)
    {
        bodyLanding = push_native_landing(false);
    }

    // --- initial suspend: function returns handle immediately, body runs on first resume ---
    {
        auto* coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_suspend);
        llvm::Value* initSusp = builder->CreateCall(coroSuspendFn, {
                                                        llvm::ConstantTokenNone::get(*context),
                                                        builder->getFalse()
                                                    }, "coro.init.susp");
        auto* initResumeBB = llvm::BasicBlock::Create(*context, "init.resume", llvmFunc);
        auto* initSwitch = builder->CreateSwitch(initSusp, suspendBB, 2);
        initSwitch->addCase(builder->getInt8(0), initResumeBB);
        initSwitch->addCase(builder->getInt8(1), cleanupBB);
        builder->SetInsertPoint(initResumeBB);
    }

    // The promise error slot starts clean (frames are not zero-initialized)
    emit_promise_error_slot_zero(promiseTy, promisePtr);

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
        if (paramType.kind == TypeKind::INTEGER)
        {
            currentScope->set_variable_signed(paramName, paramType.sign);
            currentScope->set_variable_non_zero(paramName, paramType.nonZero);
        }
        // No non-zero entry check here: contracts are not emitted in async
        // bodies yet (same as explicit require clauses)
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
        if (asyncPromisePtr && !origReturnType->isVoidTy())
        {
            // Store default value
            builder->CreateStore(llvm::Constant::getNullValue(origReturnType), asyncPromisePtr);
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

    // Fill the async body landing: both pads mirror the error state (set by
    // the shim) into the promise slot and resume at the final suspend
    if (bodyLandingActive)
    {
        builder->SetInsertPoint(bodyLanding.dispatchBB);
        auto* catchSwitch = builder->CreateCatchSwitch(
            llvm::ConstantTokenNone::get(*context), nullptr, 2);
        catchSwitch->addHandler(bodyLanding.djinnPad);
        catchSwitch->addHandler(bodyLanding.allPad);

        auto fillBodyPad = [&](llvm::BasicBlock* padBB, const bool foreign)
        {
            builder->SetInsertPoint(padBB);
            llvm::Value* catchType = foreign
                                         ? static_cast<llvm::Value*>(llvm::ConstantPointerNull::get(ptrTy))
                                         : static_cast<llvm::Value*>(ehErrorTypeDesc);
            auto* pad = builder->CreateCatchPad(catchSwitch, {catchType});
            if (foreign)
            {
                auto* wrapTy = llvm::FunctionType::get(ptrTy, false);
                auto* wrapFn = module->getFunction("__djinn_wrap_foreign");
                if (!wrapFn)
                {
                    wrapFn = llvm::Function::Create(wrapTy, llvm::Function::ExternalLinkage,
                                                    "__djinn_wrap_foreign", *module);
                }
                builder->CreateCall(wrapFn);
            }

            builder->CreateStore(errno_load_i32(1, "body.err.tag"),
                                 builder->CreateStructGEP(promiseTy, promisePtr, 0, "body.slot.tag"));
            builder->CreateStore(errno_load_ptr(2, "body.err.msg"),
                                 builder->CreateStructGEP(promiseTy, promisePtr, 1, "body.slot.msg"));
            builder->CreateStore(errno_load_ptr(3, "body.err.type"),
                                 builder->CreateStructGEP(promiseTy, promisePtr, 2, "body.slot.type"));
            emit_all_scope_cleanup();
            if (asyncPromisePtr && !origReturnType->isVoidTy())
            {
                builder->CreateStore(get_default_value(origReturnType), asyncPromisePtr);
            }
            builder->CreateCatchRet(pad, finalSuspendBB);
        };
        fillBodyPad(bodyLanding.djinnPad, false);
        fillBodyPad(bodyLanding.allPad, true);
        ehLandingStack_.pop_back();
    }

    // Restore async state
    inAsyncFunction = prevInAsync;
    asyncCoroId = prevCoroId;
    asyncCoroHandle = prevCoroHandle;
    asyncPromisePtr = prevPromisePtr;
    asyncErrSlotPtr = prevErrSlotPtr;
    asyncPromiseType = prevPromiseType;
    asyncFinalSuspendBB = prevFinalSuspendBB;
    asyncCleanupBB = prevCleanupBB;
    asyncSuspendBB = prevSuspendBB;
    asyncReturnType = prevAsyncReturnType;

    debugInfo->end_function();
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

    llvmFunc->addFnAttr(llvm::Attribute::NoInline);
    llvmFunc->removeFnAttr(llvm::Attribute::ReadNone);
    llvmFunc->removeFnAttr(llvm::Attribute::ReadOnly);
    llvmFunc->addFnAttr(llvm::Attribute::NoUnwind);

    functions[func.name] = llvmFunc;
    externFunctions.push_back(llvmFunc);

    if (func.abi == "C")
    {
        llvmFunc->setCallingConv(llvm::CallingConv::C);
    }
}

void Generator::generate_coro_wrappers()
{
    auto* ptrTy = llvm::PointerType::getUnqual(*context);
    auto* i1Ty = builder->getInt1Ty();
    auto* i32Ty = builder->getInt32Ty();
    auto* voidTy = builder->getVoidTy();

    // __djinn_coro_resume(ptr %handle) -> void
    {
        auto* ft = llvm::FunctionType::get(voidTy, {ptrTy}, false);
        auto* fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                          "__djinn_coro_resume", *module);
        auto* bb = llvm::BasicBlock::Create(*context, "entry", fn);
        builder->SetInsertPoint(bb);
        auto* resumeFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_resume);
        builder->CreateCall(resumeFn, {fn->getArg(0)});
        builder->CreateRetVoid();
    }

    // __djinn_coro_done(ptr %handle) -> i32 (zext from i1 to match C int return)
    {
        auto* ft = llvm::FunctionType::get(i32Ty, {ptrTy}, false);
        auto* fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                          "__djinn_coro_done", *module);
        auto* bb = llvm::BasicBlock::Create(*context, "entry", fn);
        builder->SetInsertPoint(bb);
        auto* doneFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_done);
        auto* result = builder->CreateCall(doneFn, {fn->getArg(0)}, "done");
        auto* extended = builder->CreateZExt(result, i32Ty, "done.ext");
        builder->CreateRet(extended);
    }

    // __djinn_coro_destroy(ptr %handle) -> void
    {
        auto* ft = llvm::FunctionType::get(voidTy, {ptrTy}, false);
        auto* fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                          "__djinn_coro_destroy", *module);
        auto* bb = llvm::BasicBlock::Create(*context, "entry", fn);
        builder->SetInsertPoint(bb);
        auto* destroyFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_destroy);
        builder->CreateCall(destroyFn, {fn->getArg(0)});
        builder->CreateRetVoid();
    }

    // __djinn_coro_promise(ptr %handle, i32 %align) -> ptr
    {
        auto* ft = llvm::FunctionType::get(ptrTy, {ptrTy, i32Ty}, false);
        auto* fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                          "__djinn_coro_promise", *module);
        auto* bb = llvm::BasicBlock::Create(*context, "entry", fn);
        builder->SetInsertPoint(bb);
        auto* promiseFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_promise);
        auto* result = builder->CreateCall(promiseFn, {
                                               fn->getArg(0), fn->getArg(1), builder->getFalse()
                                           }, "promise");
        builder->CreateRet(result);
    }
}

void Generator::generate_runtime_declarations()
{
    auto* ptrTy = llvm::PointerType::getUnqual(*context);
    auto* i32Ty = builder->getInt32Ty();
    auto* voidTy = builder->getVoidTy();
    auto* i64Ty = builder->getInt64Ty();

    // void __djinn_runtime_init(i32 num_threads)
    if (!module->getFunction("__djinn_runtime_init"))
    {
        auto* initTy = llvm::FunctionType::get(voidTy, {i32Ty}, false);
        llvm::Function::Create(initTy, llvm::Function::ExternalLinkage,
                               "__djinn_runtime_init", *module);
    }

    // void __djinn_runtime_shutdown()
    if (!module->getFunction("__djinn_runtime_shutdown"))
    {
        auto* shutdownTy = llvm::FunctionType::get(voidTy, {}, false);
        llvm::Function::Create(shutdownTy, llvm::Function::ExternalLinkage,
                               "__djinn_runtime_shutdown", *module);
    }

    // void __djinn_uncaught_error(i32 tag, ptr type_name, ptr message, ptr origin_file,
    //                              i32 origin_line, i32 origin_column)
    if (!module->getFunction("__djinn_uncaught_error"))
    {
        auto* uncaughtTy = llvm::FunctionType::get(voidTy, {i32Ty, ptrTy, ptrTy, ptrTy, i32Ty, i32Ty}, false);
        llvm::Function::Create(uncaughtTy, llvm::Function::ExternalLinkage,
                               "__djinn_uncaught_error", *module);
    }

    // void __djinn_spawn(ptr handle)
    if (!module->getFunction("__djinn_spawn"))
    {
        auto* spawnTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
        llvm::Function::Create(spawnTy, llvm::Function::ExternalLinkage,
                               "__djinn_spawn", *module);
    }

    // i32 __djinn_event_loop(ptr main_handle)
    if (!module->getFunction("__djinn_event_loop"))
    {
        auto* loopTy = llvm::FunctionType::get(i32Ty, {ptrTy}, false);
        llvm::Function::Create(loopTy, llvm::Function::ExternalLinkage,
                               "__djinn_event_loop", *module);
    }

    // void __djinn_event_loop_run(ptr main_handle)
    if (!module->getFunction("__djinn_event_loop_run"))
    {
        auto* loopRunTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
        llvm::Function::Create(loopRunTy, llvm::Function::ExternalLinkage,
                               "__djinn_event_loop_run", *module);
    }

    // void __djinn_mark_waiting(ptr handle)
    if (!module->getFunction("__djinn_mark_waiting"))
    {
        auto* markWaitingTy = llvm::FunctionType::get(voidTy, {ptrTy}, false);
        llvm::Function::Create(markWaitingTy, llvm::Function::ExternalLinkage,
                               "__djinn_mark_waiting", *module);
    }

    // void __djinn_await(ptr child, ptr parent)
    if (!module->getFunction("__djinn_await"))
    {
        auto* awaitTy = llvm::FunctionType::get(voidTy, {ptrTy, ptrTy}, false);
        llvm::Function::Create(awaitTy, llvm::Function::ExternalLinkage,
                               "__djinn_await", *module);
    }

    // i64 __djinn_async_read(i32 fd, ptr buf, i64 count, ptr coro)
    if (!module->getFunction("__djinn_async_read"))
    {
        auto* readTy = llvm::FunctionType::get(i64Ty, {i32Ty, ptrTy, i64Ty, ptrTy}, false);
        llvm::Function::Create(readTy, llvm::Function::ExternalLinkage,
                               "__djinn_async_read", *module);
    }

    // i64 __djinn_async_write(i32 fd, ptr buf, i64 count, ptr coro)
    if (!module->getFunction("__djinn_async_write"))
    {
        auto* writeTy = llvm::FunctionType::get(i64Ty, {i32Ty, ptrTy, i64Ty, ptrTy}, false);
        llvm::Function::Create(writeTy, llvm::Function::ExternalLinkage,
                               "__djinn_async_write", *module);
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