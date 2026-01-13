//
// Variable declaration, initialization and assignment binding
//

#include "../Binder.h"

void Binder::bindVariableDeclaration(const VariableDeclaration &decl) {
    if (!isTypeDefined(decl.type)) {
        if (decl.type.kind == TypeKind::STRUCT) {
            errorUndefinedStruct(decl.type.structName, {});
        }
    }

    if (!_current_scope->defineVariable(decl.name, decl.type, decl.isMutable)) {
        errorDuplicateDefinition(decl.name, SymbolKind::Variable, {});
    }
}

void Binder::bindVariableInit(const VariableInit &init) {
    if (!isTypeDefined(init.type)) {
        if (init.type.kind == TypeKind::STRUCT) {
            errorUndefinedStruct(init.type.structName, {});
        }
    }

    if (init.value) {
        if (const auto *braceInit = dynamic_cast<const BraceInitializer *>(init.value.get())) {
            bindBraceInitializer(*braceInit, &init.type);
        } else {
            bindExpression(*init.value);
            // Check type compatibility (signed/unsigned, float/int, etc.)
            if (init.type.kind != TypeKind::AUTO) {
                checkTypeCompatibility(init.type, *init.value, {});
            }
        }
    }

    const auto symbol = std::make_shared<Symbol>(SymbolKind::Variable, init.name, init.type, SourceLocation{},
                                                 init.isMutable);
    symbol->isInitialized = true;
    if (!_current_scope->define(symbol)) {
        errorDuplicateDefinition(init.name, SymbolKind::Variable, {});
    }
}

void Binder::bindAssignment(const Assignment &assign) {
    if (const auto symbol = _current_scope->lookupVariable(assign.name); symbol) {
        if (!symbol->isMutable) {
            errorImmutableVariable(assign.name, {});
        }

        symbol->isInitialized = true;
        _current_scope->markUsed(assign.name);

        // Check type compatibility with existing variable type
        if (assign.value) {
            bindExpression(*assign.value);
            checkTypeCompatibility(symbol->type, *assign.value, {});
        }
    } else {
        errorUndefinedVariable(assign.name, {});
        if (assign.value) {
            bindExpression(*assign.value);
        }
    }
}
