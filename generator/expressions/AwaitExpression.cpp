//
// Await expression code generation — resumes coroutine and extracts result from promise
//

#include "../Generator.h"

llvm::Value* Generator::generate_await_in_async(llvm::Value* childHandle, llvm::Type* resultType)
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

    // 3. await.ready: extract result from child promise
    builder->SetInsertPoint(awaitReadyBB);

    llvm::Value* result = nullptr;
    if (resultType && !resultType->isVoidTy())
    {
        auto* coroPromiseFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_promise);
        unsigned align = module->getDataLayout().getABITypeAlign(resultType).value();
        llvm::Value* promisePtr = builder->CreateCall(coroPromiseFn, {
                                                          childHandle,
                                                          builder->getInt32(align),
                                                          builder->getFalse()
                                                      }, "await.promise");
        result = builder->CreateLoad(resultType, promisePtr, "await.result");
    }

    // 4. Destroy child coroutine frame
    auto* coroDestroyFn = llvm::Intrinsic::getOrInsertDeclaration(
        module.get(), llvm::Intrinsic::coro_destroy);
    builder->CreateCall(coroDestroyFn, {childHandle});

    return result;
}

llvm::Value* Generator::generate_await_loop(llvm::Value* handle, llvm::Type* resultType)
{
    auto* ptrTy = llvm::PointerType::getUnqual(*context);

    // Resume loop: keep resuming until coroutine is done
    auto* awaitLoopBB = llvm::BasicBlock::Create(*context, "await.loop", currentFunction);
    auto* awaitResumeBB = llvm::BasicBlock::Create(*context, "await.resume", currentFunction);
    auto* awaitReadyBB = llvm::BasicBlock::Create(*context, "await.ready", currentFunction);

    builder->CreateBr(awaitLoopBB);

    // --- await.loop ---
    builder->SetInsertPoint(awaitLoopBB);
    auto* coroDoneFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_done);
    llvm::Value* done = builder->CreateCall(coroDoneFn, {handle}, "await.done");
    builder->CreateCondBr(done, awaitReadyBB, awaitResumeBB);

    // --- await.resume ---
    builder->SetInsertPoint(awaitResumeBB);
    auto* coroResumeFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_resume);
    builder->CreateCall(coroResumeFn, {handle});
    builder->CreateBr(awaitLoopBB);

    // --- await.ready ---
    builder->SetInsertPoint(awaitReadyBB);

    llvm::Value* result = nullptr;
    if (resultType && !resultType->isVoidTy())
    {
        // Extract the promise (return value) from the coroutine frame
        // %promise = call ptr @llvm.coro.promise(ptr %hdl, i32 <align>, i1 false)
        auto* coroPromiseFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_promise);
        unsigned align = module->getDataLayout().getABITypeAlign(resultType).value();
        llvm::Value* promisePtr = builder->CreateCall(coroPromiseFn, {
                                                          handle,
                                                          builder->getInt32(align),
                                                          builder->getFalse()
                                                      }, "await.promise");

        result = builder->CreateLoad(resultType, promisePtr, "await.result");
    }

    auto* coroDestroyFn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::coro_destroy);
    builder->CreateCall(coroDestroyFn, {handle});

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
        return generate_await_in_async(handle, resultType);
    }
    return generate_await_loop(handle, resultType);
}