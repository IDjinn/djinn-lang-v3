#include "../Generator.h"
#include "../ErrorTagMatching.h"
#include "../../utils/Logger.h"
#include "../../binder/ErrorTypes.h"

llvm::StructType* djinn_error_value_type(llvm::LLVMContext& context, llvm::IRBuilder<>& builder)
{
    return llvm::StructType::get(context, {builder.getInt32Ty(), builder.getPtrTy(), builder.getPtrTy()});
}

// Thread-local error state owned by the runtime (single definition shared by
// every generated module — linked libraries included), accessed field-wise.
llvm::StructType* djinn_errno_type(llvm::LLVMContext& context, llvm::IRBuilder<>& builder)
{
    return llvm::StructType::get(context, {
                                     builder.getInt32Ty(), builder.getInt32Ty(), builder.getPtrTy(), builder.getPtrTy(),
                                     builder.getPtrTy(), builder.getInt32Ty(), builder.getInt32Ty(),
                                 });
}

void Generator::ensure_error_globals_declared()
{
    if (errnoGlobal) return;

    errnoGlobal = new llvm::GlobalVariable(
        *module,
        djinn_errno_type(*context, *builder),
        false,
        llvm::GlobalVariable::ExternalLinkage,
        nullptr,
        "__djinn_errno"
    );
    errnoGlobal->setThreadLocal(true);
}

llvm::Value* Generator::errno_field(const unsigned index, const char* name)
{
    auto* ty = djinn_errno_type(*context, *builder);
    return builder->CreateStructGEP(ty, errnoGlobal, index, name);
}

llvm::Value* Generator::errno_load_i32(const unsigned index, const char* name)
{
    return builder->CreateLoad(builder->getInt32Ty(), errno_field(index, ""), true, name);
}

llvm::Value* Generator::errno_load_ptr(const unsigned index, const char* name)
{
    return builder->CreateLoad(builder->getPtrTy(), errno_field(index, ""), true, name);
}

void Generator::errno_store_i32(const unsigned index, llvm::Value* value)
{
    builder->CreateStore(value, errno_field(index, ""), true);
}

void Generator::errno_store_ptr(const unsigned index, llvm::Value* value)
{
    builder->CreateStore(value, errno_field(index, ""), true);
}

void Generator::errno_clear_flag()
{
    errno_store_i32(0, builder->getInt32(0));
}

llvm::Value* Generator::errno_load_flag(const char* name)
{
    auto* raw = errno_load_i32(0, name);
    return builder->CreateICmpNE(raw, builder->getInt32(0), std::string(name) + ".bool");
}

// Leaves the current function with an error in flight. Sync functions in the
// default mode return the default value with the error state left set
// (ordinary returns are the propagation mechanism); in native mode sync
// functions re-throw, unwinding to the caller. Async functions copy the error
// into their promise slot and go to the final suspend — unwinding cannot
// cross suspend points, and the resuming thread's error state may differ.
void Generator::emit_error_return_path()
{
    ensure_error_globals_declared();

    if (inAsyncFunction)
    {
        if (asyncErrSlotPtr)
        {
            auto* promiseTy = llvm::cast<llvm::StructType>(asyncPromiseType);
            builder->CreateStore(errno_load_i32(1, "err.slot.tag"),
                                 builder->CreateStructGEP(promiseTy, asyncErrSlotPtr, 0, "slot.tag"));
            builder->CreateStore(errno_load_ptr(2, "err.slot.msg"),
                                 builder->CreateStructGEP(promiseTy, asyncErrSlotPtr, 1, "slot.msg"));
            builder->CreateStore(errno_load_ptr(3, "err.slot.type"),
                                 builder->CreateStructGEP(promiseTy, asyncErrSlotPtr, 2, "slot.type"));
        }
        emit_all_scope_cleanup();
        if (asyncPromisePtr&& asyncReturnType &&!asyncReturnType->isVoidTy())
        {
            builder->CreateStore(get_default_value(asyncReturnType), asyncPromisePtr);
        }
        builder->CreateBr(asyncFinalSuspendBB);
        return;
    }

    emit_all_scope_cleanup();

    if (nativeExceptions)
    {
        emit_native_throw(errno_load_i32(1, "prop.tag"),
                          errno_load_ptr(2, "prop.msg"),
                          errno_load_ptr(3, "prop.type"));
        return;
    }

    llvm::Type* returnType = currentFunction ? currentFunction->getReturnType() : builder->getInt32Ty();
    if (returnType->isVoidTy())
    {
        builder->CreateRetVoid();
    }
    else
    {
        builder->CreateRet(get_default_value(returnType));
    }
}

