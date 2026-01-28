//
// Binder Expression Visitor Implementation
//

#include "BinderExpressionVisitor.h"
#include "../Binder.h"
#include "../../parser/ast/Expression.h"

namespace djinn {
    void BinderExpressionVisitor::visit(const IntegerLiteral &expr) {
        _result = std::make_shared<IntegerLiteralSymbol>(expr.value, expr.sign, expr.location);
    }

    void BinderExpressionVisitor::visit(const FloatLiteral &expr) {
        _result = std::make_shared<FloatLiteralSymbol>(expr.value, 64, expr.location);
    }

    void BinderExpressionVisitor::visit(const StringLiteral &expr) {
        _result = std::make_shared<StringLiteralSymbol>(expr.value, expr.location);
    }

    void BinderExpressionVisitor::visit(const Identifier &expr) {
        _result = _binder.bindIdentifier(expr);
    }

    void BinderExpressionVisitor::visit(const FunctionCall &expr) {
        _result = _binder.bindFunctionCall(expr);
    }

    void BinderExpressionVisitor::visit(const FieldAccess &expr) {
        _result = _binder.bindFieldAccess(expr);
    }

    void BinderExpressionVisitor::visit(const FieldAssignment &expr) {
        _result = _binder.bindFieldAssignment(expr);
    }

    void BinderExpressionVisitor::visit(const BinaryExpression &expr) {
        _result = _binder.bindBinaryExpression(expr);
    }

    void BinderExpressionVisitor::visit(const UnaryExpression &expr) {
        _result = _binder.bindUnaryExpression(expr);
    }

    void BinderExpressionVisitor::visit(const VariableDeclaration &expr) {
        _result = _binder.bindVariableDeclaration(expr);
    }

    void BinderExpressionVisitor::visit(const VariableInit &expr) {
        _result = _binder.bindVariableInit(expr);
    }

    void BinderExpressionVisitor::visit(const Assignment &expr) {
        _result = _binder.bindAssignment(expr);
    }

    void BinderExpressionVisitor::visit(const BraceInitializer &expr) {
        _result = _binder.bindBraceInitializer(expr);
    }

    void BinderExpressionVisitor::visit(const SwitchExpression &expr) {
        // SwitchExpression binding not yet implemented in binder
        _result = nullptr;
    }

    void BinderExpressionVisitor::visit(const VariadicForward &expr) {
        // VariadicForward is handled by FunctionCall binding
        _result = nullptr;
    }
} // namespace djinn