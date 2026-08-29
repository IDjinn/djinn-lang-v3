//
// Created by Claude on 04/01/2026.
//

#include "Binder.h"

#include "../utils/Logger.h"
#include "../lib/DjLibReader.h"
#include "ErrorTypes.h"

Binder::Binder(DiagnosticEngine& diagnostics, ErrorEnforcement enforcement)
    : _diagnostics(diagnostics), enforcement_(enforcement)
{
    _global_scope = std::make_shared<ScopedSymbolTable>();
    _current_scope = _global_scope;

    // Seed builtin error types — always available, no std import required
    for (const auto& err : djinn::errors::builtin_errors())
    {
        auto sym = std::make_shared<StructSymbol>(err.name);
        sym->isErrorType = true;
        sym->errorTag = err.tag;
        sym->errorBase = err.base ? err.base : "";
        _global_scope->defineStruct(sym);
    }
}

void Binder::injectLibrarySymbols(const djlib::DjLibReader& reader)
{
    LOG_DEBUG("[binder] injecting library symbols from djlib");
    reader.populateSymbolTable(*_global_scope);
    reader.populateAttributeTargets(_attributeTargets);
    reader.populateConstExprs(*_global_scope);
    reader.populateStaticVars(*_global_scope);
}

BindingResult Binder::bind(const Program& program)
{
    BindingResult result;
    result.globalScope = _global_scope;

    collectDeclarations(program);
    processImports(program);

    // Collect attribute declarations — register targets and structs
    {
        const std::string attrPrefix = program.getNamespacePrefix();
        for (const auto& attrDecl : program.attributeDecls)
        {
            const std::string qualifiedName = attrPrefix + attrDecl->name.token_name;
            if (!attrDecl->fields.empty())
            {
                auto structSym = std::make_shared<StructSymbol>(qualifiedName);
                for (const auto& field : attrDecl->fields)
                {
                    structSym->addField(field.name.token_name, *field.type, false);
                }
                _global_scope->defineStruct(structSym);
            }

            int32_t targetMask = 0;
            for (const auto& arg : attrDecl->targetArgs)
            {
                if (auto* str = std::get_if<std::string>(&arg.value))
                    targetMask |= resolveAttributeTargetString(*str);
            }
            if (targetMask == 0) targetMask = TargetAll;
            _attributeTargets[attrDecl->name.token_name] = targetMask;
        }
    }

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

    // Collect attribute declarations — register targets and structs for typed attributes
    for (const auto& program : programs)
    {
        const std::string prefix = program->getNamespacePrefix();
        for (const auto& attrDecl : program->attributeDecls)
        {
            const std::string qualifiedName = prefix + attrDecl->name.token_name;
            if (!attrDecl->fields.empty())
            {
                auto structSym = std::make_shared<StructSymbol>(qualifiedName);
                for (const auto& field : attrDecl->fields)
                {
                    structSym->addField(field.name.token_name, *field.type, false);
                }
                _global_scope->defineStruct(structSym);
            }

            int32_t targetMask = 0;
            for (const auto& arg : attrDecl->targetArgs)
            {
                if (auto* str = std::get_if<std::string>(&arg.value))
                    targetMask |= resolveAttributeTargetString(*str);
            }
            if (targetMask == 0) targetMask = TargetAll;
            _attributeTargets[attrDecl->name.token_name] = targetMask;
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
        const std::string prefix = program->getNamespacePrefix();
        for (const auto& sv : program->staticVars)
        {
            const std::string qualifiedName = prefix + sv->name.token_name;
            auto sym = std::make_shared<Symbol>(SymbolKind::Variable, qualifiedName, sv->type, sv->name.location);
            sym->isMutable = sv->isMutable;
            _global_scope->define(sym);
            _global_scope->staticVars[qualifiedName] = {sv->type, sv->initializer.get(), sv->isMutable};
            if (!prefix.empty())
            {
                auto shortSym = std::make_shared<Symbol>(SymbolKind::Variable, sv->name.token_name, sv->type,
                                                         sv->name.location);
                shortSym->isMutable = sv->isMutable;
                _global_scope->define(shortSym);
                _global_scope->staticVars[sv->name.token_name] = {sv->type, sv->initializer.get(), sv->isMutable};
            }
            LOG_DEBUG("[binder] static var registered: '%s' (mut=%d)", qualifiedName.c_str(), sv->isMutable);
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

bool Binder::is_error_derived_from(const std::string& structName, const std::string& baseName) const
{
    const auto base = _global_scope->lookupStruct(baseName);
    if (!base || !base->isErrorType) return false;

    auto current = _global_scope->lookupStruct(structName);
    while (current)
    {
        if (current == base) return true;
        if (!current->isErrorType || current->errorBase.empty()) return false;
        current = _global_scope->lookupStruct(current->errorBase);
    }
    return false;
}

void Binder::check_throwing_call(const std::string& calleeName, const bool calleeThrows, const SourceLocation& loc)
{
    if (enforcement_ == ErrorEnforcement::Off) return;
    if (!calleeThrows) return;

    if (insideTryExpression_)
    {
        tryOperandSawThrowingCall_ = true;
        return;
    }

    // Unchecked call inside another throwing function: error propagates.
    // Strict mode rejects it — propagate explicitly with a bare `try`.
    if (currentFunctionThrows_&& enforcement_ != ErrorEnforcement::Strict) return;

    const bool strict = enforcement_ == ErrorEnforcement::Strict;
    _diagnostics.emitAndPrint(Diagnostic(
        Severity::Error, DiagnosticCode::MISSING_TRY,
        "call to throwing function '" + calleeName + "' must be wrapped in 'try'" +
        (strict ? " (strict error enforcement: use a bare 'try' to propagate)" : ""),
        loc
    ));
}

ConstEvaluator& Binder::getCompileTimeEvaluator() const
{
    if (compileTimeEvaluatorReady_) return *compileTimeEvaluator_;
    compileTimeEvaluatorReady_ = true;

    auto eval = std::make_unique<ConstEvaluator>();

    for (const auto& [name, entry] : _global_scope->constExprConstants)
    {
        if (!entry.value) continue; // intrinsics carry no expression
        const ConstValue val = eval->evaluate(*entry.value);
        if (!val.isError() && !val.isThrown())
            eval->defineConstant(name, val);
    }

    // Registered under the symbol name (qualified) — aliases in the symbol
    // table point to the same FunctionSymbol, so each body registers once per
    // distinct name, which is harmless.
    for (const auto& [name, symbol] : _global_scope->symbols())
    {
        const auto funcSym = std::dynamic_pointer_cast<FunctionSymbol>(symbol);
        if (!funcSym || !funcSym->hasBody()) continue;
        if (!(funcSym->constExpr || funcSym->constEval)) continue;
        if (funcSym->isAsync) continue;
        eval->defineFunction(funcSym->name, funcSym->paramNames, *funcSym->body);
    }

    compileTimeEvaluator_ = std::move(eval);
    return *compileTimeEvaluator_;
}

bool Binder::check_compile_time_call(const FunctionSymbol& funcSym, const FunctionCall& call)
{
    if (_bindingStdLib) return false;
    if (enforcement_ != ErrorEnforcement::CompileTime && enforcement_ != ErrorEnforcement::Strict) return false;
    if (!funcSym.hasBody()) return false;
    if (call.arguments.size() != funcSym.paramNames.size()) return false;

    auto& eval = getCompileTimeEvaluator();

    const bool canAnalyzeThrow = (funcSym.constExpr || funcSym.constEval) && funcSym.isThrowing()
        && eval.hasFunction(funcSym.name);

    bool hasRequires = false;
    for (const auto* contract : funcSym.contracts)
    {
        if (contract && contract->isRequire())
        {
            hasRequires = true;
            break;
        }
    }
    if (!canAnalyzeThrow && !hasRequires) return false;

    // All arguments must be compile-time evaluable; otherwise the runtime
    // checks still enforce everything
    std::vector<ConstValue> args;
    args.reserve(call.arguments.size());
    for (const auto& arg : call.arguments)
    {
        ConstValue val = eval.evaluate(*arg);
        if (val.isError() || val.isThrown()) return false;
        args.push_back(val);
    }

    if (canAnalyzeThrow)
    {
        const ConstValue result = eval.evaluateFunction(funcSym.name, args);
        if (result.isThrown())
        {
            const std::string thrownType = result.errorName.empty() ? "error" : result.errorName;
            if (insideTryExpression_)
            {
                _diagnostics.emitAndPrint(Diagnostic(
                    Severity::Warning, DiagnosticCode::ALWAYS_THROWS_HANDLED,
                    "call to '" + call.name.token_name + "' always throws '" + thrownType +
                    "' (evaluated at compile time); the error path is always taken",
                    call.name.location));
            }
            else
            {
                _diagnostics.emitAndPrint(Diagnostic(
                    Severity::Error, DiagnosticCode::CONSTEXPR_CALL_THROWS,
                    "call to '" + call.name.token_name + "' always throws '" + thrownType +
                    "' (evaluated at compile time)",
                    call.name.location));
                return true;
            }
            return false;
        }
    }

    if (hasRequires)
    {
        // Bind parameter names to the constant arguments, evaluate each
        // require clause, then restore the evaluator state
        std::vector<std::optional<ConstValue>> previous;
        previous.reserve(funcSym.paramNames.size());
        for (size_t i = 0; i < funcSym.paramNames.size(); i++)
        {
            previous.push_back(eval.lookupConstant(funcSym.paramNames[i]));
            eval.defineConstant(funcSym.paramNames[i], args[i]);
        }
        const auto restoreConstants = [&]
        {
            for (size_t i = 0; i < funcSym.paramNames.size(); i++)
            {
                if (previous[i]) eval.defineConstant(funcSym.paramNames[i], *previous[i]);
                else eval.removeConstant(funcSym.paramNames[i]);
            }
        };

        for (const auto* contract : funcSym.contracts)
        {
            if (!contract || !contract->isRequire()) continue;

            const Expression* condition = contract->condition.get();
            if (!condition && contract->block && contract->block->statements.size() == 1)
            {
                if (const auto* ret = dynamic_cast<const ReturnStatement*>(contract->block->statements[0].get()))
                    condition = ret->value.get();
            }
            if (!condition) continue;

            const ConstValue val = eval.evaluate(*condition);
            if (val.kind == ConstValue::Bool && !val.boolVal)
            {
                restoreConstants();
                if (insideTryExpression_)
                {
                    _diagnostics.emitAndPrint(Diagnostic(
                        Severity::Warning, DiagnosticCode::ALWAYS_THROWS_HANDLED,
                        "call to '" + call.name.token_name +
                        "' always violates a 'require' clause (evaluated at compile time); the error path is always taken",
                        call.name.location));
                }
                else
                {
                    _diagnostics.emitAndPrint(Diagnostic(
                        Severity::Error, DiagnosticCode::CONTRACT_VIOLATION_COMPILE_TIME,
                        "call to '" + call.name.token_name +
                        "' violates a 'require' clause (evaluated at compile time)",
                        call.name.location));
                    return true;
                }
                return false;
            }
        }
        restoreConstants();
    }

    return false;
}

void Binder::popScope()
{
    _ownership.popScope();
    if (const auto parent = _current_scope->parentScope())
    {
        _current_scope = parent;
    }
}

int32_t Binder::resolveAttributeTargetString(const std::string& targetStr)
{
    static const std::unordered_map<std::string, int32_t> targetMap = {
        {"AttributeTarget.Function", TargetFunction},
        {"AttributeTarget.Method", TargetMethod},
        {"AttributeTarget.Struct", TargetStruct},
        {"AttributeTarget.Field", TargetField},
        {"AttributeTarget.Parameter", TargetParameter},
        {"AttributeTarget.ReturnValue", TargetReturnValue},
        {"AttributeTarget.Variable", TargetVariable},
        {"AttributeTarget.All", TargetAll},
    };

    int32_t result = 0;
    std::string remaining = targetStr;

    while (!remaining.empty())
    {
        // Trim whitespace
        size_t start = remaining.find_first_not_of(" ");
        if (start == std::string::npos) break;
        remaining = remaining.substr(start);

        // Skip '|' separator
        if (remaining[0] == '|')
        {
            remaining = remaining.substr(1);
            continue;
        }

        // Find next token boundary (space or |)
        size_t end = remaining.find_first_of(" |");
        std::string token = remaining.substr(0, end);
        remaining = end == std::string::npos ? "" : remaining.substr(end);

        auto it = targetMap.find(token);
        if (it != targetMap.end())
            result |= it->second;
    }

    return result;
}

void Binder::validateAttributeTarget(const std::string& attrName, int32_t context, const SourceLocation& loc) const
{
    auto it = _attributeTargets.find(attrName);
    if (it == _attributeTargets.end()) return;

    if ((it->second & context) == 0)
    {
        static const std::unordered_map<int32_t, std::string> contextNames = {
            {TargetFunction, "function"},
            {TargetMethod, "method"},
            {TargetStruct, "struct"},
            {TargetField, "field"},
            {TargetParameter, "parameter"},
            {TargetReturnValue, "return value"},
            {TargetVariable, "variable"},
        };
        std::string contextName = "unknown";
        auto nameIt = contextNames.find(context);
        if (nameIt != contextNames.end()) contextName = nameIt->second;

        BINDER_WARNING(DiagnosticCode::INVALID_ATTRIBUTE_TARGET,
                       "attribute '" + attrName + "' is not valid on " + contextName,
                       loc);
    }
}