llvm::Value* Generator::get_default_value(llvm::Type* type)
{
    if (type->isIntegerTy())
        return llvm::ConstantInt::get(type, 0);
    if (type->isFloatingPointTy())
        return llvm::ConstantFP::get(type, 0.0);
    if (type->isPointerTy())
        return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
    if (type->isVoidTy())
        return nullptr;
    return llvm::Constant::getNullValue(type);
}

std::shared_ptr<StructSymbol> Generator::resolve_error_struct(const std::string& name) const
{
    if (const auto sym = symbols->lookupStruct(name))
    {
        if (sym->isErrorType) return sym;
    }
    return nullptr;
}

// Error values have the layout { i32 tag, i8* message, i8* type_name }
llvm::Value* Generator::generate_error_construction(const FunctionCall& call)
{
    ensure_error_globals_declared();

    const auto errSym = resolve_error_struct(call.name.token_name);
    if (!errSym)
    {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "unknown error type: " + call.name.token_name);
    }

    auto* errType = djinn_error_value_type(*context, *builder);
    auto* alloca = builder->CreateAlloca(errType, nullptr, "err_val");

    auto* tagPtr = builder->CreateStructGEP(errType, alloca, 0, "err_tag_ptr");
    builder->CreateStore(builder->getInt32(errSym->errorTag), tagPtr);

    llvm::Value* msgPtr = llvm::ConstantPointerNull::get(builder->getPtrTy());
    if (call.arguments.size() > 1)
    {
        // Interpolated message: the parser desugared "msg {expr}" into
        // ("msg {0}", expr) — format it at runtime
        msgPtr = coerce_str_to_ptr(generate_interpolated_error_message(call));
    }
    else if (!call.arguments.empty())
    {
        auto* msgVal = generate_expression(*call.arguments[0]);
        msgPtr = coerce_str_to_ptr(msgVal);
    }

    auto* msgFieldPtr = builder->CreateStructGEP(errType, alloca, 1, "err_msg_ptr");
    builder->CreateStore(msgPtr, msgFieldPtr);

    // Type name for uncaught-exception reports; one deduped global per error
    // type, and displayed without the namespace qualification
    auto name = errSym->name;
    if (const auto pos = name.rfind("::"); pos != std::string::npos)
        name = name.substr(pos + 2);
    auto* nameFieldPtr = builder->CreateStructGEP(errType, alloca, 2, "err_type_ptr");
    builder->CreateStore(cached_global_string(name, "err.type"), nameFieldPtr);

    return alloca;
}

