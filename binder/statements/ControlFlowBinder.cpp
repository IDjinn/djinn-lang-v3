//
// Control flow statements: if, for, while, do-while, switch
//

#include "../Binder.h"
#include "../scope/ScopeGuard.h"
#include "../../utils/Logger.h"

using djinn::binder::ScopeGuard;
using djinn::binder::ControlFlowContext;

bool Binder::isCompileTimeExpression(const Expression& expr) const
{
    if (const auto* id = dynamic_cast<const Identifier*>(&expr))
        return _global_scope->constExprConstants.contains(id->name());

    if (dynamic_cast<const IntegerLiteral*>(&expr) || dynamic_cast<const BooleanLiteral*>(&expr))
        return true;

    if (const auto* fa = dynamic_cast<const FieldAccess*>(&expr))
    {
        if (const auto* obj = dynamic_cast<const Identifier*>(fa->object.get()))
        {
            LOG_DEBUG("[binder] isCompileTimeExpression: FieldAccess '%s.%s'",
                      obj->name().c_str(), fa->fieldName.token_name.c_str());
            auto structSym = _global_scope->lookupStruct(obj->name());
            if (structSym)
            {
                auto field = structSym->findField(fa->fieldName.token_name);
                LOG_DEBUG("[binder]   struct '%s' found, field found=%s, isConstant=%s",
                          obj->name().c_str(),
                          field.has_value() ? "true" : "false",
                          (field.has_value() && field->isConstant) ? "true" : "false");
                return field.has_value() && field->isConstant;
            }
            else
            {
                LOG_DEBUG("[binder]   struct '%s' NOT found in global scope", obj->name().c_str());
            }
        }
    }

    if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expr))
        return isCompileTimeExpression(*unary->operand);

    if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expr))
        return isCompileTimeExpression(*binary->left) && isCompileTimeExpression(*binary->right);

    return false;
}

bool Binder::hasConstExprIdentifier(const Expression& expr) const
{
    if (const auto* id = dynamic_cast<const Identifier*>(&expr))
        return _global_scope->constExprConstants.contains(id->name());

    if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expr))
        return hasConstExprIdentifier(*unary->operand);

    if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expr))
        return hasConstExprIdentifier(*binary->left) || hasConstExprIdentifier(*binary->right);

    return false;
}

void Binder::bindIfStatement(const IfStatement& stmt)
{
    LOG_DEBUG("[binder] bindIfStatement: compileTimeKind=%d", static_cast<int>(stmt.compileTimeKind));
    LOG_DEBUG("[binder]   condition type: %s", typeid(*stmt.condition).name());

    bindExpression(*stmt.condition);

    const bool isCompileTimeCompatible = isCompileTimeExpression(*stmt.condition);
    LOG_DEBUG("[binder]   isCompileTimeExpression=%s", isCompileTimeCompatible ? "true" : "false");

    if (stmt.compileTimeKind == CompileTimeKind::ConstEval || stmt.compileTimeKind == CompileTimeKind::ConstExpr)
    {
        if (!isCompileTimeCompatible)
        {
            BINDER_ERROR(DiagnosticCode::NOT_COMPILE_TIME_EVALUABLE,
                         "compile time if condition require const/constexpr/consteval expressions!",
                         stmt, stmt.location);
        }
    }
    else if (isCompileTimeCompatible)
    {
        BINDER_WARNING(DiagnosticCode::CONSTEXPR_RUNTIME_IF,
                       "compile-time expression used in runtime if — remove '()' for compile-time evaluation",
                       stmt.location);
    }

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