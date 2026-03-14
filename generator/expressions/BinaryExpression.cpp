//
// Created by Luke on 06/12/2025.
//

#include <llvm/Pass.h>
#include <llvm/Support/Error.h>

#include "../Generator.h"
#include "../../utils/Logger.h"
#include "../../utils/string_utils.h"

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

    // For struct types with Equatable, dispatch == and != to the equals() method
    if (isStruct && (expr.op == TokenType::EQUAL_EQUAL || expr.op == TokenType::BANG_EQUAL))
    {
        const auto* structType = llvm::dyn_cast<llvm::StructType>(left->getType());
        const std::string structTypeName = structType->getName().str();

        // Find the equals method: structName__equals
        const auto mangledName = structTypeName + "__equals";
        const auto it = functions.find(mangledName);
        if (it == functions.end())
        {
            throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                               "struct '" + structTypeName + "' does not have an 'equals' method (implement Equatable)",
                               expr.location);
        }

        llvm::Function* equalsFunc = it->second;

        // equals(this*, other) -> bool
        // Store left in alloca so we can pass pointer as 'this'
        auto* leftAlloca = builder->CreateAlloca(left->getType(), nullptr, "eq_left");
        builder->CreateStore(left, leftAlloca);

        std::vector<llvm::Value*> args = {leftAlloca, right};
        llvm::Value* result = builder->CreateCall(equalsFunc, args, "equals_result");

        // For !=, negate the result
        if (expr.op == TokenType::BANG_EQUAL)
        {
            result = builder->CreateNot(result, "neq_result");
        }
        return result;
    }

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