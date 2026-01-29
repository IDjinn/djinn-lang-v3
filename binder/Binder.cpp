//
// Created by Claude on 04/01/2026.
//

#include "Binder.h"

#include "../utils/Logger.h"

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

BindingResult Binder::bindAll(const std::vector<std::shared_ptr<Program> > &programs) {
    BindingResult result;
    result.globalScope = _global_scope;

    LOG_DEBUG("starting binding programs");

    for (const auto &[name, symbol]: _global_scope->symbols()) {
        if (const auto pos = name.rfind("::"); pos != std::string::npos) {
            const std::string shortName = name.substr(pos + 2);
            if (!_global_scope->isDefinedLocally(shortName)) {
                _global_scope->defineAlias(shortName, symbol);
            }
        }
    }

    for (const auto &program: programs) {
        collectDeclarations(*program);
    }

    for (const auto &program: programs) {
        processImports(*program);
    }

    for (const auto &program: programs) {
        bindProgram(*program);
    }

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