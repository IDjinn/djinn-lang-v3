//
// Mode-aware integer arithmetic for the w/t/c/s overflow suffixes
//

#include "../Generator.h"
#include "llvm/IR/Intrinsics.h"
#include "../../binder/ErrorTypes.h"

namespace
{
    llvm::Intrinsic::ID withOverflowIntrinsic(const TokenType op, const bool isSigned)
    {
        if (op == TokenType::PLUS)
            return isSigned ? llvm::Intrinsic::sadd_with_overflow : llvm::Intrinsic::uadd_with_overflow;
        if (op == TokenType::MINUS)
            return isSigned ? llvm::Intrinsic::ssub_with_overflow : llvm::Intrinsic::usub_with_overflow;
        return isSigned ? llvm::Intrinsic::smul_with_overflow : llvm::Intrinsic::umul_with_overflow;
    }

    // Only signed div/rem can overflow: INT_MIN / -1 and INT_MIN % -1
    llvm::Value* signedDivRemOverflowBit(llvm::IRBuilder<>& builder, llvm::Value* left, llvm::Value* right)
    {
        auto* ty = left->getType();
        const unsigned bits = ty->getIntegerBitWidth();
        auto* isMinusOne = builder.CreateICmpEQ(
            right, llvm::ConstantInt::get(ty, llvm::APInt::getAllOnes(bits)), "ovf.m1");
        auto* isMin = builder.CreateICmpEQ(
            left, llvm::ConstantInt::get(ty, llvm::APInt::getSignedMinValue(bits)), "ovf.min");
        return builder.CreateAnd(isMinusOne, isMin, "ovf.divbit");
    }
}

llvm::Function* Generator::get_or_declare_runtime_error_fn()
{
    if (auto* fn = module->getFunction("__djinn_runtime_error")) return fn;
    auto* fnType = llvm::FunctionType::get(builder->getVoidTy(), {builder->getPtrTy()}, false);
    return llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, "__djinn_runtime_error", *module);
}

// Trapped: abort via __djinn_runtime_error on overflow.
// Checked: raise the builtin Overflow error through the error-flag mechanism
// (the binder guarantees the enclosing function declares 'throws').
namespace
{
    // Descriptor opcode for the runtime error report ('n' = negate)
    char trapOpCode(const TokenType op)
    {
        switch (op)
        {
        case TokenType::PLUS: return '+';
        case TokenType::MINUS: return '-';
        case TokenType::STAR: return '*';
        case TokenType::SLASH: return '/';
        case TokenType::PERCENT: return '%';
        default: return 0;
        }
    }
}

llvm::Value* Generator::emit_int_arith_with_overflow(const TokenType op, const TrapOperand& leftOp,
                                                     const TrapOperand& rightOp, const bool isSigned,
                                                     const OverflowMode mode, const SourceLocation& loc)
{
    auto* left = leftOp.value;
    auto* right = rightOp.value;
    auto* ty = left->getType();

    llvm::Value* result;
    llvm::Value* overflowBit;

    if (op == TokenType::SLASH || op == TokenType::PERCENT)
    {
        if (!isSigned)
        {
            // unsigned div/rem never overflow
            return op == TokenType::SLASH
                       ? builder->CreateSDiv(left, right, "divtmp")
                       : builder->CreateSRem(left, right, "modtmp");
        }

        overflowBit = signedDivRemOverflowBit(*builder, left, right);
        // div/rem is emitted in the ok-block below: executing it before the
        // overflow check would fault on INT_MIN / -1 before the trap runs
    }
    else
    {
        auto* fn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), withOverflowIntrinsic(op, isSigned), {ty});
        auto* pair = builder->CreateCall(fn, {left, right}, "ovf");
        result = builder->CreateExtractValue(pair, 0, "ovf.result");
        overflowBit = builder->CreateExtractValue(pair, 1, "ovf.bit");
    }

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "ovf.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "ovf.err", llvmFunc);

    builder->CreateCondBr(overflowBit, errBB, okBB);

    builder->SetInsertPoint(errBB);
    if (mode == OverflowMode::Checked)
    {
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("Overflow"));
    }
    else
    {
        emit_runtime_error_trap(loc, "integer overflow", trapOpCode(op), leftOp, rightOp, isSigned);
    }

    builder->SetInsertPoint(okBB);
    if (op == TokenType::SLASH)
        result = builder->CreateSDiv(left, right, "divtmp");
    else if (op == TokenType::PERCENT)
        result = builder->CreateSRem(left, right, "modtmp");
    return result;
}

