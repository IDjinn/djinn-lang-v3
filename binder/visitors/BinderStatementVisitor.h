//
// Binder Statement Visitor - Binds statements
//

#ifndef DJINN_BINDER_STATEMENT_VISITOR_H
#define DJINN_BINDER_STATEMENT_VISITOR_H

#include "../../visitor/StatementVisitor.h"

class Binder;

namespace djinn
{
    class BinderStatementVisitor : public IStatementVisitor
    {
        Binder& _binder;

    public:
        explicit BinderStatementVisitor(Binder& binder) : _binder(binder)
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
    };
} // namespace djinn

#endif // DJINN_BINDER_STATEMENT_VISITOR_H