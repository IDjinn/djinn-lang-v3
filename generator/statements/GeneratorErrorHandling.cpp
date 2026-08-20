#include "../Generator.h"
#include "../../utils/Logger.h"
#include "../../binder/ErrorTypes.h"

llvm::StructType* djinn_error_value_type(llvm::LLVMContext& context, llvm::IRBuilder<>& builder)
{
    return llvm::StructType::get(context, {builder.getInt32Ty(), builder.getPtrTy()});
}

void Generator::ensure_error_globals_declared()
{
    if (errorFlagGlobal) return;

    errorFlagGlobal = new llvm::GlobalVariable(
        *module,
        builder->getInt1Ty(),
        false,
        llvm::GlobalVariable::InternalLinkage,
        llvm::ConstantInt::get(builder->getInt1Ty(), 0),
        "__djinn_error_flag"
    );

    errorTagGlobal = new llvm::GlobalVariable(
        *module,
        builder->getInt32Ty(),
        false,
        llvm::GlobalVariable::InternalLinkage,
        llvm::ConstantInt::get(builder->getInt32Ty(), 0),
        "__djinn_error_tag"
    );

    errorPayloadGlobal = new llvm::GlobalVariable(
        *module,
        builder->getPtrTy(),
        false,
        llvm::GlobalVariable::InternalLinkage,
        llvm::ConstantPointerNull::get(builder->getPtrTy()),
        "__djinn_error_payload"
    );
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

// Error values have the layout { i32 tag, i8* message }
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

    return alloca;
}

// Emits a Console.format(fmt, ...values) call for an interpolated error
// message; arg 0 is the format string, the rest are the values to box.
llvm::Value* Generator::generate_interpolated_error_message(const FunctionCall& call)
{
    auto* fmtFn = resolve_static_method_function("Console", "format");
    if (!fmtFn)
    {
        GENERATOR_ERROR(
            DiagnosticCode::UNDEFINED_FUNCTION,
            "Console.format is required for interpolated error messages",
            call.name.location
        );
    }

    llvm::Value* fmtVal = generate_expression(*call.arguments[0]);
    llvm::Type* expectedType = fmtFn->getFunctionType()->getParamType(0);
    if (fmtVal->getType()->isPointerTy() && expectedType->isStructTy())
    {
        fmtVal = builder->CreateLoad(expectedType, fmtVal, "fmt_load");
    }
    else
    {
        fmtVal = cast_value(fmtVal, expectedType);
    }

    llvm::Value* varargs = emit_boxed_varargs_array(call.arguments, 1);
    return builder->CreateCall(fmtFn, {fmtVal, varargs}, "err_fmt");
}

void Generator::generate_throw_statement(const ThrowStatement& stmt)
{
    ensure_error_globals_declared();

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
            auto* tagVal = builder->CreateLoad(builder->getInt32Ty(), tagPtr, "throw_tag");
            builder->CreateStore(tagVal, errorTagGlobal, true);
        }

        if (errType->getNumElements() >= 2)
        {
            auto* msgPtr = builder->CreateStructGEP(errType, errPtr, 1, "throw_msg_ptr");
            auto* msgVal = builder->CreateLoad(builder->getPtrTy(), msgPtr, "throw_msg");
            builder->CreateStore(msgVal, errorPayloadGlobal, true);
        }
    }

    builder->CreateStore(llvm::ConstantInt::get(builder->getInt1Ty(), 1), errorFlagGlobal, true);

    emit_all_scope_cleanup();

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

