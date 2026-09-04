//
// Task<T> intrinsic method generation
//

#include "../Generator.h"
#include "llvm/IR/Intrinsics.h"
#include "../../utils/Logger.h"

llvm::Value* Generator::generate_task_intrinsic_method(
    const FunctionCall& call, llvm::AllocaInst* receiverAlloca, const StructDef* def)
{
    // Load handle field from task struct
    // task<T> = { void* handle }
    auto* structTy = receiverAlloca->getAllocatedType();
    auto* handlePtr = builder->CreateStructGEP(structTy, receiverAlloca, 0, "task.handle.ptr");
    auto* ptrTy = llvm::PointerType::getUnqual(*context);
    llvm::Value* handle = builder->CreateLoad(ptrTy, handlePtr, "task.handle");

    const std::string& methodName = call.name.token_name;

    if (methodName == "is_completed")
    {
        auto* doneFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_done);
        return builder->CreateCall(doneFn, {handle}, "task.done");
    }

    if (methodName == "resume")
    {
        auto* resumeFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_resume);
        return builder->CreateCall(resumeFn, {handle});
    }

    if (methodName == "destroy")
    {
        auto* destroyFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_destroy);
        return builder->CreateCall(destroyFn, {handle});
    }

    if (methodName == "get_result")
    {
        // Resolve the result type T from the monomorphized struct fields
        // The struct was monomorphized with type args, so we can figure out T
        // by looking at the struct's fields or the generic context.
        //
        // For task<T>, T is the type the coroutine's promise holds.
        // We need to determine T from the monomorphized struct name.
        // The monomorphized name encodes the type args.
        //
        // Strategy: look at the struct's methods for get_result and extract its return type.
        // Or: since we know the struct fields, and the first field is void* handle,
        // we look at the StructDef's methods to find get_result's declared return type.

        llvm::Type* resultType = nullptr;

        // Try to find the get_result method's return type from the StructDef
        for (const auto& method : def->methods)
        {
            if (method->name == "get_result")
            {
                resultType = generate_type(method->returnType);
                break;
            }
        }

        if (!resultType)
        {
            // Fallback: try i32
            resultType = builder->getInt32Ty();
        }

        auto* promiseFn = llvm::Intrinsic::getOrInsertDeclaration(
            module.get(), llvm::Intrinsic::coro_promise);
        // Promise layout: { err_tag, err_message, err_type_name, value }
        llvm::Value* promise = builder->CreateCall(promiseFn, {
                                                       handle, builder->getInt32(16), builder->getFalse()
                                                   }, "task.promise");
        auto* valuePtr = builder->CreateStructGEP(promise_type(resultType), promise, 3, "task.value.ptr");
        return builder->CreateLoad(resultType, valuePtr, "task.result");
    }

    throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                       "unknown task method: " + methodName);
}