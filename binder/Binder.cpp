//
// Created by Claude on 04/01/2026.
//

#include "Binder.h"

#include "../utils/Logger.h"

Binder::Binder(DiagnosticEngine& diagnostics)
    : _diagnostics(diagnostics)
{
    _global_scope = std::make_shared<ScopedSymbolTable>();
    _current_scope = _global_scope;
}

BindingResult Binder::bind(const Program& program)
{
    BindingResult result;
    result.globalScope = _global_scope;

    collectDeclarations(program);
    processImports(program);

    // Collect constexpr declarations
    const std::string prefix = program.getNamespacePrefix();
    for (const auto& ce : program.constExprs)
    {
        const std::string qualifiedName = prefix + ce->name.token_name;
        _global_scope->constExprConstants[qualifiedName] = {ce->type, ce->isIntrinsic ? nullptr : ce->value.get()};
        auto sym = std::make_shared<Symbol>(SymbolKind::Variable, qualifiedName, ce->type, ce->name.location);
        _global_scope->define(sym);
        if (!prefix.empty())
        {
            auto shortSym = std::make_shared<Symbol>(SymbolKind::Variable, ce->name.token_name, ce->type,
                                                     ce->name.location);
            _global_scope->define(shortSym);
            _global_scope->constExprConstants[ce->name.token_name] = {
                ce->type, ce->isIntrinsic ? nullptr : ce->value.get()
            };
        }
    }

    bindProgram(program);

    result.success = !_diagnostics.hasErrors();
    return result;
}