// Formats an interpolated error message ("msg {expr}") into the runtime's
// fixed thread-local buffer (__djinn_error_format) — allocation-free, and the
// buffer outlives unwinding so the message pointer stays valid in both error
// modes. Args are boxed exactly like Console.format varargs.
llvm::Value* Generator::generate_interpolated_error_message(const FunctionCall& call)
{
    auto* fmtTy = llvm::FunctionType::get(
        builder->getPtrTy(),
        {builder->getPtrTy(), builder->getInt32Ty(), builder->getPtrTy(), builder->getInt32Ty()},
        false);
    auto* fmtFn = module->getFunction("__djinn_error_format");
    if (!fmtFn)
    {
        fmtFn = llvm::Function::Create(fmtTy, llvm::Function::ExternalLinkage,
                                       "__djinn_error_format", *module);
    }

    auto* fmtVal = generate_expression(*call.arguments[0]);
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(fmtVal))
    {
        if (llvm::isa<llvm::StructType>(alloca->getAllocatedType()))
            fmtVal = builder->CreateLoad(alloca->getAllocatedType(), fmtVal, "fmt_load");
    }
    else if (fmtVal->getType()->isPointerTy())
    {
        fmtVal = builder->CreateLoad(
            llvm::StructType::get(builder->getPtrTy(), builder->getInt32Ty()), fmtVal, "fmt_load");
    }

    llvm::Value* fmtData = builder->CreateExtractValue(fmtVal, 0, "fmt.data");
    llvm::Value* fmtLen = builder->CreateExtractValue(fmtVal, 1, "fmt.len");

    llvm::Value* varargs = emit_boxed_varargs_array(call.arguments, 1);
    auto* objData = builder->CreateExtractValue(varargs, 0, "args.data");
    auto* objCount = builder->CreateExtractValue(varargs, 1, "args.len");

    return builder->CreateCall(fmtFn, {fmtData, fmtLen, objData, objCount}, "err_fmt");
}

void Generator::generate_throw_statement(const ThrowStatement& stmt)
{
    ensure_error_globals_declared();

    llvm::Value* tagVal = builder->getInt32(0);
    llvm::Value* msgVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
    llvm::Value* nameVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

    if (stmt.expression)
    {
        llvm::Value* errPtr = generate_expression(*stmt.expression);

        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(errPtr))
        {
            errPtr = alloca;
        }
        else if (!errPtr->getType()->isPointerTy())
        {
            // Plain struct value: spill it so we can read the tag
            auto* errType = djinn_error_value_type(*context, *builder);
            auto* tmp = builder->CreateAlloca(errType, nullptr, "throw_tmp");
            builder->CreateStore(errPtr, tmp);
            errPtr = tmp;
        }

        llvm::StructType* errType = nullptr;
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(errPtr))
            errType = llvm::cast<llvm::StructType>(alloca->getAllocatedType());
        else
            errType = djinn_error_value_type(*context, *builder);

        if (errType->getNumElements() >= 1)
        {
            auto* tagPtr = builder->CreateStructGEP(errType, errPtr, 0, "throw_tag_ptr");
            tagVal = builder->CreateLoad(builder->getInt32Ty(), tagPtr, "throw_tag");
        }

        if (errType->getNumElements() >= 2)
        {
            auto* msgPtr = builder->CreateStructGEP(errType, errPtr, 1, "throw_msg_ptr");
            msgVal = builder->CreateLoad(builder->getPtrTy(), msgPtr, "throw_msg");
        }

        if (errType->getNumElements() >= 3)
        {
            auto* namePtr = builder->CreateStructGEP(errType, errPtr, 2, "throw_type_ptr");
            nameVal = builder->CreateLoad(builder->getPtrTy(), namePtr, "throw_type");
        }
    }

    store_error_origin(stmt.location);

    if (nativeExceptions)
    {
        // The shim mirrors the error into the thread-local state before the
        // throw, keeping report rendering uniform across both modes
        errno_store_i32(1, tagVal);
        errno_store_ptr(2, msgVal);
        errno_store_ptr(3, nameVal);
        emit_native_throw(tagVal, msgVal, nameVal);
        return;
    }

    emit_error_trace_capture();

    errno_store_i32(0, builder->getInt32(1));
    errno_store_i32(1, tagVal);
    errno_store_ptr(2, msgVal);
    errno_store_ptr(3, nameVal);

    emit_error_return_path();
}

