#include "../Generator.h"
#include "../utils/Logger.h"

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

void Generator::generate_throw_statement(const ThrowStatement& stmt)
{
    ensure_error_globals_declared();

    if (stmt.expression)
    {
        auto exceptionVal = generate_expression(*stmt.expression);

        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(exceptionVal))
        {
            exceptionVal = builder->CreateLoad(alloca->getAllocatedType(), alloca, "throw_load");
        }

        if (auto* structType = llvm::dyn_cast<llvm::StructType>(exceptionVal->getType()))
        {
            auto* tagPtr = builder->CreateStructGEP(structType, exceptionVal, 0, "throw_tag_ptr");
            auto* tagVal = builder->CreateLoad(builder->getInt32Ty(), tagPtr, "throw_tag");
            builder->CreateStore(tagVal, errorTagGlobal);
        }
    }

    builder->CreateStore(llvm::ConstantInt::get(builder->getInt1Ty(), 1), errorFlagGlobal);

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

    builder->CreateStore(llvm::ConstantInt::get(builder->getInt1Ty(), 0), errorFlagGlobal);

    auto* callResult = generate_expression(*expr.expr);

    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(callResult))
    {
        callResult = builder->CreateLoad(alloca->getAllocatedType(), alloca, "try_load");
    }

    auto* errorFlag = builder->CreateLoad(builder->getInt1Ty(), errorFlagGlobal, "try_err_flag");

    if (!expr.fallback)
    {
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
    builder->CreateStore(llvm::ConstantInt::get(builder->getInt1Ty(), 0), errorFlagGlobal);
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
