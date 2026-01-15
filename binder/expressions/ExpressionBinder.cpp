//
// Expression dispatcher - routes to specific binders
//

#include "../Binder.h"

void Binder::bindExpression(const Expression &expr) {
    if (const auto *id = dynamic_cast<const Identifier *>(&expr)) {
        bindIdentifier(*id);
    } else if (const auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        bindFunctionCall(*call);
    } else if (const auto *access = dynamic_cast<const FieldAccess *>(&expr)) {
        bindFieldAccess(*access);
    } else if (const auto *fieldAssign = dynamic_cast<const FieldAssignment *>(&expr)) {
        bindFieldAssignment(*fieldAssign);
    } else if (const auto *binary = dynamic_cast<const BinaryExpression *>(&expr)) {
        bindBinaryExpression(*binary);
    } else if (const auto *unary = dynamic_cast<const UnaryExpression *>(&expr)) {
        bindUnaryExpression(*unary);
    } else if (const auto *varDecl = dynamic_cast<const VariableDeclaration *>(&expr)) {
        bindVariableDeclaration(*varDecl);
    } else if (const auto *varInit = dynamic_cast<const VariableInit *>(&expr)) {
        bindVariableInit(*varInit);
    } else if (const auto *assign = dynamic_cast<const Assignment *>(&expr)) {
        bindAssignment(*assign);
    } else if (const auto *braceInit = dynamic_cast<const BraceInitializer *>(&expr)) {
        bindBraceInitializer(*braceInit);
    }
}