// After a call to a throwing function inside another throwing function
// (unchecked call sites), re-throw when the callee failed. Native mode
// propagates by unwinding instead: the call itself is an invoke and this
// check is not emitted.
void Generator::emit_error_propagation_check(const SourceLocation& loc)
{
    if (nativeExceptions) return;

    ensure_error_globals_declared();

    auto* errorFlag = errno_load_flag("prop_err_flag");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* contBB = llvm::BasicBlock::Create(*context, "prop.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "prop.err", llvmFunc);

    builder->CreateCondBr(errorFlag, errBB, contBB);

    builder->SetInsertPoint(errBB);
    store_error_origin(loc);
    emit_error_return_path();

    builder->SetInsertPoint(contBB);
}

// Reports the error currently held in the error state and aborts: loads
// tag/type/message/origin and tail-calls __djinn_uncaught_error (noreturn).
void Generator::emit_uncaught_error_trap()
{
    ensure_error_globals_declared();

    auto* uncaughtFn = module->getFunction("__djinn_uncaught_error");
    if (!uncaughtFn)
    {
        auto* uncaughtTy = llvm::FunctionType::get(builder->getVoidTy(),
                                                   {
                                                       builder->getInt32Ty(), builder->getPtrTy(),
                                                       builder->getPtrTy(), builder->getPtrTy(),
                                                       builder->getInt32Ty(), builder->getInt32Ty(),
                                                   }, false);
        uncaughtFn = llvm::Function::Create(uncaughtTy, llvm::Function::ExternalLinkage,
                                            "__djinn_uncaught_error", *module);
    }

    auto* tag = errno_load_i32(1, "uncaught_tag");
    auto* name = errno_load_ptr(3, "uncaught_type");
    auto* payload = errno_load_ptr(2, "uncaught_msg");
    auto* originFile = errno_load_ptr(4, "uncaught_file");
    auto* originLine = errno_load_i32(5, "uncaught_line");
    auto* originCol = errno_load_i32(6, "uncaught_col");
    builder->CreateCall(uncaughtFn, {tag, name, payload, originFile, originLine, originCol});
    builder->CreateUnreachable();
}

// After user main returns: an error flag still set means an exception
// escaped main() throws — report it and abort instead of exiting silently.
void Generator::emit_uncaught_error_check()
{
    ensure_error_globals_declared();

    auto* errorFlag = errno_load_flag("uncaught_flag");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "main.err.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "main.err.uncaught", llvmFunc);

    builder->CreateCondBr(errorFlag, errBB, okBB);

    builder->SetInsertPoint(errBB);
    emit_uncaught_error_trap();

    builder->SetInsertPoint(okBB);
}

// Division/remission by zero: throws DivisionByZero in throwing functions,
// traps via __djinn_runtime_error otherwise.
void Generator::emit_div_by_zero_check(const TrapOperand& dividend, const TrapOperand& divisor,
                                       const SourceLocation& loc)
{
    ensure_error_globals_declared();

    auto* isZero = builder->CreateICmpEQ(divisor.value,
                                         llvm::ConstantInt::get(divisor.value->getType(), 0), "div_zero");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "div.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "div.zero", llvmFunc);

    builder->CreateCondBr(isZero, errBB, okBB);

    builder->SetInsertPoint(errBB);

    if (currentFunctionThrows)
    {
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("DivisionByZero"), loc);
    }
    else
    {
        // Trap with a rich report so non-throwing code fails loudly instead of UB
        emit_runtime_error_trap(loc, "division by zero", '/', dividend, divisor, true);
    }

    builder->SetInsertPoint(okBB);
}

// Set up contract state for the function being generated: binds the `return`
// pseudo-variable for ensure clauses and emits require checks at entry.
void Generator::setup_contracts(const std::vector<const ContractClause*>& contracts, llvm::Function* llvmFunc)
{
    currentContracts_ = contracts;
    contractReturnAlloca = nullptr;

    bool hasEnsure = false;
    for (const auto& contract : contracts)
    {
        if (contract && contract->isEnsure() && contract->condition)
        {
            hasEnsure = true;
            break;
        }
    }

    if (hasEnsure && llvmFunc && !llvmFunc->getReturnType()->isVoidTy())
    {
        contractReturnAlloca = builder->CreateAlloca(llvmFunc->getReturnType(), nullptr, "__return");
        currentScope->define_variable("return", contractReturnAlloca, "");
    }

    emit_contract_requirements();
}

