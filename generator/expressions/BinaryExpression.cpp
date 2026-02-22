//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value* Generator::generate_binary_expression(const BinaryExpression& expr)
{
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
        else if (leftType->isFloatingPointTy() && rightType->isFloatingPointTy())
        {
            if (leftType->getPrimitiveSizeInBits() > rightType->getPrimitiveSizeInBits())
            {
                right = builder->CreateFPExt(right, leftType, "fpext");
            }
            else
            {
                left = builder->CreateFPExt(left, rightType, "fpext");
            }
        }
    }

    const bool isFloat = left->getType()->isFloatingPointTy();

    switch (expr.op)
    {
    case TokenType::PLUS:
        return isFloat
                   ? builder->CreateFAdd(left, right, "addtmp")
                   : builder->CreateAdd(left, right, "addtmp");
    case TokenType::MINUS:
        return isFloat
                   ? builder->CreateFSub(left, right, "subtmp")
                   : builder->CreateSub(left, right, "subtmp");
    case TokenType::STAR:
        return isFloat
                   ? builder->CreateFMul(left, right, "multmp")
                   : builder->CreateMul(left, right, "multmp");
    case TokenType::SLASH:
        return isFloat
                   ? builder->CreateFDiv(left, right, "divtmp")
                   : builder->CreateSDiv(left, right, "divtmp");
    case TokenType::PERCENT:
        return isFloat
                   ? builder->CreateFRem(left, right, "modtmp")
                   : builder->CreateSRem(left, right, "modtmp");

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

    default:
        throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador binário não suportado");
    }
}