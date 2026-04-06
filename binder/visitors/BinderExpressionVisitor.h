//
// Binder Expression Visitor - Binds expressions and returns Symbol
//

#ifndef DJINN_BINDER_EXPRESSION_VISITOR_H
#define DJINN_BINDER_EXPRESSION_VISITOR_H

#include "../../visitor/ExpressionVisitor.h"
#include "../Symbol.h"
#include <memory>

class Binder;

namespace djinn
{
    class BinderExpressionVisitor : public IExpressionVisitor
    {
        Binder& _binder;
        std::shared_ptr<Symbol> _result;

    public:
        explicit BinderExpressionVisitor(Binder& binder) : _binder(binder)
        {
        }

        [[nodiscard]] std::shared_ptr<Symbol> result() const { return _result; }

        void visit(const IntegerLiteral& expr) override;

        void visit(const FloatLiteral& expr) override;

        void visit(const StringLiteral& expr) override;

        void visit(const BooleanLiteral& expr) override;

        void visit(const Identifier& expr) override;

        void visit(const FunctionCall& expr) override;

        void visit(const FieldAccess& expr) override;

        void visit(const FieldAssignment& expr) override;

        void visit(const BinaryExpression& expr) override;

        void visit(const UnaryExpression& expr) override;

        void visit(const PostfixExpression& expr) override;

        void visit(const VariableDeclaration& expr) override;

        void visit(const VariableInit& expr) override;

        void visit(const Assignment& expr) override;

        void visit(const BraceInitializer& expr) override;

        void visit(const SwitchExpression& expr) override;

        void visit(const VariadicForward& expr) override;

        void visit(const NewExpression& expr) override;

        void visit(const ArrayLiteral& expr) override;

        void visit(const IndexAccess& expr) override;

        void visit(const IndexAssignment& expr) override;

        void visit(const CastExpression& expr) override;

        void visit(const AwaitExpression& expr) override;

        void visit(const FixedArrayExpression& expr) override;

        void visit(const IsExpression& expr) override;

        void visit(const MacroExpansionExpression& expr) override;
    };
} // namespace djinn

#endif // DJINN_BINDER_EXPRESSION_VISITOR_H