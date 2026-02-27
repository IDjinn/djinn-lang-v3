//
// Binder Statement Visitor Implementation
//

#include "BinderStatementVisitor.h"
#include "../Binder.h"
#include "../../parser/ast/Statement.h"

namespace djinn
{
    void BinderStatementVisitor::visit(const ExpressionStatement& stmt)
    {
        _binder.bindExpression(*stmt.expression);
    }

    void BinderStatementVisitor::visit(const ReturnStatement& stmt)
    {
        if (stmt.value)
        {
            _binder.bindExpression(*stmt.value);
        }
    }

    void BinderStatementVisitor::visit(const Block& stmt)
    {
        _binder.pushScope();
        _binder.bindBlock(stmt);
        _binder.popScope();
    }

    void BinderStatementVisitor::visit(const IfStatement& stmt)
    {
        _binder.bindIfStatement(stmt);
    }

    void BinderStatementVisitor::visit(const ForStatement& stmt)
    {
        _binder.bindForStatement(stmt);
    }

    void BinderStatementVisitor::visit(const WhileStatement& stmt)
    {
        _binder.bindWhileStatement(stmt);
    }

    void BinderStatementVisitor::visit(const DoWhileStatement& stmt)
    {
        _binder.bindDoWhileStatement(stmt);
    }

    void BinderStatementVisitor::visit(const SwitchStatement& stmt)
    {
        _binder.bindSwitchStatement(stmt);
    }

    void BinderStatementVisitor::visit(const BreakStatement& stmt)
    {
        _binder.validateBreakStatement(stmt);
    }

    void BinderStatementVisitor::visit(const ContinueStatement& stmt)
    {
        _binder.validateContinueStatement(stmt);
    }

    void BinderStatementVisitor::visit(const YieldStatement&/*stmt*/)
    {
        // Validation of "yield only in async" is done in the generator
    }
} // namespace djinn