//
// Binder Expression Visitor Implementation
//

#include "BinderExpressionVisitor.h"
#include "../Binder.h"
#include "../../parser/ast/Expression.h"

namespace djinn
{
    void BinderExpressionVisitor::visit(const IntegerLiteral& expr)
    {
        _result = std::make_shared<IntegerLiteralSymbol>(expr.value, expr.sign, expr.location);
    }

    void BinderExpressionVisitor::visit(const FloatLiteral& expr)
    {
        _result = std::make_shared<FloatLiteralSymbol>(expr.value, 64, expr.location);
    }

    void BinderExpressionVisitor::visit(const BooleanLiteral& expr)
    {
        _result = std::make_shared<
            IntegerLiteralSymbol>(std::to_string(expr.value == "true" ? 1 : 0), 1, expr.location);
    }

    void BinderExpressionVisitor::visit(const StringLiteral& expr)
    {
        _result = std::make_shared<StringLiteralSymbol>(expr.value, expr.location);
    }

    void BinderExpressionVisitor::visit(const Identifier& expr)
    {
        _result = _binder.bindIdentifier(expr);
    }

    void BinderExpressionVisitor::visit(const FunctionCall& expr)
    {
        _result = _binder.bindFunctionCall(expr);
    }

    void BinderExpressionVisitor::visit(const FieldAccess& expr)
    {
        _result = _binder.bindFieldAccess(expr);
    }

    void BinderExpressionVisitor::visit(const FieldAssignment& expr)
    {
        _result = _binder.bindFieldAssignment(expr);
    }

    void BinderExpressionVisitor::visit(const BinaryExpression& expr)
    {
        _result = _binder.bindBinaryExpression(expr);
    }

    void BinderExpressionVisitor::visit(const UnaryExpression& expr)
    {
        _result = _binder.bindUnaryExpression(expr);
    }

    void BinderExpressionVisitor::visit(const VariableDeclaration& expr)
    {
        _result = _binder.bindVariableDeclaration(expr);
    }

    void BinderExpressionVisitor::visit(const VariableInit& expr)
    {
        _result = _binder.bindVariableInit(expr);
    }

    void BinderExpressionVisitor::visit(const Assignment& expr)
    {
        _result = _binder.bindAssignment(expr);
    }

    void BinderExpressionVisitor::visit(const BraceInitializer& expr)
    {
        _result = _binder.bindBraceInitializer(expr);
    }

    void BinderExpressionVisitor::visit(const SwitchExpression& expr)
    {
        // SwitchExpression binding not yet implemented in binder
        _result = nullptr;
    }

    void BinderExpressionVisitor::visit(const VariadicForward& expr)
    {
        // VariadicForward is handled by FunctionCall binding
        _result = nullptr;
    }

    void BinderExpressionVisitor::visit(const NewExpression& expr)
    {
        _result = _binder.bindNewExpression(expr);
    }

    void BinderExpressionVisitor::visit(const ArrayLiteral& expr)
    {
        for (const auto& elem : expr.elements)
        {
            _binder.bindExpression(*elem);
        }
        _result = nullptr;
    }

    void BinderExpressionVisitor::visit(const IndexAccess& expr)
    {
        // _binder.bindExpression(*expr.object);
        // _binder.bindExpression(*expr.index);
        // _result = nullptr;
        _result = _binder.bindIndexAccess(expr);
    }

    void BinderExpressionVisitor::visit(const IndexAssignment& expr)
    {
        _binder.bindExpression(*expr.object);
        _binder.bindExpression(*expr.index);
        _binder.bindExpression(*expr.value);
        _result = nullptr;
    }

    void BinderExpressionVisitor::visit(const CastExpression& expr)
    {
        auto operand = _binder.bindExpression(*expr.operand);
        _result = std::make_shared<Symbol>(SymbolKind::Variable, "cast", expr.targetType, expr.location);
    }

    void BinderExpressionVisitor::visit(const AwaitExpression& expr)
    {
        _binder.bindExpression(*expr.operand);
        _result = nullptr;
    }
} // namespace djinn