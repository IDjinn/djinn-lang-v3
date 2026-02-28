//
// [intrinsic] struct method generation — compiler-generated LLVM intrinsics
//

#include "../Generator.h"
#include "llvm/IR/Intrinsics.h"
#include "../../utils/Logger.h"

llvm::Value* Generator::generate_intrinsic_method(
    const FunctionCall& call, const StructDef* def, const std::string& methodName)
{
    // methodName is provided explicitly (from :: syntax or . syntax)
    const std::string& method = methodName;

    // Extract base name (strip namespace prefix)
    std::string baseName = def->name;
    if (const auto pos = baseName.rfind("::"); pos != std::string::npos)
        baseName = baseName.substr(pos + 2);

    LOG_DEBUG("[generator] intrinsic method call: %s::%s", baseName.c_str(), method.c_str());

    if (baseName == "coro")
    {
        return generate_coro_intrinsic(call, method);
    }

    throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                       "unknown intrinsic struct: " + baseName);
}

llvm::Value* Generator::generate_coro_intrinsic(
    const FunctionCall& call, const std::string& method)
{
    if (method == "handle")
    {
        if (!inAsyncFunction || !asyncCoroHandle)
        {
            throw CompileError(DiagnosticCode::UNEXPECTED_TOKEN,
                               "coro::handle() can only be called inside an async function");
        }
        return asyncCoroHandle;
    }

    if (method == "done")
    {
        if (call.arguments.empty())
        {
            throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT,
                               "coro::done() requires 1 argument (coroutine handle)");
        }
        auto* arg = generate_expression(*call.arguments[0]);
        auto* fn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_done);
        return builder->CreateCall(fn, {arg}, "coro.done");
    }

    if (method == "resume")
    {
        if (call.arguments.empty())
        {
            throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT,
                               "coro::resume() requires 1 argument (coroutine handle)");
        }
        auto* arg = generate_expression(*call.arguments[0]);
        auto* fn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_resume);
        return builder->CreateCall(fn, {arg});
    }

    if (method == "destroy")
    {
        if (call.arguments.empty())
        {
            throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT,
                               "coro::destroy() requires 1 argument (coroutine handle)");
        }
        auto* arg = generate_expression(*call.arguments[0]);
        auto* fn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_destroy);
        return builder->CreateCall(fn, {arg});
    }

    if (method == "suspend")
    {
        if (!inAsyncFunction || !asyncCoroHandle)
        {
            throw CompileError(DiagnosticCode::UNEXPECTED_TOKEN,
                               "coro::suspend() can only be called inside an async function");
        }

        // Call __djinn_mark_waiting(handle) — tells event loop not to re-enqueue
        auto* markWaitingFn = module->getFunction("__djinn_mark_waiting");
        if (!markWaitingFn)
        {
            auto* ptrTy = llvm::PointerType::getUnqual(*context);
            auto* ft = llvm::FunctionType::get(builder->getVoidTy(), {ptrTy}, false);
            markWaitingFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                                   "__djinn_mark_waiting", *module);
        }
        builder->CreateCall(markWaitingFn, {asyncCoroHandle});

        // Emit coro.suspend(false) + switch (same pattern as yield)
        auto* coroSuspendFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_suspend);
        llvm::Value* suspResult = builder->CreateCall(coroSuspendFn, {
                                                          llvm::ConstantTokenNone::get(*context),
                                                          builder->getFalse()
                                                      }, "coro.suspend");

        auto* resumeBB = llvm::BasicBlock::Create(*context, "suspend.resume", currentFunction);
        auto* switchInst = builder->CreateSwitch(suspResult, asyncSuspendBB, 2);
        switchInst->addCase(builder->getInt8(0), resumeBB);
        switchInst->addCase(builder->getInt8(1), asyncCleanupBB);

        builder->SetInsertPoint(resumeBB);
        return nullptr; // void
    }

    throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                       "unknown coro intrinsic method: " + method);
}