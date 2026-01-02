//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_binary_expression(const BinaryExpression &expr) {
    auto *left = generate_expression(*expr.left);
    auto *right = generate_expression(*expr.right);

    if (!left || !right) return nullptr;

    switch (expr.op) {
        case TokenType::PLUS:
            return builder->CreateAdd(left, right, "addtmp");
        case TokenType::MINUS:
            return builder->CreateSub(left, right, "subtmp");
        case TokenType::STAR:
            return builder->CreateMul(left, right, "multmp");
        case TokenType::SLASH:
            return builder->CreateSDiv(left, right, "divtmp");
        case TokenType::PERCENT:
            return builder->CreateSRem(left, right, "modtmp");

        case TokenType::EQUAL_EQUAL:
            return builder->CreateICmpEQ(left, right, "eqtmp");
        case TokenType::BANG_EQUAL:
            return builder->CreateICmpNE(left, right, "netmp");
        case TokenType::LESS:
            return builder->CreateICmpSLT(left, right, "lttmp");
        case TokenType::LESS_EQUAL:
            return builder->CreateICmpSLE(left, right, "letmp");
        case TokenType::GREATER:
            return builder->CreateICmpSGT(left, right, "gttmp");
        case TokenType::GREATER_EQUAL:
            return builder->CreateICmpSGE(left, right, "getmp");

        case TokenType::AND_AND:
            return builder->CreateAnd(left, right, "andtmp");
        case TokenType::OR_OR:
            return builder->CreateOr(left, right, "ortmp");

        default:
            throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador binário não suportado");
    }
}