// After a call to a throwing function inside another throwing function
// (unchecked call sites), re-throw when the callee failed.
void Generator::emit_error_propagation_check()
{
    ensure_error_globals_declared();

    auto* errorFlag = builder->CreateLoad(builder->getInt1Ty(), errorFlagGlobal, true, "prop_err_flag");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* contBB = llvm::BasicBlock::Create(*context, "prop.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "prop.err", llvmFunc);

    builder->CreateCondBr(errorFlag, errBB, contBB);

    builder->SetInsertPoint(errBB);
    emit_all_scope_cleanup();
    llvm::Type* returnType = currentFunction ? currentFunction->getReturnType() : builder->getInt32Ty();
    if (returnType->isVoidTy())
    {
        builder->CreateRetVoid();
    }
    else
    {
        builder->CreateRet(get_default_value(returnType));
    }

    builder->SetInsertPoint(contBB);
}

// Division/remission by zero: throws DivisionByZero in throwing functions,
// traps via __djinn_runtime_error otherwise.
void Generator::emit_div_by_zero_check(llvm::Value* divisor)
{
    ensure_error_globals_declared();

    auto* isZero = builder->CreateICmpEQ(divisor, llvm::ConstantInt::get(divisor->getType(), 0), "div_zero");

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "div.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "div.zero", llvmFunc);

    builder->CreateCondBr(isZero, errBB, okBB);

    builder->SetInsertPoint(errBB);

    if (currentFunctionThrows)
    {
        builder->CreateStore(
            llvm::ConstantInt::get(builder->getInt32Ty(), djinn::errors::builtin_error_tag("DivisionByZero")),
            errorTagGlobal, true);
        builder->CreateStore(llvm::ConstantInt::get(builder->getInt1Ty(), 1), errorFlagGlobal, true);
        emit_all_scope_cleanup();
        llvm::Type* returnType = currentFunction ? currentFunction->getReturnType() : builder->getInt32Ty();
        if (returnType->isVoidTy())
            builder->CreateRetVoid();
        else
            builder->CreateRet(get_default_value(returnType));
    }
    else
    {
        // Trap with a message so non-throwing code fails loudly instead of UB
        auto* trapFn = module->getFunction("__djinn_runtime_error");
        if (!trapFn)
        {
            auto* trapType = llvm::FunctionType::get(builder->getVoidTy(), {builder->getPtrTy()}, false);
            trapFn = llvm::Function::Create(trapType, llvm::Function::ExternalLinkage,
                                            "__djinn_runtime_error", *module);
        }
        auto* msg = builder->CreateGlobalStringPtr("division by zero", "divz_msg");
        builder->CreateCall(trapFn, {msg});
        builder->CreateUnreachable();
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

// Require checks: evaluate each precondition at function entry and throw
// ContractViolation when one fails.
void Generator::emit_contract_requirements()
{
    for (const auto& contract : currentContracts_)
    {
        if (!contract || !contract->isRequire() || !contract->condition) continue;

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
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("ContractViolation"));

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
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("ContractViolation"));

        builder->SetInsertPoint(okBB);
    }
}

// Marks the error flag/tag and returns the function's default value,
// terminating the current path (used by throw and contract violations).
void Generator::emit_error_throw_with_tag(const int32_t tag)
{
    ensure_error_globals_declared();

    builder->CreateStore(builder->getInt32(tag), errorTagGlobal, true);
    builder->CreateStore(llvm::ConstantInt::get(builder->getInt1Ty(), 1), errorFlagGlobal, true);

    emit_all_scope_cleanup();

    llvm::Type* returnType = currentFunction ? currentFunction->getReturnType() : builder->getInt32Ty();
    if (returnType->isVoidTy())
        builder->CreateRetVoid();
    else
        builder->CreateRet(get_default_value(returnType));
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

    builder->CreateStore(llvm::ConstantInt::get(builder->getInt1Ty(), 0), errorFlagGlobal, true);

    const bool prevInsideTry = insideTryOperand_;
    insideTryOperand_ = true;
    auto* callResult = generate_expression(*expr.expr);
    insideTryOperand_ = prevInsideTry;

    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(callResult))
    {
        callResult = builder->CreateLoad(alloca->getAllocatedType(), alloca, "try_load");
    }

    auto* errorFlag = builder->CreateLoad(builder->getInt1Ty(), errorFlagGlobal, true, "try_err_flag");

    if (!expr.fallback)
    {
        if (currentFunctionThrows)
        {
            // No fallback: propagate the error to this function's callers
            auto* llvmFunc = builder->GetInsertBlock()->getParent();
            auto* okBB = llvm::BasicBlock::Create(*context, "try.prop.ok", llvmFunc);
            auto* errBB = llvm::BasicBlock::Create(*context, "try.prop.err", llvmFunc);

            builder->CreateCondBr(errorFlag, errBB, okBB);

            builder->SetInsertPoint(errBB);
            emit_all_scope_cleanup();
            llvm::Type* returnType = currentFunction ? currentFunction->getReturnType() : builder->getInt32Ty();
            if (returnType->isVoidTy())
                builder->CreateRetVoid();
            else
                builder->CreateRet(get_default_value(returnType));

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
    builder->CreateStore(llvm::ConstantInt::get(builder->getInt1Ty(), 0), errorFlagGlobal, true);
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
