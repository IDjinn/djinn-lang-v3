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
            builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy(), builder.getPtrTy(),
                                     });
    }

    llvm::Function* get_or_declare_runtime_fn(llvm::Module* module, const char* name, llvm::FunctionType* type)
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

    llvm::Value* global_or_null(llvm::IRBuilder<>& builder, const std::string& text, const char* prefix)
    {
        if (text.empty())
            return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(builder.getPtrTy()));
        return builder.CreateGlobalStringPtr(text, prefix);
    }
}

// Module-level string global dedup: identical texts (file paths, messages,
// variable names) share one private global instead of one per call site.
llvm::Constant* Generator::cached_global_string(const std::string& text, const char* prefix)
{
    if (const auto it = stringGlobalCache.find(text); it != stringGlobalCache.end())
        return it->second;
    auto* global = builder->CreateGlobalStringPtr(text, prefix);
    stringGlobalCache.emplace(text, global);
    return global;
}

// Emits the trap call (__djinn_runtime_error + unreachable) with a fully
// populated error descriptor. Must be called with the insert point on the
// cold error block; operand values are read here, so there is zero cost on
// the happy path.
void Generator::emit_runtime_error_trap(const SourceLocation& loc, const char* message,
                                        const char op, const TrapOperand& left, const TrapOperand& right,
                                        const bool isSigned)
{
    if (runtimeDiagnostics == RuntimeDiagnostics::Minimal)
    {
        emit_runtime_error_trap_min(loc, message, op, left, right, isSigned);
        return;
    }

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
    storePtr(0, cached_global_string(message, "err.msg"));
    storePtr(1, cached_global_string(loc.fileId, "err.file"));
    storePtr(2, builder->CreateGlobalStringPtr(lineText, "err.line"));
    storeInt(3, loc.line);
    storeInt(4, loc.column);
    storeInt(5, loc.length ? loc.length : 1);

    llvm::Value* leftWide = widen_int_operand(*builder, left.value);
    llvm::Value* rightWide = widen_int_operand(*builder, right.value);
    if (leftWide)
    {
        storeInt(6, static_cast<uint8_t>(op));
        storeInt(7, left.value->getType()->getIntegerBitWidth());
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

    // Variable identity for the assignment-history section of the report
    storePtr(12, global_or_null(*builder, left.name, "err.left.var"));
    storePtr(13, left.slot
                     ? left.slot
                     : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(builder->getPtrTy())));
    storePtr(14, global_or_null(*builder, right.name, "err.right.var"));
    storePtr(15, right.slot
                     ? right.slot
                     : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(builder->getPtrTy())));

    builder->CreateCall(trapFn, {info});
    builder->CreateUnreachable();
}

// Minimal (release) trap: no descriptor alloca, no per-site strings beyond the
// shared file/message globals — file:line, operand values and the operator are
// passed as plain scalar arguments and the runtime renders them without the
// source snippet, variable history or stack trace.
void Generator::emit_runtime_error_trap_min(const SourceLocation& loc, const char* message,
                                            const char op, const TrapOperand& left, const TrapOperand& right,
                                            const bool isSigned)
{
    auto* fnType = llvm::FunctionType::get(builder->getVoidTy(),
                                           {
                                               builder->getPtrTy(), builder->getPtrTy(),
                                               builder->getInt32Ty(), builder->getInt32Ty(),
                                               builder->getInt8Ty(), builder->getInt8Ty(),
                                               builder->getInt8Ty(), builder->getInt8Ty(),
                                               builder->getInt64Ty(), builder->getInt64Ty(),
                                           }, false);
    auto* trapFn = get_or_declare_runtime_fn(module.get(), "__djinn_runtime_error_min", fnType);

    llvm::Value* leftWide = widen_int_operand(*builder, left.value);
    llvm::Value* rightWide = widen_int_operand(*builder, right.value);
    const bool hasOperands = leftWide != nullptr;

    const uint64_t bits = hasOperands ? left.value->getType()->getIntegerBitWidth() : 0;

    builder->CreateCall(trapFn, {
        cached_global_string(message, "err.msg"),
        cached_global_string(loc.fileId, "err.file"),
        builder->getInt32(loc.line),
        builder->getInt32(loc.column),
        builder->getInt8(hasOperands ? static_cast<uint8_t>(op) : 0),
        builder->getInt8(static_cast<uint8_t>(bits)),
        builder->getInt8(hasOperands && isSigned ? 1 : 0),
        builder->getInt8(hasOperands ? 1 : 0),
        leftWide ? leftWide : builder->getInt64(0),
        rightWide ? rightWide : builder->getInt64(0),
    });
    builder->CreateUnreachable();
}

