//
// Await expression code generation — resumes coroutine and extracts result
// and error slot from the child promise
//

#include "../Generator.h"

namespace
{
    constexpr unsigned kPromiseAlign = 16; // DJINN_PROMISE_ALIGN
}

llvm::Value* Generator::generate_await_in_async(llvm::Value* childHandle, llvm::Type* resultType,
                                                const SourceLocation& loc)
{
    auto* ptrTy = llvm::PointerType::getUnqual(*context);

    // 1. Call __djinn_await(child, self) — registers continuation + enqueues child
    auto* awaitFn = module->getFunction("__djinn_await");
    if (!awaitFn)
    {
        auto* ft = llvm::FunctionType::get(builder->getVoidTy(), {ptrTy, ptrTy}, false);
        awaitFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                         "__djinn_await", *module);
    }
    builder->CreateCall(awaitFn, {childHandle, asyncCoroHandle});

    // 2. coro.suspend(false) + switch — suspend parent until child completes
    auto* coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(
        module.get(), llvm::Intrinsic::coro_suspend);
    llvm::Value* suspResult = builder->CreateCall(coroSuspendFn, {
                                                      llvm::ConstantTokenNone::get(*context),
                                                      builder->getFalse()
                                                  }, "await.susp");

    auto* awaitReadyBB = llvm::BasicBlock::Create(*context, "await.ready", currentFunction);
    auto* switchInst = builder->CreateSwitch(suspResult, asyncSuspendBB, 2);
    switchInst->addCase(builder->getInt8(0), awaitReadyBB);
    switchInst->addCase(builder->getInt8(1), asyncCleanupBB);

    // 3. await.ready: extract result and error slot from the child promise.
    //    Values are read before the destroy — they point at heap/constant
    //    storage, the promise itself dies with the frame.
    builder->SetInsertPoint(awaitReadyBB);

    auto* coroPromiseFn = llvm::Intrinsic::getOrInsertDeclaration(
        module.get(), llvm::Intrinsic::coro_promise);
    auto* promiseTy = promise_type(resultType);
    llvm::Value* promisePtr = builder->CreateCall(coroPromiseFn, {
                                                      childHandle,
                                                      builder->getInt32(kPromiseAlign),
                                                      builder->getFalse()
                                                  }, "await.promise");

    llvm::Value* result = nullptr;
    if (resultType && !resultType->isVoidTy())
    {
        auto* valuePtr = builder->CreateStructGEP(promiseTy, promisePtr, 3, "await.value.ptr");
        result = builder->CreateLoad(resultType, valuePtr, "await.result");
    }

    auto* errTag = builder->CreateLoad(builder->getInt32Ty(),
                                       builder->CreateStructGEP(promiseTy, promisePtr, 0, "await.err.tag.ptr"),
                                       "await.err.tag");
    auto* errMsg = builder->CreateLoad(builder->getPtrTy(),
                                       builder->CreateStructGEP(promiseTy, promisePtr, 1, "await.err.msg.ptr"),
                                       "await.err.msg");
    auto* errType = builder->CreateLoad(builder->getPtrTy(),
                                        builder->CreateStructGEP(promiseTy, promisePtr, 2, "await.err.type.ptr"),
                                        "await.err.type");

    // 4. Destroy child coroutine frame
    auto* coroDestroyFn = llvm::Intrinsic::getOrInsertDeclaration(
        module.get(), llvm::Intrinsic::coro_destroy);
    builder->CreateCall(coroDestroyFn, {childHandle});

    // 5. Child error slot -> thread-local error state; propagate or defer to
    //    the enclosing try/switch operand check
    emit_await_error_check(errTag, errMsg, errType, loc);

    return result;
}

