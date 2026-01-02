//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

llvm::Value *Generator::generate_expression(const Expression &expr) {
    if (auto *intLit = dynamic_cast<const IntegerLiteral *>(&expr)) {
        return generate_integer_literal(*intLit);
    }

    if (auto *strLit = dynamic_cast<const StringLiteral *>(&expr)) {
        return generate_string_literal(*strLit);
    }

    if (auto *binExpr = dynamic_cast<const BinaryExpression *>(&expr)) {
        return generate_binary_expression(*binExpr);
    }

    if (auto *unaryExpr = dynamic_cast<const UnaryExpression *>(&expr)) {
        return generate_unary_expression(*unaryExpr);
    }

    if (auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        return generate_function_call(*call);
    }

    if (auto *ident = dynamic_cast<const Identifier *>(&expr)) {
        return generate_identifier(*ident);
    }

    if (auto *varDecl = dynamic_cast<const VariableDeclaration *>(&expr)) {
        return generate_variable_declaration(*varDecl);
    }

    if (auto *fieldAccess = dynamic_cast<const FieldAccess *>(&expr)) {
        return generate_field_access(*fieldAccess);
    }

    if (auto *varInit = dynamic_cast<const VariableInit *>(&expr)) {
        return generate_variable_init(*varInit);
    }

    if (auto *assign = dynamic_cast<const Assignment *>(&expr)) {
        return generate_assignment(*assign);
    }

    if (auto *braceInit = dynamic_cast<const BraceInitializer *>(&expr)) {
        return generate_brace_initializer(*braceInit);
    }

    return nullptr;
}
