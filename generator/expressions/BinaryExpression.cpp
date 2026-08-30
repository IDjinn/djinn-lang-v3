//
// Created by Luke on 06/12/2025.
//

#include <llvm/Pass.h>
#include <llvm/Support/Error.h>

#include "../Generator.h"
#include "../../utils/Logger.h"
#include "../../utils/string_utils.h"

// Non-zero analysis for division: a non-zero literal, a variable carrying the
// i32n guarantee (declared type or proven by require(p != 0)) or a cast to a
// non-zero type can never be a zero divisor
bool Generator::is_provably_non_zero(const Expression& expr) const
{
    if (dynamic_cast<const IntegerLiteral*>(&expr))
    {
        return !is_zero_literal(expr);
    }

    if (const auto* cast = dynamic_cast<const CastExpression*>(&expr))
    {
        return cast->targetType.kind == TypeKind::INTEGER && cast->targetType.nonZero;
    }

    if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
    {
        return currentScope->lookup_variable_non_zero(ident->identifier.token_name).value_or(false);
    }

    return false;
}

llvm::Value* Generator::generate_binary_expression(const BinaryExpression& expr)
{
    if (expr.op == TokenType::QUESTION_QUESTION)
    {
        auto* left = generate_expression(*expr.left);
        if (!left) return nullptr;
        if (!left->getType()->isPointerTy())
        {
            return left; // non-nullable: pass-through
        }
        llvm::Function* func = builder->GetInsertBlock()->getParent();
        auto* thenBB = llvm::BasicBlock::Create(*context, "coal.null", func);
        auto* elseBB = llvm::BasicBlock::Create(*context, "coal.notnull", func);
        auto* mergeBB = llvm::BasicBlock::Create(*context, "coal.merge", func);
        auto* nullV = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(left->getType()));
        auto* cond = builder->CreateICmpEQ(left, nullV, "isnull");
        builder->CreateCondBr(cond, thenBB, elseBB);

        builder->SetInsertPoint(thenBB);
        auto* right = generate_expression(*expr.right);
        if (right && right->getType() != left->getType() && right->getType()->isPointerTy())
        {
            right = builder->CreateBitCast(right, left->getType(), "coal.cast");
        }
        auto* thenEnd = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);

        builder->SetInsertPoint(elseBB);
        builder->CreateBr(mergeBB);

        builder->SetInsertPoint(mergeBB);
        auto* phi = builder->CreatePHI(left->getType(), 2, "coal");
        phi->addIncoming(right ? right : left, thenEnd);
        phi->addIncoming(left, elseBB);
        return phi;
    }

    auto* left = generate_expression(*expr.left);
    auto* right = generate_expression(*expr.right);

    if (!left || !right) return nullptr;

    const auto leftType = left->getType();
    if (const auto rightType = right->getType(); leftType != rightType)
    {
        if (leftType->isIntegerTy() && rightType->isIntegerTy())
        {
            const unsigned leftBits = leftType->getIntegerBitWidth();
            const unsigned rightBits = rightType->getIntegerBitWidth();
            if (leftBits > rightBits)
            {
                // Use ZExt for i1 (bool) to avoid sign extension issues (1 in i1 sign-extends to -1)
                if (rightBits == 1)
                {
                    right = builder->CreateZExt(right, leftType, "zext");
                }
                else
                {
                    right = builder->CreateSExt(right, leftType, "sext");
                }
            }
            else
            {
                // Use ZExt for i1 (bool) to avoid sign extension issues
                if (leftBits == 1)
                {
                    left = builder->CreateZExt(left, rightType, "zext");
                }
                else
                {
                    left = builder->CreateSExt(left, rightType, "sext");
                }
            }
        }
        // Pointer arithmetic: ptr +/- int
        else if ((leftType->isPointerTy() && rightType->isIntegerTy()) &&
            (expr.op == TokenType::PLUS || expr.op == TokenType::MINUS))
        {
            llvm::Value* offset = right;
            if (expr.op == TokenType::MINUS)
            {
                offset = builder->CreateNeg(right, "neg_offset");
            }
            return builder->CreateGEP(builder->getInt8Ty(), left, offset, "ptr_arith");
        }
        else if ((rightType->isPointerTy() && leftType->isIntegerTy()) && expr.op == TokenType::PLUS)
        {
            return builder->CreateGEP(builder->getInt8Ty(), right, left, "ptr_arith");
        }
        else
        {
            // Unhandled type mismatch — log before LLVM asserts
            LOG_ERROR("BinaryExpr type mismatch: \n\tleft: %s \n\tright: %s \n\tat line %d col %d",
                      string_utils::llvm_type_str(leftType).c_str(),
                      string_utils::llvm_type_str(rightType).c_str(),
                      expr.location.line, expr.location.column);
        }
    }

    const bool isFloat = left->getType()->isFloatingPointTy();
    const bool isStruct = left->getType()->isStructTy();

    // For struct types, look up operator overloads
    if (isStruct)
    {
        const auto* structType = llvm::dyn_cast<llvm::StructType>(left->getType());
        const std::string structTypeName = structType->getName().str();

        // Map TokenType to canonical operator name
        std::string opCanonical;
        switch (expr.op)
        {
        case TokenType::PLUS: opCanonical = "__op_add";
            break;
        case TokenType::MINUS: opCanonical = "__op_sub";
            break;
        case TokenType::STAR: opCanonical = "__op_mul";
            break;
        case TokenType::SLASH: opCanonical = "__op_div";
            break;
        case TokenType::PERCENT: opCanonical = "__op_mod";
            break;
        case TokenType::EQUAL_EQUAL: opCanonical = "__op_eq";
            break;
        case TokenType::BANG_EQUAL: opCanonical = "__op_neq";
            break;
        case TokenType::LESS: opCanonical = "__op_lt";
            break;
        case TokenType::LESS_EQUAL: opCanonical = "__op_lte";
            break;
        case TokenType::GREATER: opCanonical = "__op_gt";
            break;
        case TokenType::GREATER_EQUAL: opCanonical = "__op_gte";
            break;
        case TokenType::AMPERSAND: opCanonical = "__op_bitand";
            break;
        case TokenType::PIPE: opCanonical = "__op_bitor";
            break;
        case TokenType::CARET: opCanonical = "__op_bitxor";
            break;
        case TokenType::LESS_LESS: opCanonical = "__op_shl";
            break;
        case TokenType::GREATER_GREATER: opCanonical = "__op_shr";
            break;
        case TokenType::AND_AND: opCanonical = "__op_and";
            break;
        case TokenType::OR_OR: opCanonical = "__op_or";
            break;
        default: break;
        }

        if (!opCanonical.empty())
        {
            auto mangledName = structTypeName + "__" + opCanonical;
            auto it = functions.find(mangledName);

            // For !=, fallback to negating ==
            bool negateResult = false;
            if (it == functions.end() && expr.op == TokenType::BANG_EQUAL)
            {
                mangledName = structTypeName + "__" + "__op_eq";
                it = functions.find(mangledName);
                negateResult = true;
            }

            // Also try legacy equals method for backwards compatibility
            if (it == functions.end() && (expr.op == TokenType::EQUAL_EQUAL || expr.op == TokenType::BANG_EQUAL))
            {
                mangledName = structTypeName + "__equals";
                it = functions.find(mangledName);
                negateResult = (expr.op == TokenType::BANG_EQUAL);
            }

            if (it != functions.end())
            {
                llvm::Function* opFunc = it->second;

                // Operators are static: both operands passed as args
                // Check if first param is a pointer (this*) for backwards compat with equals
                std::vector<llvm::Value*> args;
                if (opFunc->arg_size() >= 1 && opFunc->getArg(0)->getType()->isPointerTy())
                {
                    auto* leftAlloca = builder->CreateAlloca(left->getType(), nullptr, "op_left");
                    builder->CreateStore(left, leftAlloca);
                    args.push_back(leftAlloca);
                    args.push_back(right);
                }
                else
                {
                    args.push_back(left);
                    args.push_back(right);
                }

                llvm::Value* result = builder->CreateCall(opFunc, args, "op_result");

                if (negateResult)
                {
                    result = builder->CreateNot(result, "neq_result");
                }
                return result;
            }

            GENERATOR_ERROR(
                DiagnosticCode::OPERATOR_NOT_IMPLEMENTED,
                "struct '" + structTypeName + "' does not implement operator '" + operatorTypeToHumanString(expr.op) +
                "'",
                expr.location
            );
        }
    }

    // Integer overflow modes (w/t/c/s): t/c check and trap/throw, s saturates.
    // Returns nullptr for the default wrapped behavior.
    const auto modeArith = [&](const TokenType op) -> llvm::Value*
    {
        if (expr.overflowMode == OverflowMode::Trapped || expr.overflowMode == OverflowMode::Checked)
        {
            return emit_int_arith_with_overflow(op,
                                                make_trap_operand(*expr.left, left),
                                                make_trap_operand(*expr.right, right),
                                                expr.overflowSigned, expr.overflowMode, expr.location);
        }
        if (expr.overflowMode == OverflowMode::Saturating)
        {
            return emit_saturating_int_arith(op, left, right, expr.overflowSigned);
        }
        return nullptr;
    };

    switch (expr.op)
    {
    case TokenType::PLUS:
        if (isFloat) return builder->CreateFAdd(left, right, "addtmp");
        if (auto* v = modeArith(expr.op)) return v;
        return builder->CreateAdd(left, right, "addtmp");
    case TokenType::MINUS:
        if (isFloat) return builder->CreateFSub(left, right, "subtmp");
        if (auto* v = modeArith(expr.op)) return v;
        return builder->CreateSub(left, right, "subtmp");
    case TokenType::STAR:
        if (isFloat) return builder->CreateFMul(left, right, "multmp");
        if (auto* v = modeArith(expr.op)) return v;
        return builder->CreateMul(left, right, "multmp");
    case TokenType::SLASH:
        if (isFloat) return builder->CreateFDiv(left, right, "divtmp");
        if (!is_provably_non_zero(*expr.right))
        {
            emit_div_by_zero_check(make_trap_operand(*expr.left, left),
                                   make_trap_operand(*expr.right, right), expr.location);
        }
        if (auto* v = modeArith(expr.op)) return v;
        return builder->CreateSDiv(left, right, "divtmp");
    case TokenType::PERCENT:
        if (isFloat) return builder->CreateFRem(left, right, "modtmp");
        if (!is_provably_non_zero(*expr.right))
        {
            emit_div_by_zero_check(make_trap_operand(*expr.left, left),
                                   make_trap_operand(*expr.right, right), expr.location);
        }
        if (auto* v = modeArith(expr.op)) return v;
        return builder->CreateSRem(left, right, "modtmp");

    case TokenType::EQUAL_EQUAL:
        return isFloat
                   ? builder->CreateFCmpOEQ(left, right, "eqtmp")
                   : builder->CreateICmpEQ(left, right, "eqtmp");
    case TokenType::BANG_EQUAL:
        return isFloat
                   ? builder->CreateFCmpONE(left, right, "netmp")
                   : builder->CreateICmpNE(left, right, "netmp");
    case TokenType::LESS:
        return isFloat
                   ? builder->CreateFCmpOLT(left, right, "lttmp")
                   : builder->CreateICmpSLT(left, right, "lttmp");
    case TokenType::LESS_EQUAL:
        return isFloat
                   ? builder->CreateFCmpOLE(left, right, "letmp")
                   : builder->CreateICmpSLE(left, right, "letmp");
    case TokenType::GREATER:
        return isFloat
                   ? builder->CreateFCmpOGT(left, right, "gttmp")
                   : builder->CreateICmpSGT(left, right, "gttmp");
    case TokenType::GREATER_EQUAL:
        return isFloat
                   ? builder->CreateFCmpOGE(left, right, "getmp")
                   : builder->CreateICmpSGE(left, right, "getmp");

    case TokenType::AND_AND:
        return builder->CreateAnd(left, right, "andtmp");
    case TokenType::OR_OR:
        return builder->CreateOr(left, right, "ortmp");

    // Bitwise operators (primitives only — struct dispatch handled above)
    case TokenType::AMPERSAND:
        return builder->CreateAnd(left, right, "bitandtmp");
    case TokenType::PIPE:
        return builder->CreateOr(left, right, "bitortmp");
    case TokenType::CARET:
        return builder->CreateXor(left, right, "bitxortmp");
    case TokenType::LESS_LESS:
        return builder->CreateShl(left, right, "shltmp");
    case TokenType::GREATER_GREATER:
        return builder->CreateAShr(left, right, "shrtmp");

    default:
        throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador binário não suportado");
    }
}
