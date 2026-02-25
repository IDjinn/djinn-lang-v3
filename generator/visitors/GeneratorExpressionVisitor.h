//
// Generator Expression Visitor - Generates LLVM IR for expressions
//

#ifndef DJINN_GENERATOR_EXPRESSION_VISITOR_H
#define DJINN_GENERATOR_EXPRESSION_VISITOR_H

#include "../../visitor/ExpressionVisitor.h"
#include <llvm/IR/Value.h>

class Generator;

namespace djinn
{
    class GeneratorExpressionVisitor : public IExpressionVisitor
    {
        Generator& _generator;
        llvm::Value* _result = nullptr;

    public:
        explicit GeneratorExpressionVisitor(Generator& generator) : _generator(generator)
        {
        }

        [[nodiscard]] llvm::Value* result() const { return _result; }

        void visit(const IntegerLiteral& expr) override;

        void visit(const FloatLiteral& expr) override;

        void visit(const StringLiteral& expr) override;

        void visit(const Identifier& expr) override;

        void visit(const FunctionCall& expr) override;

        void visit(const FieldAccess& expr) override;

        void visit(const FieldAssignment& expr) override;

        void visit(const BinaryExpression& expr) override;

        void visit(const UnaryExpression& expr) override;

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
    };
} // namespace djinn

#endif // DJINN_GENERATOR_EXPRESSION_VISITOR_H