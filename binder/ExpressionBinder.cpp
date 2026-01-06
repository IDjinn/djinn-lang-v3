//
// Created by Claude on 05/01/2026.
//

#include "Binder.h"

void Binder::bindExpression(const Expression &expr) {
    if (const auto *id = dynamic_cast<const Identifier *>(&expr)) {
        bindIdentifier(*id);
    } else if (const auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        bindFunctionCall(*call);
    } else if (const auto *access = dynamic_cast<const FieldAccess *>(&expr)) {
        bindFieldAccess(*access);
    } else if (const auto *fieldAssign = dynamic_cast<const FieldAssignment *>(&expr)) {
        bindFieldAssignment(*fieldAssign);
    } else if (const auto *binary = dynamic_cast<const BinaryExpression *>(&expr)) {
        bindBinaryExpression(*binary);
    } else if (const auto *unary = dynamic_cast<const UnaryExpression *>(&expr)) {
        bindUnaryExpression(*unary);
    } else if (const auto *varDecl = dynamic_cast<const VariableDeclaration *>(&expr)) {
        bindVariableDeclaration(*varDecl);
    } else if (const auto *varInit = dynamic_cast<const VariableInit *>(&expr)) {
        bindVariableInit(*varInit);
    } else if (const auto *assign = dynamic_cast<const Assignment *>(&expr)) {
        bindAssignment(*assign);
    } else if (const auto *braceInit = dynamic_cast<const BraceInitializer *>(&expr)) {
        bindBraceInitializer(*braceInit);
    }
}

void Binder::bindIdentifier(const Identifier &id) const {
    if (const auto sym = _current_scope->lookupVariable(id.name); sym) {
        _current_scope->markUsed(id.name);
        return;
    }

    if (const auto funcSym = _current_scope->lookupFunction(id.name); !funcSym) {
        errorUndefinedVariable(id.name, {});
    }
}

void Binder::bindFunctionCall(const FunctionCall &call) {
    if (const auto funcSym = _global_scope->lookupFunction(call.name); funcSym) {
        if (!funcSym->isVariadic && call.arguments.size() != funcSym->arity()) {
            errorWrongArgumentCount(call.name, funcSym->arity(), call.arguments.size(), {});
        } else if (funcSym->isVariadic && call.arguments.size() < funcSym->arity()) {
            errorWrongArgumentCount(call.name, funcSym->arity(), call.arguments.size(), {});
        }
    } else {
        errorUndefinedFunction(call.name, {});
    }

    for (const auto &arg: call.arguments) {
        bindExpression(*arg);
    }
}

void Binder::bindFieldAccess(const FieldAccess &access) {
    bindExpression(*access.object);

    // TODO: Type checking would validate the field exists on the struct type
    // For now, we just bind the expression - field validation happens in type checking
}

void Binder::bindFieldAssignment(const FieldAssignment &assign) {
    bindExpression(*assign.object);

    if (assign.value) {
        bindExpression(*assign.value);
    }

    // TODO: Type checking would validate the field exists and is mutable
}

void Binder::bindBinaryExpression(const BinaryExpression &expr) {
    bindExpression(*expr.left);
    bindExpression(*expr.right);
}

void Binder::bindUnaryExpression(const UnaryExpression &expr) {
    bindExpression(*expr.operand);
}

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
    } else {
        errorUndefinedVariable(assign.name, {});
    }

    if (assign.value) {
        bindExpression(*assign.value);
    }
}

void Binder::bindBraceInitializer(const BraceInitializer &init, const Type *expectedType) {
    if (expectedType && expectedType->kind == TypeKind::STRUCT) {
        if (const auto structSym = _global_scope->lookupStruct(expectedType->structName)) {
            for (const auto &elem: init.elements) {
                if (elem.isDesignated()) {
                    if (!structSym->hasField(elem.fieldName)) {
                        errorUndefinedField(expectedType->structName, elem.fieldName, {});
                    }
                }
                if (elem.value) {
                    bindExpression(*elem.value);
                }
            }
            return;
        }
    }

    for (const auto &elem: init.elements) {
        if (elem.value) {
            bindExpression(*elem.value);
        }
    }
}