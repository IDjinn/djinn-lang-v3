//
// Variable declaration, initialization and assignment binding
//

#include "../Binder.h"

std::shared_ptr<Symbol> Binder::bindVariableDeclaration(const VariableDeclaration& decl)
{
    if (!isTypeDefined(decl.type))
    {
        if (decl.type.kind == TypeKind::STRUCT)
        {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + decl.type.structName + "'", decl,
                         decl.name.location);
        }
    }

    const auto variableSymbol = _current_scope->defineVariable(decl.name.token_name, decl.type, decl.isMutable);
    if (!variableSymbol)
    {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "variable '" + decl.name.token_name + "' is already defined",
                     decl, decl.name.location);
    }

    // Track ownership for the new variable
    trackVariableDefinition(decl.name.token_name, decl.type, decl.name.location);

    return variableSymbol;
}

std::shared_ptr<Symbol> Binder::bindVariableInit(const VariableInit& init)
{
    if (!isTypeDefined(init.type))
    {
        if (init.type.kind == TypeKind::STRUCT)
        {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + init.type.structName + "'", init,
                         init.name.location);
        }
    }

    if (init.value)
    {
        if (const auto* braceInit = dynamic_cast<const BraceInitializer*>(init.value.get()))
        {
            bindBraceInitializer(*braceInit, &init.type);
        }
        else
        {
            // Bind expression first (validates the value exists and is usable)
            bindExpression(*init.value);

            // If initializing from another variable, check and perform move AFTER binding
            if (const auto* idExpr = dynamic_cast<const Identifier*>(init.value.get()))
            {
                checkVariableMove(idExpr->identifier.token_name, idExpr->identifier.location);
                performMove(idExpr->identifier.token_name, idExpr->identifier.location);
            }

            // Type compatibility check
            if (init.type.kind != TypeKind::AUTO)
            {
                checkTypeCompatibility(init.type, *init.value, init.value->location);
            }
        }
    }

    Type resolvedType = init.type;
    if (resolvedType.kind == TypeKind::AUTO && init.value)
    {
        if (auto inferred = inferExpressionType(*init.value))
        {
            resolvedType = *inferred;
        }
    }

    const auto symbol = std::make_shared<Symbol>(SymbolKind::Variable, init.name.token_name, resolvedType,
                                                 SourceLocation{},
                                                 init.isMutable);
    symbol->isInitialized = true;
    if (!_current_scope->define(symbol))
    {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "variable '" + init.name.token_name + "' is already defined",
                     init, init.name.location);
    }

    // Track ownership for the new variable
    trackVariableDefinition(init.name.token_name, resolvedType, init.name.location);

    return symbol;
}

std::shared_ptr<Symbol> Binder::bindAssignment(const Assignment& assign)
{
    const auto variableSymbol = _current_scope->lookupVariable(assign.name.token_name);
    if (variableSymbol)
    {
        if (!variableSymbol->isMutable)
        {
            BINDER_ERROR(DiagnosticCode::IMMUTABLE_MODIFICATION,
                         "tried to modify an immutable variable '" + assign.name.token_name + "'", symbol,
                         assign.name.location);
        }

        // Check ownership: cannot assign while borrowed
        checkVariableAssignment(assign.name.token_name, assign.name.location);

        variableSymbol->isInitialized = true;
        _current_scope->markUsed(assign.name.token_name);

        if (assign.value)
        {
            // Check if the value is a move from another variable
            if (const auto* idExpr = dynamic_cast<const Identifier*>(assign.value.get()))
            {
                checkVariableMove(idExpr->identifier.token_name, idExpr->identifier.location);
                performMove(idExpr->identifier.token_name, idExpr->identifier.location);
            }
            bindExpression(*assign.value);
            checkTypeCompatibility(variableSymbol->type, *assign.value, assign.value->location);
        }

        // Re-initialize ownership if the variable was previously moved
        reinitializeVariable(assign.name.token_name);
    }
    else
    {
        BINDER_ERROR(DiagnosticCode::UNDEFINED_VARIABLE, "undefined variable '" + assign.name.token_name + "'", assign,
                     assign.name.location);
        if (assign.value)
        {
            bindExpression(*assign.value);
        }
    }

    return variableSymbol;
}