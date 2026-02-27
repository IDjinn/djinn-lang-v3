//
// Generator Statement Visitor - Generates LLVM IR for statements
//

#ifndef DJINN_GENERATOR_STATEMENT_VISITOR_H
#define DJINN_GENERATOR_STATEMENT_VISITOR_H

#include "../../visitor/StatementVisitor.h"

class Generator;

namespace djinn
{
    class GeneratorStatementVisitor : public IStatementVisitor
    {
        Generator& _generator;

    public:
        explicit GeneratorStatementVisitor(Generator& generator) : _generator(generator)
        {
        }

        void visit(const ExpressionStatement& stmt) override;

        void visit(const ReturnStatement& stmt) override;

        void visit(const Block& stmt) override;

        void visit(const IfStatement& stmt) override;

        void visit(const ForStatement& stmt) override;

        void visit(const WhileStatement& stmt) override;

        void visit(const DoWhileStatement& stmt) override;

        void visit(const SwitchStatement& stmt) override;

        void visit(const BreakStatement& stmt) override;

        void visit(const ContinueStatement& stmt) override;

        void visit(const YieldStatement& stmt) override;

        void visit(const SpawnStatement& stmt) override;
    };
} // namespace djinn

#endif // DJINN_GENERATOR_STATEMENT_VISITOR_H