// Non-zero parameter entry check: the parameter type is an implicit
// require(param != 0) clause. Throws ContractViolation when violated, which
// is what lets division-by-zero checks inside the body be elided.
void Generator::emit_non_zero_param_check(const std::string& paramName, const Type& paramType)
{
    if (paramType.kind != TypeKind::INTEGER || !paramType.nonZero) return;

    auto* alloca = currentScope->lookup_variable(paramName);
    if (!alloca) return;

    auto* value = builder->CreateLoad(alloca->getAllocatedType(), alloca, paramName + ".nz_load");
    auto* isZero = builder->CreateICmpEQ(
        value, llvm::ConstantInt::get(value->getType(), 0), "nz_zero");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "nz.ok", llvmFunc);
    auto* violBB = llvm::BasicBlock::Create(*context, "nz.violation", llvmFunc);

    builder->CreateCondBr(isZero, violBB, okBB);

    builder->SetInsertPoint(violBB);
    emit_error_throw_with_tag(djinn::errors::builtin_error_tag("ContractViolation"), paramType.location);

    builder->SetInsertPoint(okBB);
}

// Require checks: evaluate each precondition at function entry and throw
// ContractViolation when one fails. A require that merely restates a non-zero
// guarantee (declared i32n or upgraded from require(p != 0)) is skipped —
// the non-zero entry check already covers it.
void Generator::emit_contract_requirements()
{
    for (const auto& contract : currentContracts_)
    {
        if (!contract || !contract->isRequire() || !contract->condition) continue;

        if (const auto proven = non_zero_proven_identifier(*contract->condition);
            proven && currentScope->lookup_variable_non_zero(*proven).value_or(false))
        {
            continue;
        }

        auto* condVal = generate_expression(*contract->condition);
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(condVal))
        {
            condVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "req_cond_load");
        }
        auto* condTrue = builder->CreateICmpNE(
            condVal, llvm::ConstantInt::get(condVal->getType(), 0), "req_cond");

        auto* llvmFunc = builder->GetInsertBlock()->getParent();
        auto* okBB = llvm::BasicBlock::Create(*context, "req.ok", llvmFunc);
        auto* violBB = llvm::BasicBlock::Create(*context, "req.violation", llvmFunc);

        builder->CreateCondBr(condTrue, okBB, violBB);

        builder->SetInsertPoint(violBB);
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("ContractViolation"),
                                  contract->condition->location);

        builder->SetInsertPoint(okBB);
    }
}

// Ensure checks: called right before each return statement (the return value
// has already been stored in the `return` pseudo-variable).
void Generator::emit_contract_ensures()
{
    for (const auto& contract : currentContracts_)
    {
        if (!contract || !contract->isEnsure() || !contract->condition) continue;

        auto* condVal = generate_expression(*contract->condition);
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(condVal))
        {
            condVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "ens_cond_load");
        }
        auto* condTrue = builder->CreateICmpNE(
            condVal, llvm::ConstantInt::get(condVal->getType(), 0), "ens_cond");

        auto* llvmFunc = builder->GetInsertBlock()->getParent();
        auto* okBB = llvm::BasicBlock::Create(*context, "ens.ok", llvmFunc);
        auto* violBB = llvm::BasicBlock::Create(*context, "ens.violation", llvmFunc);

        builder->CreateCondBr(condTrue, okBB, violBB);

        builder->SetInsertPoint(violBB);
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("ContractViolation"),
                                  contract->condition->location);

        builder->SetInsertPoint(okBB);
    }
}

// Records where an error was raised so uncaught-exception reports can point
// at the failing expression even without a stack trace (release builds).
void Generator::store_error_origin(const SourceLocation& loc)
{
    llvm::Value* filePtr = llvm::ConstantPointerNull::get(builder->getPtrTy());
    if (!loc.fileId.empty())
        filePtr = cached_global_string(loc.fileId, "err.origin.file");
    errno_store_ptr(4, filePtr);
    errno_store_i32(5, builder->getInt32(loc.line));
    errno_store_i32(6, builder->getInt32(loc.column));
}

