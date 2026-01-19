//
// Variable declaration, initialization and assignment binding
//

#include "../Binder.h"

std::shared_ptr<Symbol> Binder::bindVariableDeclaration(const VariableDeclaration &decl) {
    if (!isTypeDefined(decl.type)) {
        if (decl.type.kind == TypeKind::STRUCT) {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + decl.type.structName + "'", decl,
                         decl.name.location);
        }
    }

    const auto variableSymbol = _current_scope->defineVariable(decl.name.token_name, decl.type, decl.isMutable);
    if (!variableSymbol) {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "variable '" + decl.name.token_name + "' is already defined",
                     decl, decl.name.location);
    }

    return variableSymbol;
}

std::shared_ptr<Symbol> Binder::bindVariableInit(const VariableInit &init) {
    if (!isTypeDefined(init.type)) {
        if (init.type.kind == TypeKind::STRUCT) {
            BINDER_ERROR(DiagnosticCode::UNDEFINED_STRUCT, "undefined struct '" + init.type.structName + "'", init,
                         init.name.location);
        }
    }

    if (init.value) {
        if (const auto *braceInit = dynamic_cast<const BraceInitializer *>(init.value.get())) {
            bindBraceInitializer(*braceInit, &init.type);
        } else {
            bindExpression(*init.value);
            // type compatibility (signed/unsigned, float/int, etc.)
            if (init.type.kind != TypeKind::AUTO) {
                checkTypeCompatibility(init.type, *init.value, {});
            }
        }
    }

    const auto symbol = std::make_shared<Symbol>(SymbolKind::Variable, init.name.token_name, init.type,
                                                 SourceLocation{},
                                                 init.isMutable);
    symbol->isInitialized = true;
    if (!_current_scope->define(symbol)) {
        BINDER_ERROR(DiagnosticCode::DUPLICATE_DEFINITION, "variable '" + init.name.token_name + "' is already defined",
                     init, init.name.location);
    }

    return symbol;
}

std::shared_ptr<Symbol> Binder::bindAssignment(const Assignment &assign) {
    const auto variableSymbol = _current_scope->lookupVariable(assign.name.token_name);
    if (variableSymbol) {
        if (!variableSymbol->isMutable) {
            BINDER_ERROR(DiagnosticCode::IMMUTABLE_MODIFICATION,
                         "tried to modify an immutable variable '" + assign.name.token_name + "'", symbol,
                         assign.name.location);
        }

        variableSymbol->isInitialized = true;
        _current_scope->markUsed(assign.name.token_name);

        if (assign.value) {
            bindExpression(*assign.value);
            checkTypeCompatibility(variableSymbol->type, *assign.value, {});
        }
    } else {
        BINDER_ERROR(DiagnosticCode::UNDEFINED_VARIABLE, "undefined variable '" + assign.name.token_name + "'", assign,
                     assign.name.location);
        if (assign.value) {
            bindExpression(*assign.value);
        }
    }

    return variableSymbol;
}