llvm::Value* Generator::generate_await_loop(llvm::Value* handle, llvm::Type* resultType,
                                            const SourceLocation& loc)
{
    auto* ptrTy = llvm::PointerType::getUnqual(*context);

    // Use the event loop to properly resolve nested async awaits.
    // The naive busy-loop (resume/check done) doesn't work when the awaited
    // coroutine internally uses __djinn_await, because it would resume the
    // outermost coroutine past its suspend point before children complete.
    auto* eventLoopFn = module->getFunction("__djinn_event_loop_run");
    if (!eventLoopFn && hasAsyncFunctions)
    {
        auto* ft = llvm::FunctionType::get(builder->getVoidTy(), {ptrTy}, false);
        eventLoopFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                             "__djinn_event_loop_run", *module);
    }
    if (eventLoopFn)
    {
        builder->CreateCall(eventLoopFn, {handle});
    }
    else
    {
        // Fallback: simple busy-loop for programs with no async runtime
        auto* awaitLoopBB = llvm::BasicBlock::Create(*context, "await.loop", currentFunction);
        auto* awaitResumeBB = llvm::BasicBlock::Create(*context, "await.resume", currentFunction);
        auto* awaitReadyBB = llvm::BasicBlock::Create(*context, "await.ready", currentFunction);

        builder->CreateBr(awaitLoopBB);

        builder->SetInsertPoint(awaitLoopBB);
        auto* coroDoneFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_done);
        llvm::Value* done = builder->CreateCall(coroDoneFn, {handle}, "await.done");
        builder->CreateCondBr(done, awaitReadyBB, awaitResumeBB);

        builder->SetInsertPoint(awaitResumeBB);
        auto* coroResumeFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_resume);
        builder->CreateCall(coroResumeFn, {handle});
        builder->CreateBr(awaitLoopBB);

        builder->SetInsertPoint(awaitReadyBB);
    }

    // Extract result and error slot from coroutine promise (values are read
    // before the destroy — they outlive the frame)
    auto* coroPromiseFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_promise);
    auto* promiseTy = promise_type(resultType);
    llvm::Value* promisePtr = builder->CreateCall(coroPromiseFn, {
                                                      handle,
                                                      builder->getInt32(kPromiseAlign),
                                                      builder->getFalse()
                                                  }, "await.promise");

    llvm::Value* result = nullptr;
    if (resultType && !resultType->isVoidTy())
    {
        auto* valuePtr = builder->CreateStructGEP(promiseTy, promisePtr, 3, "await.value.ptr");
        result = builder->CreateLoad(resultType, valuePtr, "await.result");
    }

    auto* errTag = builder->CreateLoad(builder->getInt32Ty(),
                                       builder->CreateStructGEP(promiseTy, promisePtr, 0, "await.err.tag.ptr"),
                                       "await.err.tag");
    auto* errMsg = builder->CreateLoad(builder->getPtrTy(),
                                       builder->CreateStructGEP(promiseTy, promisePtr, 1, "await.err.msg.ptr"),
                                       "await.err.msg");
    auto* errType = builder->CreateLoad(builder->getPtrTy(),
                                        builder->CreateStructGEP(promiseTy, promisePtr, 2, "await.err.type.ptr"),
                                        "await.err.type");

    auto* coroDestroyFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_destroy);
    builder->CreateCall(coroDestroyFn, {handle});

    emit_await_error_check(errTag, errMsg, errType, loc);

    return result;
}

llvm::Value* Generator::generate_await_expression(const AwaitExpression& expr)
{
    // Generate the operand (should be a call to an async function returning a coroutine handle)
    llvm::Value* handle = generate_expression(*expr.operand);

    // Determine the result type from the called async function
    // The operand should be a function call; look up the function's declared return type
    llvm::Type* resultType = nullptr;

    if (auto* funcCall = dynamic_cast<const FunctionCall*>(expr.operand.get()))
    {
        // Look up the function name in the symbol table to find its declared return type
        const std::string& funcName = funcCall->name.token_name;

        // Try direct lookup
        if (auto funcSym = symbols->lookupFunction(funcName))
        {
            auto* fSym = dynamic_cast<FunctionSymbol*>(funcSym.get());
            if (fSym && fSym->isAsync)
            {
                resultType = generate_type(fSym->returnType);
            }
        }

        // Try method call
        if (!resultType && funcCall->isMethodCall())
        {
            // For method calls, we need to look up the method on the struct
            // The result type comes from the method's return type
            // For now, just try to find it via the symbols table
            for (const auto& [name, sym] : symbols->symbols())
            {
                if (auto* structSym = dynamic_cast<StructSymbol*>(sym.get()))
                {
                    if (auto methodSym = structSym->getMethod(funcName))
                    {
                        if (methodSym->isAsync)
                        {
                            resultType = generate_type(methodSym->returnType);
                            break;
                        }
                    }
                }
            }
        }

        // Also try resolved alias
        if (!resultType)
        {
            const std::string resolved = currentScope->resolve_alias(funcName);
            if (auto funcSym2 = symbols->lookupFunction(resolved))
            {
                auto* fSym2 = dynamic_cast<FunctionSymbol*>(funcSym2.get());
                if (fSym2 && fSym2->isAsync)
                {
                    resultType = generate_type(fSym2->returnType);
                }
            }
        }
    }

    if (!resultType)
    {
        // Fallback: if we can't determine the type, assume void
        resultType = builder->getVoidTy();
    }

    // In async context: use proper suspend/resume via event loop
    // In sync context: fallback to busy-loop
    if (inAsyncFunction)
    {
        return generate_await_in_async(handle, resultType, expr.location);
    }
    return generate_await_loop(handle, resultType, expr.location);
}