// Marks the error flag/tag and leaves the function with the error in flight
// (used by throw and contract violations).
void Generator::emit_error_throw_with_tag(const int32_t tag, const SourceLocation& loc)
{
    ensure_error_globals_declared();

    store_error_origin(loc);

    if (nativeExceptions)
    {
        errno_store_i32(0, builder->getInt32(1));
        errno_store_i32(1, builder->getInt32(tag));
        // Builtin-tag errors carry no type name of their own — clear any name left
        // by a previous user exception so reports fall back to the builtin table
        errno_store_ptr(3, llvm::ConstantPointerNull::get(builder->getPtrTy()));
        emit_native_throw(builder->getInt32(tag),
                          llvm::ConstantPointerNull::get(builder->getPtrTy()),
                          llvm::ConstantPointerNull::get(builder->getPtrTy()));
        return;
    }

    errno_store_i32(1, builder->getInt32(tag));
    // Builtin-tag errors carry no type name of their own — clear any name left
    // by a previous user exception so reports fall back to the builtin table
    errno_store_ptr(3, llvm::ConstantPointerNull::get(builder->getPtrTy()));
    errno_store_i32(0, builder->getInt32(1));
    emit_error_trace_capture();

    emit_error_return_path();
}

// Block-form try/catch/finally (native mode only). The try body runs with a
// landing whose pads catchret to a normal dispatch block, so handler bodies,
// return/break/continue and nested tries all behave like ordinary code; the
// arms match by error tag exactly like outcome-switch arms, and unmatched
// errors re-throw. `finally` runs inline on every non-unwinding path and once
// more before a re-throw.
void Generator::generate_try_catch_statement(const TryCatchStatement& stmt)
{
    if (!nativeExceptions)
    {
        GENERATOR_ERROR(DiagnosticCode::TRY_CATCH_REQUIRES_EXCEPTIONS,
                        "block-form try/catch requires the exceptions mode ('--exceptions')",
                        stmt.location);
    }
    if (!eh_is_msvc_target())
    {
        GENERATOR_ERROR(DiagnosticCode::TRY_CATCH_REQUIRES_EXCEPTIONS,
                        "native exceptions are not supported on this target yet",
                        stmt.location);
    }

    push_scope();

    auto* func = builder->GetInsertBlock()->getParent();
    auto* dispatchBB = llvm::BasicBlock::Create(*context, "trycatch.dispatch", func);
    auto* contBB = llvm::BasicBlock::Create(*context, "trycatch.cont", func);
    auto* rethrowBB = llvm::BasicBlock::Create(*context, "trycatch.rethrow", func);
    auto* finallyBB = stmt.finallyBlock
                          ? llvm::BasicBlock::Create(*context, "trycatch.finally", func)
                          : contBB;

    const auto landing = push_native_landing(false);

    const bool prevInsideTry = insideTryOperand_;
    insideTryOperand_ = true;
    generate_block(*stmt.tryBlock);
    insideTryOperand_ = prevInsideTry;
    ehLandingStack_.pop_back();

    if (!builder->GetInsertBlock()->getTerminator())
    {
        builder->CreateBr(dispatchBB);
    }

    // Arms match by tag in source order (specific types match derived errors
    // too; Error/_ catch everything) — same semantics as outcome-switch arms
    builder->SetInsertPoint(dispatchBB);
    auto* thrownTag = errno_load_i32(1, "trycatch.tag");

    for (size_t i = 0; i < stmt.catches.size(); ++i)
    {
        const auto& clause = stmt.catches[i];
        auto* armBB = llvm::BasicBlock::Create(*context, "trycatch.arm." + clause.errorType.token_name, func);
        llvm::BasicBlock* nextArmBB = i + 1 < stmt.catches.size()
                                          ? llvm::BasicBlock::Create(*context, "trycatch.next", func)
                                          : rethrowBB;

        const auto errSym = resolve_error_struct(clause.errorType.token_name);
        if (errSym)
        {
            llvm::Value* matched = nullptr;
            for (const int32_t tag : djinn::error_arm_matched_tags(*symbols, *errSym))
            {
                auto* cmp = builder->CreateICmpEQ(thrownTag, builder->getInt32(tag), "trycatch.cmp");
                matched = matched ? builder->CreateOr(matched, cmp) : cmp;
            }
            builder->CreateCondBr(matched, armBB, nextArmBB);
        }
        else
        {
            // "Error" or "_" — catch-all
            builder->CreateBr(armBB);
        }

        builder->SetInsertPoint(armBB);
        errno_clear_flag();

        push_scope();
        if (clause.binding)
        {
            auto* errType = djinn_error_value_type(*context, *builder);
            auto* errAlloca = builder->CreateAlloca(errType, nullptr, clause.binding->token_name);
            builder->CreateStore(thrownTag,
                                 builder->CreateStructGEP(errType, errAlloca, 0, "bind.tag"));
            builder->CreateStore(errno_load_ptr(2, "bind.msg"),
                                 builder->CreateStructGEP(errType, errAlloca, 1, "bind.msg.ptr"));
            builder->CreateStore(errno_load_ptr(3, "bind.type"),
                                 builder->CreateStructGEP(errType, errAlloca, 2, "bind.type.ptr"));
            currentScope->define_variable(clause.binding->token_name, errAlloca);
        }
        generate_block(*clause.body);
        pop_scope();

        if (!builder->GetInsertBlock()->getTerminator())
        {
            builder->CreateBr(finallyBB);
        }

        if (i + 1 < stmt.catches.size())
        {
            builder->SetInsertPoint(nextArmBB);
        }
    }

    // No arm matched (or no catch arms at all — finally-only try): run
    // finally once more, then re-throw
    if (stmt.catches.empty())
    {
        builder->SetInsertPoint(dispatchBB);
        builder->CreateBr(rethrowBB);
    }
    builder->SetInsertPoint(rethrowBB);
    if (stmt.finallyBlock)
    {
        push_scope();
        generate_block(*stmt.finallyBlock);
        pop_scope();
    }
    ensure_error_globals_declared();
    emit_native_throw(errno_load_i32(1, "rethrow.tag"),
                      errno_load_ptr(2, "rethrow.msg"),
                      errno_load_ptr(3, "rethrow.type"));

    if (stmt.finallyBlock)
    {
        builder->SetInsertPoint(finallyBB);
        push_scope();
        generate_block(*stmt.finallyBlock);
        pop_scope();
        if (!builder->GetInsertBlock()->getTerminator())
        {
            builder->CreateBr(contBB);
        }
    }

    // Fill the landing blocks now that the dispatch exists
    finalize_native_landing(landing, dispatchBB);

    builder->SetInsertPoint(contBB);
    pop_scope();
}

