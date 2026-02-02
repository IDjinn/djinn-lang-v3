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
                LOG_TRACE("defining global alias %s to %s namespace", shortName.c_str(), symbol->name.c_str());
                _global_scope->defineAlias(shortName, symbol);
            }
        }
    }

    for (const auto &program: programs) {
        LOG_TRACE("collecting declarations of program %s", program->name.c_str());
        collectDeclarations(*program);
        LOG_TRACE("end of collecting declarations");
    }

    for (const auto &program: programs) {
        LOG_TRACE("processing imports of program %s", program->name.c_str());
        processImports(*program);
        LOG_TRACE("end of processing imports");
    }

    for (const auto &program: programs) {
        LOG_TRACE("binding program %s", program->name.c_str());
        bindProgram(*program);
        LOG_TRACE("end of binding program");
    }

    LOG_INFO("bind programs returned total of %d diagnostics", _diagnostics.get_diagnostics().size());
    result.success = !_diagnostics.hasErrors();
    return result;
}

void Binder::pushScope() {
    _current_scope = _current_scope->createChildScope();
    _ownership.pushScope();
}

void Binder::popScope() {
    _ownership.popScope();
    if (const auto parent = _current_scope->parentScope()) {
        _current_scope = parent;
    }
}
