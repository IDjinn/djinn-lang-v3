//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_unary_expression(const UnaryExpression &expr) {
    auto *operand = generate_expression(*expr.operand);
    if (!operand) return nullptr;

    switch (expr.op) {
        case TokenType::MINUS:
            return builder->CreateNeg(operand, "negtmp");
        case TokenType::BANG:
            return builder->CreateNot(operand, "nottmp");
        default:
            throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador unário não suportado");
    }
}