//
// Control flow statements: if, for, while, do-while, switch
//

#include "../Binder.h"
#include "../scope/ScopeGuard.h"

using djinn::binder::ScopeGuard;
using djinn::binder::ControlFlowContext;

void Binder::bindIfStatement(const IfStatement& stmt)
{
    bindExpression(*stmt.condition);
    {
        ScopeGuard guard(*this, djinn::binder::ScopeType::IF);
        if (stmt.thenBranch)
        {
            bindBlock(*stmt.thenBranch);
        }
    }

    if (stmt.elseBranch)
    {
        ScopeGuard guard(*this, djinn::binder::ScopeType::ELSE);
        bindBlock(*stmt.elseBranch);
    }
}

void Binder::bindForStatement(const ForStatement& stmt)
{
    ScopeGuard scopeGuard(*this, djinn::binder::ScopeType::FOR);
    ControlFlowContext::LoopGuard loopGuard(_controlFlow);

    if (stmt.initializer)
    {
        bindExpression(*stmt.initializer);
    }
    if (stmt.condition)
    {
        bindExpression(*stmt.condition);
    }
    if (stmt.postfix)
    {
        bindExpression(*stmt.postfix);
    }
    if (stmt.body)
    {
        bindBlock(*stmt.body);
    }
}

void Binder::bindRangeForStatement(const RangeForStatement& stmt)
{
    ScopeGuard scopeGuard(*this, djinn::binder::ScopeType::FOR);
    ControlFlowContext::LoopGuard loopGuard(_controlFlow);

    _current_scope->defineVariable(stmt.variableName.token_name, stmt.variableType, false);
    bindExpression(*stmt.start);
    bindExpression(*stmt.end);
    if (stmt.body)
    {
        bindBlock(*stmt.body);
    }
}

void Binder::bindWhileStatement(const WhileStatement& stmt)
{
    bindExpression(*stmt.condition);

    ScopeGuard scopeGuard(*this, djinn::binder::ScopeType::WHILE);
    ControlFlowContext::LoopGuard loopGuard(_controlFlow);

    if (stmt.body)
    {
        bindBlock(*stmt.body);
    }
}

void Binder::bindDoWhileStatement(const DoWhileStatement& stmt)
{
    {
        ScopeGuard scopeGuard(*this, djinn::binder::ScopeType::DO_WHILE);
        ControlFlowContext::LoopGuard loopGuard(_controlFlow);

        if (stmt.body)
        {
            bindBlock(*stmt.body);
        }
    }

    bindExpression(*stmt.condition);
}

void Binder::bindSwitchStatement(const SwitchStatement& stmt)
{
    bindExpression(*stmt.value);

    ControlFlowContext::SwitchGuard switchGuard(_controlFlow);

    for (const auto& caseStmt : stmt.cases)
    {
        if (caseStmt->expression)
        {
            bindExpression(*caseStmt->expression);
        }
        if (caseStmt->body)
        {
            ScopeGuard guard(*this, djinn::binder::ScopeType::CASE);
            bindBlock(*caseStmt->body);
        }
    }
}