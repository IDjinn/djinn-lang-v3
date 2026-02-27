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

    throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                       "unknown coro intrinsic method: " + method);
}
