//
// Expression dispatcher - routes to specific binders
//

#include "../Binder.h"

std::shared_ptr<Symbol> Binder::bindExpression(const Expression &expr) {
    // Literals
    if (const auto *intLit = dynamic_cast<const IntegerLiteral *>(&expr)) {
        return std::make_shared<IntegerLiteralSymbol>(intLit->value, intLit->sign, intLit->location);
    }

    if (const auto *floatLit = dynamic_cast<const FloatLiteral *>(&expr)) {
        return std::make_shared<FloatLiteralSymbol>(floatLit->value, 64, floatLit->location);
    }

    if (const auto *strLit = dynamic_cast<const StringLiteral *>(&expr)) {
        return std::make_shared<StringLiteralSymbol>(strLit->value, strLit->location);
    }

    // Identifiers and calls
    if (const auto *id = dynamic_cast<const Identifier *>(&expr)) {
        return bindIdentifier(*id);
    }

    if (const auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        return bindFunctionCall(*call);
    }

    if (const auto *access = dynamic_cast<const FieldAccess *>(&expr)) {
        return bindFieldAccess(*access);
    }

    if (const auto *fieldAssign = dynamic_cast<const FieldAssignment *>(&expr)) {
        return bindFieldAssignment(*fieldAssign);
    }

    if (const auto *binary = dynamic_cast<const BinaryExpression *>(&expr)) {
        return bindBinaryExpression(*binary);
    }

    if (const auto *unary = dynamic_cast<const UnaryExpression *>(&expr)) {
        return bindUnaryExpression(*unary);
    }

    if (const auto *varDecl = dynamic_cast<const VariableDeclaration *>(&expr)) {
        return bindVariableDeclaration(*varDecl);
    }

    if (const auto *varInit = dynamic_cast<const VariableInit *>(&expr)) {
        return bindVariableInit(*varInit);
    }

    if (const auto *assign = dynamic_cast<const Assignment *>(&expr)) {
        return bindAssignment(*assign);
    }

    if (const auto *braceInit = dynamic_cast<const BraceInitializer *>(&expr)) {
        return bindBraceInitializer(*braceInit);
    }

    // BINDER_ERROR(DiagnosticCode::UNEXPECTED_TOKEN, "unknown expression type", expr, {});
    return nullptr;
}
