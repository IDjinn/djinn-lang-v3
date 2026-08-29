//
// Polished runtime errors: rich trap descriptors (source location, operand
// values) and the shadow call-stack frames backing runtime stack traces.
// The descriptor layout must match djinn_error_info_t (runtime/djinn_runtime.h).
//

#include "Generator.h"

namespace
{
    llvm::StructType* error_info_type(llvm::IRBuilder<>& builder)
    {
        return llvm::StructType::get(builder.getContext(), {
            builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy(),
            builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt32Ty(),
            builder.getInt8Ty(), builder.getInt8Ty(), builder.getInt8Ty(), builder.getInt8Ty(),
            builder.getInt64Ty(), builder.getInt64Ty(),
        });
    }

    llvm::Function* get_or_declare_runtime_fn(llvm::Module* module, llvm::IRBuilder<>& builder,
                                              const char* name, llvm::FunctionType* type)
    {
        if (auto* fn = module->getFunction(name)) return fn;
        return llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, *module);
    }

    // Widens an integer operand to i64 keeping its raw two's-complement bits,
    // so the runtime can print it with the right signedness.
    llvm::Value* widen_int_operand(llvm::IRBuilder<>& builder, llvm::Value* value)
    {
        if (!value || !value->getType()->isIntegerTy()) return nullptr;
        const unsigned bits = value->getType()->getIntegerBitWidth();
        if (bits >= 64) return value;
        return builder.CreateZExt(value, builder.getInt64Ty());
    }
}

// Emits the trap call (__djinn_runtime_error + unreachable) with a fully
// populated error descriptor. Must be called with the insert point on the
// cold error block; operand values are read here, so there is zero cost on
// the happy path.
void Generator::emit_runtime_error_trap(const SourceLocation& loc, const char* message,
                                        const char op, llvm::Value* left, llvm::Value* right,
                                        const bool isSigned)
{
    auto* trapFn = get_or_declare_runtime_error_fn();

    auto* infoTy = error_info_type(*builder);
    auto* info = builder->CreateAlloca(infoTy, nullptr, "err_info");

    auto field = [&](const unsigned idx) -> llvm::Value*
    {
        return builder->CreateStructGEP(infoTy, info, idx);
    };
    auto storePtr = [&](const unsigned idx, llvm::Value* ptr)
    {
        builder->CreateStore(ptr, field(idx));
    };
    auto storeInt = [&](const unsigned idx, const uint64_t value)
    {
        const auto* ty = llvm::cast<llvm::IntegerType>(infoTy->getElementType(idx));
        builder->CreateStore(builder->getIntN(ty->getBitWidth(), value), field(idx));
    };

    const std::string lineText = _diagnostics.getLine(loc.fileId, loc.line);
    storePtr(0, builder->CreateGlobalStringPtr(message, "err.msg"));
    storePtr(1, builder->CreateGlobalStringPtr(loc.fileId, "err.file"));
    storePtr(2, builder->CreateGlobalStringPtr(lineText, "err.line"));
    storeInt(3, loc.line);
    storeInt(4, loc.column);
    storeInt(5, loc.length ? loc.length : 1);

    llvm::Value* leftWide = widen_int_operand(*builder, left);
    llvm::Value* rightWide = widen_int_operand(*builder, right);
    if (leftWide)
    {
        storeInt(6, static_cast<uint8_t>(op));
        storeInt(7, left->getType()->getIntegerBitWidth());
        storeInt(8, isSigned ? 1 : 0);
        storeInt(9, 1);
        builder->CreateStore(leftWide, field(10), "err.left");
        builder->CreateStore(rightWide ? rightWide : builder->getInt64(0), field(11), "err.right");
    }
    else
    {
        storeInt(6, 0);
        storeInt(7, 0);
        storeInt(8, 0);
        storeInt(9, 0);
        builder->CreateStore(builder->getInt64(0), field(10), "err.left");
        builder->CreateStore(builder->getInt64(0), field(11), "err.right");
    }

    builder->CreateCall(trapFn, {info});
    builder->CreateUnreachable();
}

// Pushes a shadow-stack frame at function entry; ShadowStackPopPass inserts
// the matching pop before every return.
void Generator::emit_frame_push(const std::string& displayName, const SourceLocation& loc)
{
    auto* fnType = llvm::FunctionType::get(builder->getVoidTy(),
                                           {builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty()},
                                           false);
    auto* pushFn = get_or_declare_runtime_fn(module.get(), *builder, "__djinn_frame_push", fnType);
    builder->CreateCall(pushFn, {
        builder->CreateGlobalStringPtr(displayName, "frame.name"),
        builder->CreateGlobalStringPtr(loc.fileId, "frame.file"),
        builder->getInt32(loc.line),
    });
}

// Updates the current frame's line so the stack trace points at the call site
// instead of the function definition.
void Generator::emit_frame_set_line(const SourceLocation& loc)
{
    auto* fnType = llvm::FunctionType::get(builder->getVoidTy(), {builder->getInt32Ty()}, false);
    auto* setLineFn = get_or_declare_runtime_fn(module.get(), *builder, "__djinn_frame_set_line", fnType);
    builder->CreateCall(setLineFn, {builder->getInt32(loc.line)});
}
