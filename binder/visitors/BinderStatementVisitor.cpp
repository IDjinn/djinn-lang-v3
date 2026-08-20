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
        if (stmt.flatten)
        {
            _binder.bindBlock(stmt);
            return;
        }
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

    void BinderStatementVisitor::visit(const RangeForStatement& stmt)
    {
        _binder.bindRangeForStatement(stmt);
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

    void BinderStatementVisitor::visit(const SpawnStatement& stmt)
    {
        if (stmt.expression)
        {
            _binder.bindExpression(*stmt.expression);
        }
    }

    void BinderStatementVisitor::visit(const ThrowStatement& stmt)
    {
        if (!_binder.currentFunctionThrows_)
        {
            _binder._diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::THROW_OUTSIDE_THROWS,
                "'throw' statement outside of a 'throws' function",
                stmt.expression ? stmt.expression->location : SourceLocation{}
            ));
        }
        std::shared_ptr<Symbol> thrownSym;
        if (stmt.expression)
        {
            thrownSym = _binder.bindExpression(*stmt.expression);
        }

        // Resolve the thrown error type and validate it against the throws clause
        std::string thrownTypeName;
        if (stmt.expression)
        {
            if (const auto* call = dynamic_cast<const FunctionCall*>(stmt.expression.get()))
            {
                if (const auto errStruct = _binder._global_scope->lookupStruct(call->name.token_name);
                    errStruct && errStruct->isErrorType)
                {
                    thrownTypeName = errStruct->name;
                }
            }
            if (thrownTypeName.empty() && thrownSym && thrownSym->type.kind == TypeKind::STRUCT)
            {
                thrownTypeName = thrownSym->type.structName;
            }
        }

        if (thrownTypeName.empty()) return;

        const auto thrownStruct = _binder._global_scope->lookupStruct(thrownTypeName);
        if (!thrownStruct || !thrownStruct->isErrorType)
        {
            _binder._diagnostics.emitAndPrint(Diagnostic(
                Severity::Error, DiagnosticCode::THROWS_TYPE_MISMATCH,
                "can only throw error types (structs deriving from 'Exception'), got '" + thrownTypeName + "'",
                stmt.expression ? stmt.expression->location : SourceLocation{}
            ));
            return;
        }

        if (!_binder.currentFunctionThrowsAny_ && !_binder.currentFunctionThrowsTypes_.empty())
        {
            bool covered = false;
            for (const auto& t : _binder.currentFunctionThrowsTypes_)
            {
                if (t.kind == TypeKind::STRUCT && _binder.is_error_derived_from(thrownTypeName, t.structName))
                {
                    covered = true;
                    break;
                }
            }
            if (!covered)
            {
                _binder._diagnostics.emitAndPrint(Diagnostic(
                    Severity::Error, DiagnosticCode::THROWS_TYPE_MISMATCH,
                    "thrown error '" + thrownTypeName + "' is not covered by the function's throws clause",
                    stmt.expression ? stmt.expression->location : SourceLocation{}
                ));
            }
        }
    }
} // namespace djinn