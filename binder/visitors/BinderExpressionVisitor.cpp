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
        _result = std::make_shared<IntegerLiteralSymbol>(expr.value, expr.sign, expr.location, expr.overflowMode);
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

    void BinderExpressionVisitor::visit(const NullLiteral& expr)
    {
        Type t = Type::pointer(Type(TypeKind::VOID, 0, false));
        t.nullable = true;
        _result = std::make_shared<Symbol>(SymbolKind::Variable, "null", t, expr.location);
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

    void BinderExpressionVisitor::visit(const PostfixExpression& expr)
    {
        _result = _binder.bindPostfixExpression(expr);
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
        if (!expr.targetType.nullable)
        {
            const auto operandType = _binder.inferExpressionType(*expr.operand);
            if (operandType && operandType->nullable)
            {
                _binder._diagnostics.emitAndPrint(Diagnostic(
                    Severity::Warning,
                    DiagnosticCode::TYPE_MISMATCH,
                    std::string("casting from nullable to non-nullable — value will be null-checked at runtime; ")
                    + "use '?" "?' to provide a fallback to avoid this warning",
                    expr.location
                ));
            }
        }
        if (expr.targetType.nonZero && is_zero_literal(*expr.operand))
        {
            _binder._diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                "cannot cast integer literal 0 to non-zero type '" + expr.targetType.toHumanString() + "'",
                expr.location
            ));
            throw CompileError(DiagnosticCode::TYPE_MISMATCH,
                               "cannot cast integer literal 0 to non-zero type '" + expr.targetType.toHumanString() +
                               "'");
        }
        _result = std::make_shared<Symbol>(SymbolKind::Variable, "cast", expr.targetType, expr.location);
    }

    void BinderExpressionVisitor::visit(const AwaitExpression& expr)
    {
        _binder.bindExpression(*expr.operand);
        _result = nullptr;
    }

    void BinderExpressionVisitor::visit(const FixedArrayExpression& expr)
    {
        _binder.bindExpression(*expr.sizeExpr);
        _result = nullptr;
    }

    void BinderExpressionVisitor::visit(const IsExpression& expr)
    {
        auto operand = _binder.bindExpression(*expr.operand);
        if (operand)
        {
            const auto& type = operand->type;
            bool isObject = type.kind == TypeKind::STRUCT &&
                (type.structName == "object" || type.structName == "std::types::object");
            if (!isObject)
            {
                _binder._diagnostics.emitAndPrint(
                    Diagnostic(Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                               "'is' expression requires operand of type 'object', got '" + type.toHumanString() + "'",
                               expr.location));
                throw CompileError(DiagnosticCode::TYPE_MISMATCH,
                                   "'is' expression requires operand of type 'object'");
            }
        }

        if (expr.bindingName)
        {
            auto resolvedType = _binder.resolveType(expr.targetType);
            Type bindType = resolvedType ? *resolvedType : expr.targetType;
            _binder._current_scope->defineVariable(*expr.bindingName, bindType, false);
        }

        _result = std::make_shared<Symbol>(SymbolKind::Variable, "is", Type::integer(1, false), expr.location);
    }

    void BinderExpressionVisitor::visit(const MacroExpansionExpression& expr)
    {
        for (const auto& local : expr.locals)
        {
            auto valueSym = _binder.bindExpression(*local.value);
            _binder.defineMacroLocal(local.varName, valueSym ? valueSym->type : Type::auto_type());
        }
        _result = _binder.bindExpression(*expr.body);
    }

    void BinderExpressionVisitor::visit(const TernaryExpression& expr)
    {
        _binder.bindExpression(*expr.condition);
        auto trueSym = _binder.bindExpression(*expr.trueExpr);
        auto falseSym = _binder.bindExpression(*expr.falseExpr);

        if (trueSym && falseSym)
        {
            const bool compatible = trueSym->type.kind == falseSym->type.kind &&
                (trueSym->type.kind != TypeKind::STRUCT ||
                 trueSym->type.structName == falseSym->type.structName);
            if (!compatible)
            {
                _binder._diagnostics.emitAndPrint(Diagnostic(
                    Severity::Error, DiagnosticCode::TYPE_MISMATCH,
                    "ternary branches have incompatible types '" + trueSym->type.toHumanString() +
                    "' and '" + falseSym->type.toHumanString() + "'",
                    expr.location
                ));
            }
        }
        _result = trueSym;
    }

    void BinderExpressionVisitor::visit(const TryExpression& expr)
    {
        const bool prevInsideTry = _binder.insideTryExpression_;
        const bool prevSawThrowing = _binder.tryOperandSawThrowingCall_;
        _binder.insideTryExpression_ = true;
        _binder.tryOperandSawThrowingCall_ = false;

        auto operandSym = _binder.bindExpression(*expr.expr);

        _binder.insideTryExpression_ = prevInsideTry;
        const bool sawThrowing = _binder.tryOperandSawThrowingCall_;
        _binder.tryOperandSawThrowingCall_ = prevSawThrowing;

        if (!sawThrowing)
        {
            _binder._diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TRY_ON_NON_THROWING,
                "'try' applied to an expression that cannot throw",
                expr.location
            ));
        }

        if (!expr.fallback && !_binder.currentFunctionThrows_)
        {
            _binder._diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::TRY_WITHOUT_FALLBACK,
                "'try' without '?:' fallback is only allowed inside a 'throws' function (to re-throw)",
                expr.location
            ));
        }

        if (expr.fallback)
        {
            _binder.bindExpression(*expr.fallback);
        }
        _result = operandSym;
    }
} // namespace djinn