BindingResult Binder::bindAll(const std::vector<std::shared_ptr<Program>>& programs)
{
    BindingResult result;
    result.globalScope = _global_scope;

    LOG_DEBUG("starting binding programs");

    // Create aliases for any pre-existing namespaced symbols (snapshot to avoid UB)
    {
        const std::vector<std::pair<std::string, std::shared_ptr<Symbol>>> existing(
            _global_scope->symbols().begin(), _global_scope->symbols().end());
        for (const auto& [name, symbol] : existing)
        {
            if (const auto pos = name.rfind("::"); pos != std::string::npos)
            {
                const std::string shortName = name.substr(pos + 2);
                if (!_global_scope->isDefinedLocally(shortName))
                {
                    LOG_TRACE("defining global alias %s to %s namespace", shortName.c_str(), symbol->name.c_str());
                    _global_scope->defineAlias(shortName, symbol);
                }
            }
        }
    }

    // First pass: collect all interfaces so structs can reference them
    for (const auto& program : programs)
    {
        const std::string prefix = program->fileNamespace;
        for (const auto& iface : program->interfaces)
        {
            collectInterfaceWithPrefix(*iface, prefix);
        }
    }

    // Create short-name aliases for namespaced interfaces (e.g., "Hashable" -> "std::types::constraints::Hashable")
    {
        std::vector<std::pair<std::string, std::shared_ptr<InterfaceSymbol>>> aliases;
        for (const auto& [key, symbol] : _global_scope->symbols())
        {
            if (!key.starts_with("interface:")) continue;
            const auto& name = symbol->name;
            if (const auto pos = name.rfind("::"); pos != std::string::npos)
            {
                aliases.emplace_back(name.substr(pos + 2), std::dynamic_pointer_cast<InterfaceSymbol>(symbol));
            }
        }
        for (auto& [shortName, ifaceSym] : aliases)
        {
            _global_scope->defineInterfaceAlias(shortName, std::move(ifaceSym));
        }
    }

    // Prelude pass: collect std::types declarations first and create aliases immediately
    // This ensures prelude types (str, arr, string, bool, size, etc.) are available
    // for all subsequent binding phases.
    for (const auto& program : programs)
    {
        if (program->fileNamespace == "std::types")
        {
            LOG_DEBUG("[binder] PRELUDE: collecting declarations for '%s' (structs=%zu, enums=%zu)",
                      program->name.c_str(), program->structs.size(), program->enums.size());
            collectDeclarations(*program);

            // Create short-name aliases for prelude types immediately
            const std::vector<std::pair<std::string, std::shared_ptr<Symbol>>> snapshot(
                _global_scope->symbols().begin(), _global_scope->symbols().end());
            for (const auto& [name, symbol] : snapshot)
            {
                if (!name.starts_with("std::types::")) continue;
                const auto shortName = name.substr(std::string("std::types::").length());
                if (shortName.find("::") != std::string::npos) continue;
                if (!_global_scope->isDefinedLocally(shortName))
                {
                    LOG_DEBUG("[binder]   prelude alias: '%s' -> '%s'", shortName.c_str(), name.c_str());
                    _global_scope->defineAlias(shortName, symbol);
                }
            }
            break;
        }
    }

    // Collect remaining declarations (skip std::types, already done)
    for (const auto& program : programs)
    {
        if (program->fileNamespace == "std::types") continue;
        LOG_DEBUG("[binder] collecting declarations: '%s' (ns='%s', structs=%zu, enums=%zu, functions=%zu)",
                  program->name.c_str(), program->fileNamespace.c_str(),
                  program->structs.size(), program->enums.size(), program->functions.size());
        collectDeclarations(*program);
    }

    LOG_DEBUG("[binder] total symbols after collection: %zu", _global_scope->symbols().size());

    for (const auto& program : programs)
    {
        LOG_TRACE("processing imports of program %s", program->name.c_str());
        processImports(*program);
        LOG_TRACE("end of processing imports");
    }

    // Ensure ALL namespaced symbols have short-name aliases (covers non-prelude std types too)
    {
        const std::vector<std::pair<std::string, std::shared_ptr<Symbol>>> snapshot(
            _global_scope->symbols().begin(), _global_scope->symbols().end());
        for (const auto& [name, symbol] : snapshot)
        {
            if (const auto pos = name.rfind("::"); pos != std::string::npos)
            {
                const std::string shortName = name.substr(pos + 2);
                if (!_global_scope->isDefinedLocally(shortName))
                {
                    LOG_DEBUG("[binder]   alias: '%s' -> '%s'", shortName.c_str(), name.c_str());
                    _global_scope->defineAlias(shortName, symbol);
                }
            }
        }
    }

    // Debug: verify intrinsic struct aliases
    for (const std::string& check : {
             "Target", "Platform", "Arch", "Build", "Runtime",
             "std::sys::Target", "std::sys::Platform"
         })
    {
        auto sym = _global_scope->lookup(check);
        LOG_DEBUG("[binder] intrinsic check: '%s' = %s (isStruct=%s)",
                  check.c_str(), sym ? "FOUND" : "NOT FOUND",
                  (sym && sym->isStruct()) ? "true" : "false");
    }

    // Collect constexpr declarations into the global scope
    for (const auto& program : programs)
    {
        const std::string prefix = program->getNamespacePrefix();
        for (const auto& ce : program->constExprs)
        {
            const std::string qualifiedName = prefix + ce->name.token_name;
            _global_scope->constExprConstants[qualifiedName] = {ce->type, ce->isIntrinsic ? nullptr : ce->value.get()};
            auto sym = std::make_shared<Symbol>(SymbolKind::Variable, qualifiedName, ce->type, ce->name.location);
            _global_scope->define(sym);
            if (!prefix.empty())
            {
                auto shortSym = std::make_shared<Symbol>(SymbolKind::Variable, ce->name.token_name, ce->type,
                                                         ce->name.location);
                _global_scope->define(shortSym);
                _global_scope->constExprConstants[ce->name.token_name] = {
                    ce->type, ce->isIntrinsic ? nullptr : ce->value.get()
                };
            }
            LOG_DEBUG("[binder] constexpr registered: '%s'%s", qualifiedName.c_str(),
                      ce->isIntrinsic ? " [intrinsic]" : "");
        }
    }

    for (const auto& program : programs)
    {
        LOG_TRACE("binding program %s", program->name.c_str());
        _bindingStdLib = program->fileNamespace.starts_with("std::");
        bindProgram(*program);
        _bindingStdLib = false;
        LOG_TRACE("end of binding program");
    }

    LOG_INFO("bind programs returned total of %d diagnostics", _diagnostics.get_diagnostics().size());
    result.success = !_diagnostics.hasErrors();
    return result;
}

void Binder::pushScope()
{
    _current_scope = _current_scope->createChildScope();
    _ownership.pushScope();
}

void Binder::popScope()
{
    _ownership.popScope();
    if (const auto parent = _current_scope->parentScope())
    {
        _current_scope = parent;
    }
}