// After a child coroutine completes: transfers its promise error slot (the
// values were loaded before the frame was destroyed) into the thread-local
// error state and propagates. Inside a try/switch operand the flag is left
// set for the operand check instead — same rule as throwing calls.
void Generator::emit_await_error_check(llvm::Value* errTag, llvm::Value* errMsg, llvm::Value* errType,
                                       const SourceLocation& loc)
{
    ensure_error_globals_declared();

    auto* hasError = builder->CreateICmpNE(errTag, builder->getInt32(0), "await.haserr");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "await.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "await.err", llvmFunc);

    builder->CreateCondBr(hasError, errBB, okBB);

    builder->SetInsertPoint(errBB);
    errno_store_i32(0, builder->getInt32(1));
    errno_store_i32(1, errTag);
    errno_store_ptr(2, errMsg);
    errno_store_ptr(3, errType);

    if (insideTryOperand_)
    {
        builder->CreateBr(okBB);
    }
    else
    {
        store_error_origin(loc);
        emit_error_return_path();
    }

    builder->SetInsertPoint(okBB);
}

llvm::Value* Generator::generate_ternary_expression(const TernaryExpression& expr)
{
    auto* condVal = generate_expression(*expr.condition);

    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(condVal))
    {
        condVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "ternary_cond_load");
    }

    condVal = builder->CreateICmpNE(
        condVal,
        llvm::ConstantInt::get(condVal->getType(), 0),
        "ternary_cond"
    );

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* trueBB = llvm::BasicBlock::Create(*context, "ternary.true", llvmFunc);
    auto* falseBB = llvm::BasicBlock::Create(*context, "ternary.false", llvmFunc);
    auto* mergeBB = llvm::BasicBlock::Create(*context, "ternary.merge", llvmFunc);

    builder->CreateCondBr(condVal, trueBB, falseBB);

    builder->SetInsertPoint(trueBB);
    auto* trueVal = generate_expression(*expr.trueExpr);
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(trueVal))
    {
        trueVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "ternary_true_load");
    }
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(falseBB);
    auto* falseVal = generate_expression(*expr.falseExpr);
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(falseVal))
    {
        falseVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "ternary_false_load");
    }
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(mergeBB);
    auto* phi = builder->CreatePHI(trueVal->getType(), 2, "ternary_result");
    phi->addIncoming(trueVal, trueBB);
    phi->addIncoming(falseVal, falseBB);
    return phi;
}

