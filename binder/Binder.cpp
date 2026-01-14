//
// Created by Claude on 04/01/2026.
//

#include "Binder.h"

Binder::Binder(DiagnosticEngine &diagnostics)
    : _diagnostics(diagnostics) {
    _global_scope = std::make_shared<ScopedSymbolTable>();
    _current_scope = _global_scope;
}

BindingResult Binder::bind(const Program &program) {
    BindingResult result;
    result.globalScope = _global_scope;

    collectDeclarations(program);
    processImports(program);
    bindProgram(program);

    result.success = !_diagnostics.hasErrors();
    return result;
}

void Binder::pushScope() {
    _current_scope = _current_scope->createChildScope();
}

void Binder::popScope() {
    if (const auto parent = _current_scope->parentScope()) {
        _current_scope = parent;
    }
}