llvm::Value* Generator::emit_saturating_int_arith(const TokenType op, llvm::Value* left, llvm::Value* right,
                                                  const bool isSigned)
{
    auto* ty = left->getType();
    const unsigned bits = ty->getIntegerBitWidth();

    auto* maxV = llvm::ConstantInt::get(ty, llvm::APInt::getSignedMaxValue(bits));
    auto* minV = llvm::ConstantInt::get(ty, llvm::APInt::getSignedMinValue(bits));
    auto* umaxV = llvm::ConstantInt::get(ty, llvm::APInt::getAllOnes(bits));

    switch (op)
    {
    case TokenType::PLUS:
        {
            const auto id = isSigned ? llvm::Intrinsic::sadd_sat : llvm::Intrinsic::uadd_sat;
            auto* fn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id, {ty});
            return builder->CreateCall(fn, {left, right}, "sat.add");
        }
    case TokenType::MINUS:
        {
            const auto id = isSigned ? llvm::Intrinsic::ssub_sat : llvm::Intrinsic::usub_sat;
            auto* fn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id, {ty});
            return builder->CreateCall(fn, {left, right}, "sat.sub");
        }
    case TokenType::STAR:
        {
            const auto id = isSigned ? llvm::Intrinsic::smul_with_overflow : llvm::Intrinsic::umul_with_overflow;
            auto* fn = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id, {ty});
            auto* pair = builder->CreateCall(fn, {left, right}, "sat.mul");
            auto* result = builder->CreateExtractValue(pair, 0, "sat.result");
            auto* overflowBit = builder->CreateExtractValue(pair, 1, "sat.bit");

            llvm::Value* clamped;
            if (isSigned)
            {
                // Different signs -> negative overflow -> MIN; same signs -> MAX
                auto* signsDiffer = builder->CreateICmpSLT(
                    builder->CreateXor(left, right, "sat.signs"), llvm::ConstantInt::get(ty, 0), "sat.neg");
                clamped = builder->CreateSelect(signsDiffer, minV, maxV, "sat.clamp");
            }
            else
            {
                clamped = umaxV;
            }
            return builder->CreateSelect(overflowBit, clamped, result, "sat.mul.result");
        }
    case TokenType::SLASH:
        {
            if (!isSigned) return builder->CreateSDiv(left, right, "divtmp");

            auto* overflowBit = signedDivRemOverflowBit(*builder, left, right);
            auto* result = builder->CreateSDiv(left, right, "divtmp");
            // |INT_MIN| overflows positively -> MAX
            return builder->CreateSelect(overflowBit, maxV, result, "sat.div.result");
        }
    case TokenType::PERCENT:
        {
            if (!isSigned) return builder->CreateSRem(left, right, "modtmp");

            auto* overflowBit = signedDivRemOverflowBit(*builder, left, right);
            auto* result = builder->CreateSRem(left, right, "modtmp");
            // INT_MIN % -1 == 0 mathematically
            auto* zero = llvm::ConstantInt::get(ty, 0);
            return builder->CreateSelect(overflowBit, zero, result, "sat.rem.result");
        }
    default:
        throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador não suportado em modo saturating");
    }
}

// Unary negate: only signed MIN overflows (0 - MIN > MAX)
llvm::Value* Generator::emit_int_neg_with_overflow(const TrapOperand& operand, const bool isSigned,
                                                   const OverflowMode mode, const SourceLocation& loc)
{
    auto* value = operand.value;
    if (mode == OverflowMode::None || mode == OverflowMode::Wrapped || !isSigned)
    {
        return builder->CreateNeg(value, "negtmp");
    }

    auto* ty = value->getType();
    const unsigned bits = ty->getIntegerBitWidth();
    auto* minV = llvm::ConstantInt::get(ty, llvm::APInt::getSignedMinValue(bits));
    auto* maxV = llvm::ConstantInt::get(ty, llvm::APInt::getSignedMaxValue(bits));
    auto* isMin = builder->CreateICmpEQ(value, minV, "neg.min");

    if (mode == OverflowMode::Saturating)
    {
        auto* negated = builder->CreateNeg(value, "negtmp");
        return builder->CreateSelect(isMin, maxV, negated, "sat.neg");
    }

    auto* llvmFunc = builder->GetInsertBlock()->getParent();
    auto* okBB = llvm::BasicBlock::Create(*context, "neg.ok", llvmFunc);
    auto* errBB = llvm::BasicBlock::Create(*context, "neg.err", llvmFunc);

    builder->CreateCondBr(isMin, errBB, okBB);

    builder->SetInsertPoint(errBB);
    if (mode == OverflowMode::Checked)
    {
        emit_error_throw_with_tag(djinn::errors::builtin_error_tag("Overflow"));
    }
    else
    {
        emit_runtime_error_trap(loc, "integer overflow", 'n', operand, {}, isSigned);
    }

    builder->SetInsertPoint(okBB);
    return builder->CreateNeg(value, "negtmp");
}