llvm::Value* Generator::generate_try_expression(const TryExpression& expr)
{
    ensure_error_globals_declared();

    errno_clear_flag();

    // Native mode: the operand's throwing calls unwind to the landing's
    // pads, which resume here — at a dedicated, side-effect-free check block
    // — with the error state set by the shim.
    NativeLanding landing;
    const bool nativeLanding = nativeExceptions;
    if (nativeLanding)
    {
        landing = push_native_landing(false);
    }

    const bool prevInsideTry = insideTryOperand_;
    insideTryOperand_ = true;
    auto* callResult = generate_expression(*expr.expr);
    insideTryOperand_ = prevInsideTry;

    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(callResult))
    {
        callResult = builder->CreateLoad(alloca->getAllocatedType(), alloca, "try_load");
    }

    llvm::Value* errorFlag = nullptr;
    if (nativeLanding)
    {
        auto* llvmFunc = builder->GetInsertBlock()->getParent();
        auto* checkBB = llvm::BasicBlock::Create(*context, "try.check", llvmFunc);
        builder->CreateBr(checkBB);
        builder->SetInsertPoint(checkBB);
        errorFlag = errno_load_flag("try_err_flag");
        finalize_native_landing(landing, checkBB);
        ehLandingStack_.pop_back();
    }
    else
    {
        errorFlag = errno_load_flag("try_err_flag");
    }

    if (!expr.fallback)
    {
        if (currentFunctionThrows)
        {
            // No fallback: propagate the error to this function's callers,
            // with the origin rising to this unhandled try call site
            auto* llvmFunc = builder->GetInsertBlock()->getParent();
            auto* okBB = llvm::BasicBlock::Create(*context, "try.prop.ok", llvmFunc);
            auto* errBB = llvm::BasicBlock::Create(*context, "try.prop.err", llvmFunc);

            builder->CreateCondBr(errorFlag, errBB, okBB);

            builder->SetInsertPoint(errBB);
            store_error_origin(expr.location);
            emit_error_return_path();

            builder->SetInsertPoint(okBB);
        }
        return callResult;
    }

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "try.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "try.err", llvmFunc);
    auto* mergeBB = llvm::BasicBlock::Create(*context, "try.merge", llvmFunc);

    builder->CreateCondBr(errorFlag, errBB, okBB);

    builder->SetInsertPoint(okBB);
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(errBB);
    errno_clear_flag();
    auto* fallbackVal = generate_expression(*expr.fallback);
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(fallbackVal))
    {
        fallbackVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "try_fb_load");
    }
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(mergeBB);
    auto* phi = builder->CreatePHI(callResult->getType(), 2, "try_result");
    phi->addIncoming(callResult, okBB);
    phi->addIncoming(fallbackVal, errBB);
    return phi;
}