// Pushes a shadow-stack frame at function entry; ShadowStackPopPass inserts
// the matching pop before every return. Debug builds only — the frame name and
// file strings plus push/pop calls are what make release binaries bloat.
void Generator::emit_frame_push(const std::string& displayName, const SourceLocation& loc)
{
    if (runtimeDiagnostics == RuntimeDiagnostics::Minimal) return;

    auto* fnType = llvm::FunctionType::get(builder->getVoidTy(),
                                           {builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty()},
                                           false);
    auto* pushFn = get_or_declare_runtime_fn(module.get(), "__djinn_frame_push", fnType);
    builder->CreateCall(pushFn, {
        cached_global_string(displayName, "frame.name"),
        cached_global_string(loc.fileId, "frame.file"),
        builder->getInt32(loc.line),
    });
}

// Updates the current frame's line so the stack trace points at the call site
// instead of the function definition.
void Generator::emit_frame_set_line(const SourceLocation& loc)
{
    if (runtimeDiagnostics == RuntimeDiagnostics::Minimal) return;

    auto* fnType = llvm::FunctionType::get(builder->getVoidTy(), {builder->getInt32Ty()}, false);
    auto* setLineFn = get_or_declare_runtime_fn(module.get(), "__djinn_frame_set_line", fnType);
    builder->CreateCall(setLineFn, {builder->getInt32(loc.line)});
}

// Snapshots the live shadow stack at a throw site: the error unwinds normally
// afterwards, so this is the only chance to record where it came from for the
// uncaught-exception report. Debug builds only (release has no frames).
void Generator::emit_error_stack_capture(const SourceLocation& loc)
{
    if (runtimeDiagnostics == RuntimeDiagnostics::Minimal) return;

    auto* fnType = llvm::FunctionType::get(builder->getVoidTy(), {builder->getInt32Ty()}, false);
    auto* captureFn = get_or_declare_runtime_fn(module.get(), "__djinn_capture_error_stack", fnType);
    builder->CreateCall(captureFn, {builder->getInt32(loc.line)});
}

// Resolves the operand's variable identity: when the expression is a named
// local variable, the slot is its alloca and the runtime can show the last
// assignment sites of that variable in the error report.
TrapOperand Generator::make_trap_operand(const Expression& expr, llvm::Value* value)
{
    TrapOperand operand;
    operand.value = value;
    if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
    {
        if (auto* alloca = currentScope->lookup_variable(ident->identifier.token_name))
        {
            operand.slot = alloca;
            operand.name = ident->identifier.token_name;
        }
    }
    return operand;
}

// Records an assignment site so runtime error reports can show how a
// variable got its current value. Debug builds only — it embeds the source
// line text per site and runs on the hot path.
void Generator::emit_var_track(llvm::Value* slot, const std::string& name, const SourceLocation& loc)
{
    if (runtimeDiagnostics == RuntimeDiagnostics::Minimal || !slot) return;

    auto* fnType = llvm::FunctionType::get(
        builder->getVoidTy(),
        {builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy(), builder->getInt32Ty()}, false);
    auto* trackFn = get_or_declare_runtime_fn(module.get(), "__djinn_var_track", fnType);
    const std::string lineText = _diagnostics.getLine(loc.fileId, loc.line);
    builder->CreateCall(trackFn, {
                            slot,
                            cached_global_string(name, "var.name"),
                            builder->CreateGlobalStringPtr(lineText, "var.line"),
                            builder->getInt32(loc.line),
                        });
}
