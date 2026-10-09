#include "Verifier.h"

#include <algorithm>
#include <functional>
#include <ranges>
#include <set>

#include "../binder/Symbol.h"
#include "../binder/SymbolTable.h"
#include "../parser/ast/Declaration.h"
#include "../parser/ast/Expression.h"
#include "../parser/ast/Statement.h"
#include "../utils/Logger.h"

namespace
{
    using namespace djinn::verification;

    constexpr size_t NO_ROW = static_cast<size_t>(-1);

    const char* mode_name(ObligationMode mode)
    {
        switch (mode)
        {
            case ObligationMode::Check: return "check";
            case ObligationMode::Prove: return "prove";
            case ObligationMode::Assume: return "assume";
            case ObligationMode::Ignore: return "ignore";
        }
        return "?";
    }

    const char* status_name(ObligationStatus status)
    {
        switch (status)
        {
            case ObligationStatus::Unverified: return "unverified";
            case ObligationStatus::Proven: return "proven";
            case ObligationStatus::Checked: return "checked";
            case ObligationStatus::Assumed: return "assumed";
        }
        return "?";
    }

    ObligationMode obligation_mode(const ContractClause::Mode mode)
    {
        switch (mode)
        {
            case ContractClause::Mode::Check: return ObligationMode::Check;
            case ContractClause::Mode::Prove: return ObligationMode::Prove;
            case ContractClause::Mode::Assume: return ObligationMode::Assume;
            case ContractClause::Mode::Ignore: break;
        }
        return ObligationMode::Ignore;
    }

    // Obligation rows for one subject's contracts, positionally aligned with
    // the clause vector (NO_ROW for skipped modes — `ignore` is excluded
    // from the report entirely, spec §5.2).
    std::vector<size_t> collect_obligations(VerificationIR& ir, const bool trace,
                                            const std::string& subject,
                                            const std::vector<const ContractClause*>& contracts,
                                            const std::vector<Type>& paramTypes,
                                            const std::vector<std::string>& paramNames)
    {
        std::vector<size_t> rows;

        for (const auto* clause : contracts)
        {
            if (!clause || clause->mode == ContractClause::Mode::Ignore)
            {
                rows.push_back(NO_ROW);
                continue;
            }

            Obligation obligation;
            obligation.kind = clause->isEnsure() ? ObligationKind::Ensure : ObligationKind::Require;
            obligation.mode = obligation_mode(clause->mode);
            obligation.status = clause->mode == ContractClause::Mode::Assume
                                    ? ObligationStatus::Assumed
                                    : (clause->mode == ContractClause::Mode::Prove
                                           ? ObligationStatus::Unverified
                                           : ObligationStatus::Checked);
            obligation.subject = subject;
            obligation.description = clause->isEnsure() ? "ensure" : "require";
            if (clause->mode == ContractClause::Mode::Assume)
                obligation.justification = "trusted (mode assume)";
            if (trace)
            {
                LOG_INFO("[verification] %s @ %s (mode=%s, status=%s)", obligation.description.c_str(),
                         subject.c_str(), mode_name(obligation.mode), status_name(obligation.status));
            }
            rows.push_back(ir.obligations.size());
            ir.obligations.push_back(std::move(obligation));
        }

        for (size_t i = 0; i < paramTypes.size(); i++)
        {
            if (!paramTypes[i].nonZero)
                continue;

            Obligation obligation;
            obligation.kind = ObligationKind::Require;
            obligation.mode = ObligationMode::Check;
            obligation.status = ObligationStatus::Checked;
            obligation.subject = subject;
            obligation.description = "require(" + paramNames[i] + " != 0)";
            if (trace)
            {
                LOG_INFO("[verification] %s @ %s (mode=%s, status=%s)", obligation.description.c_str(),
                         subject.c_str(), mode_name(obligation.mode), status_name(obligation.status));
            }
            ir.obligations.push_back(std::move(obligation));
        }

        return rows;
    }

    // Effect classification for callees the inference cannot see into
    // (spec §11 vocabulary). Everything else contributes transitively.
    const std::set<SemanticEffect>* builtin_effects(const std::string& name)
    {
        using Effect = SemanticEffect;
        static const std::set<Effect> memory = {Effect::Memory};
        static const std::set<Effect> filesystem = {Effect::Filesystem};
        static const std::set<Effect> time = {Effect::Time};
        static const std::set<Effect> random = {Effect::Random};
        static const std::set<Effect> process = {Effect::Process};
        static const std::set<Effect> external_io = {Effect::ExternalIO};

        if (name == "malloc" || name == "calloc" || name == "realloc" || name == "free") return &memory;
        if (name == "fopen" || name == "fclose" || name == "fread" || name == "fwrite" ||
            name == "remove" || name == "rename" || name == "fgets" || name == "fputs")
            return &filesystem;
        if (name == "time" || name == "clock") return &time;
        if (name == "rand" || name == "srand") return &random;
        if (name == "system") return &process;
        if (name == "printf" || name == "puts" || name == "putchar" || name == "fprintf" ||
            name == "sprintf" || name == "snprintf" || name == "fflush")
            return &external_io;
        return nullptr;
    }

    std::string join_path(const std::vector<std::string>& path)
    {
        std::string joined;
        for (size_t i = 0; i < path.size(); i++)
        {
            if (i > 0) joined += ".";
            joined += path[i];
        }
        return joined;
    }

    // Method field paths are canonically "self.field" regardless of whether
    // the source wrote `this.field` (claims may use either spelling).
    std::string normalize_path(const std::string& path)
    {
        if (path.rfind("this.", 0) == 0) return "self." + path.substr(5);
        return path;
    }

    std::string join_set(const std::set<std::string>& values)
    {
        std::string joined;
        for (const auto& value : values)
        {
            if (!joined.empty()) joined += ", ";
            joined += value;
        }
        return joined;
    }

    std::string join_set(const std::set<SemanticEffect>& values)
    {
        std::set<std::string> names;
        for (const auto& value : values)
            names.insert(semantic_effect_name(value));
        return join_set(names);
    }

    std::string evidence_constants(const FactSet& facts, const std::vector<std::string>& paramNames)
    {
        std::string evidence;
        for (const auto& name : paramNames)
        {
            if (const auto constant = facts.constant_of(name))
            {
                if (!evidence.empty()) evidence += ", ";
                evidence += name + " = " + std::to_string(*constant);
            }
        }
        return evidence;
    }

    bool summary_equal(const PostconditionSummary& left, const PostconditionSummary& right)
    {
        return left.propagated == right.propagated && left.value.to_string() == right.value.to_string();
    }

    const Symbol* resolve_callee_by_name(const std::string& name,
                                         const std::shared_ptr<ScopedSymbolTable>& scope)
    {
        if (name.empty() || !scope) return nullptr;
        if (const auto fn = scope->lookupFunction(name))
            return fn.get();

        // Namespace-scoped symbols register under their qualified name while
        // a uow body only carries the written (unbound) name — fall back to a
        // qualified-name suffix match.
        const std::string suffix = "::" + name;
        for (const auto& entry : scope->symbols() | std::views::values)
        {
            if (entry && entry->isFunction() && entry->name.length() > suffix.length() &&
                entry->name.ends_with(suffix))
                return entry.get();
        }

        const auto separator = name.rfind("::");
        if (separator == std::string::npos) return nullptr;
        const auto strct = scope->lookupStruct(name.substr(0, separator));
        if (!strct) return nullptr;
        return strct->getMethod(name.substr(separator + 2)).get();
    }

    // Free identifiers of a contract condition: the names a member function
    // must be able to bind to inherit a clause from its uow.
    void collect_identifiers(const Expression& expression, std::set<std::string>& out)
    {
        if (const auto* identifier = dynamic_cast<const Identifier*>(&expression))
        {
            out.insert(identifier->name());
            return;
        }
        if (const auto* field = dynamic_cast<const FieldAccess*>(&expression))
        {
            collect_identifiers(*field->object, out);
            return;
        }
        if (const auto* field_assignment = dynamic_cast<const FieldAssignment*>(&expression))
        {
            collect_identifiers(*field_assignment->object, out);
            collect_identifiers(*field_assignment->value, out);
            return;
        }
        if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression))
        {
            collect_identifiers(*binary->left, out);
            collect_identifiers(*binary->right, out);
            return;
        }
        if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
        {
            collect_identifiers(*unary->operand, out);
            return;
        }
        if (const auto* ternary = dynamic_cast<const TernaryExpression*>(&expression))
        {
            collect_identifiers(*ternary->condition, out);
            collect_identifiers(*ternary->trueExpr, out);
            collect_identifiers(*ternary->falseExpr, out);
            return;
        }
        if (const auto* assignment = dynamic_cast<const Assignment*>(&expression))
        {
            collect_identifiers(*assignment->value, out);
            return;
        }
        if (const auto* init = dynamic_cast<const VariableInit*>(&expression))
        {
            collect_identifiers(*init->value, out);
            return;
        }
        if (const auto* index = dynamic_cast<const IndexAccess*>(&expression))
        {
            collect_identifiers(*index->object, out);
            collect_identifiers(*index->index, out);
            return;
        }
        if (const auto* fallback = dynamic_cast<const TryExpression*>(&expression))
        {
            collect_identifiers(*fallback->expr, out);
            collect_identifiers(*fallback->fallback, out);
            return;
        }
        if (const auto* call = dynamic_cast<const FunctionCall*>(&expression))
        {
            for (const auto& argument : call->arguments)
                collect_identifiers(*argument, out);
            if (call->receiver) collect_identifiers(*call->receiver, out);
            return;
        }
    }

    // A uow clause is inheritable by a member when every identifier it names
    // is one of the member's parameters (or the receiver, for methods).
    bool clause_resolvable(const ContractClause& clause, const std::vector<std::string>& paramNames,
                           const bool isMethod)
    {
        if (!clause.condition) return false;
        std::set<std::string> names;
        collect_identifiers(*clause.condition, names);
        for (const auto& name : names)
        {
            if (std::find(paramNames.begin(), paramNames.end(), name) != paramNames.end())
                continue;
            if (isMethod && (name == "self" || name == "this")) continue;
            return false;
        }
        return true;
    }
}


Verifier::Verifier(DiagnosticEngine& diagnostics, const VerificationMode mode)
    : _diagnostics(diagnostics), _mode(mode)
{
}

VerificationResult Verifier::run(const std::vector<std::shared_ptr<Program>>& programs,
                                 const std::shared_ptr<ScopedSymbolTable>& globalScope)
{
    VerificationResult result;

    if (_mode == VerificationMode::Off || !globalScope)
        return result;

    const bool trace = _mode == VerificationMode::Trace;

    // uow registry: every declared uow by identity (nested included), so
    // `uow (Name)` attachments resolve and unattached uows can be flagged.
    std::map<const UowDeclaration*, std::vector<UowMember>> uow_members;
    std::function<void(const UowDeclaration&)> register_uow =
        [&](const UowDeclaration& declaration)
    {
        uow_members.emplace(&declaration, std::vector<UowMember>{});
        for (const auto& nested : declaration.nested)
            register_uow(*nested);
    };
    std::map<std::string, const UowDeclaration*> uows_by_name;
    for (const auto& program : programs)
        for (const auto& uow : program->uows)
        {
            register_uow(*uow);
            uows_by_name.emplace(uow->name.token_name, uow.get());
        }

    // One subject per function/method with a body the pass can analyze.
    // Imports register aliases to the same symbol; collect each symbol once.
    // Members of a uow inherit its require/ensure clauses for analysis
    // (compile-time only — the generator never sees them); clauses whose
    // operands the member cannot name are recorded as assumed obligations.
    struct Subject
    {
        const Symbol* key = nullptr;
        const FunctionSymbol* function = nullptr;
        const MethodSymbol* method = nullptr;
        std::vector<size_t> obligation_rows;
        std::vector<const ContractClause*> contracts;
        std::vector<const ContractClause*> inherited_requires;
        const UowDeclaration* uow = nullptr;
        UowPhase uow_phase = UowPhase::Body;
    };
    std::vector<Subject> subjects;
    std::set<const Symbol*> seen;
    size_t symbols = 0;

    const auto subject_name = [](const Subject& subject)
    {
        return subject.function ? subject.function->name
                                : subject.method->structName + "::" + subject.method->name;
    };

    const auto inherit_uow = [&](Subject& subject, const std::string& attachedName,
                                 const SourceLocation& attachedLocation, const UowPhase phase)
    {
        const auto it = uows_by_name.find(attachedName);
        if (it == uows_by_name.end())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_UNKNOWN_UOW,
                                  "function '" + subject_name(subject) +
                                      "' attaches to unknown uow '" + attachedName + "'",
                                  attachedLocation);
            diagnostic.withHelp("declare the uow first: 'uow " + attachedName + " { ... }'");
            _diagnostics.emitAndPrint(diagnostic);
            result.success = false;
            return;
        }
        subject.uow = it->second;
        subject.uow_phase = phase;

        const bool isMethod = subject.method != nullptr;
        const auto& params = isMethod ? subject.method->paramNames : subject.function->paramNames;
        for (const auto& clause : subject.uow->contracts)
        {
            if (!clause.condition || clause.mode == ContractClause::Mode::Ignore) continue;
            if (!clause_resolvable(clause, params, isMethod))
            {
                Obligation obligation;
                obligation.kind = clause.isEnsure() ? ObligationKind::Ensure : ObligationKind::Require;
                obligation.mode = obligation_mode(clause.mode);
                obligation.status = ObligationStatus::Assumed;
                obligation.subject = subject_name(subject);
                obligation.description = clause.isEnsure() ? "ensure" : "require";
                obligation.justification =
                    "uow '" + attachedName + "': names operands outside this member";
                if (trace)
                {
                    std::set<std::string> names;
                    if (clause.condition) collect_identifiers(*clause.condition, names);
                    LOG_INFO("[verification] %s @ %s (mode=%s, status=%s) unresolvable: identifiers={%s} params={%s}",
                             obligation.description.c_str(), obligation.subject.c_str(),
                             mode_name(obligation.mode), status_name(obligation.status),
                             join_set(names).c_str(),
                             join_set(std::set<std::string>(params.begin(), params.end())).c_str());
                }
                result.ir.obligations.push_back(std::move(obligation));
                continue;
            }
            subject.contracts.push_back(&clause);
            if (clause.isRequire()) subject.inherited_requires.push_back(&clause);
        }
    };

    for (const auto& symbol : globalScope->symbols() | std::views::values)
    {
        if (!symbol || !seen.insert(symbol.get()).second)
            continue;

        if (const auto fn = std::dynamic_pointer_cast<FunctionSymbol>(symbol))
        {
            if (fn->kind == SymbolKind::ExternFunction)
                continue;
            symbols++;
            Subject subject;
            subject.key = fn.get();
            subject.function = fn.get();
            subject.contracts.assign(fn->contracts.begin(), fn->contracts.end());
            if (!fn->uowName.empty())
                inherit_uow(subject, fn->uowName, fn->uowLocation, fn->uowPhase);
            subject.obligation_rows = collect_obligations(result.ir, trace, fn->name,
                                                          subject.contracts, fn->paramTypes,
                                                          fn->paramNames);
            if (fn->kind == SymbolKind::Function && fn->hasBody() && !fn->isFromLibrary)
                subjects.push_back(std::move(subject));
        }
        else if (const auto strct = std::dynamic_pointer_cast<StructSymbol>(symbol))
        {
            for (const auto& method : strct->methods)
            {
                if (!method || !seen.insert(method.get()).second)
                    continue;
                symbols++;
                Subject subject;
                subject.key = method.get();
                subject.method = method.get();
                subject.contracts.assign(method->contracts.begin(), method->contracts.end());
                if (!method->uowName.empty())
                    inherit_uow(subject, method->uowName, method->uowLocation, method->uowPhase);
                const std::string name = method->structName + "::" + method->name;
                subject.obligation_rows = collect_obligations(result.ir, trace, name,
                                                              subject.contracts,
                                                              method->paramTypes,
                                                              method->paramNames);
                if (method->hasBody() && !method->isAbstract && !method->isFromLibrary)
                    subjects.push_back(std::move(subject));
            }
        }
    }

    const auto seeds_of = [&globalScope](const Subject& subject) -> std::vector<const ContractClause*>
    {
        std::vector<const ContractClause*> seeds;
        if (subject.method && globalScope)
        {
            if (const auto strct = globalScope->lookupStruct(subject.method->structName))
                seeds = strct->invariants;
        }
        // Inherited uow requires hold at the member's entry (its own requires
        // are seeded inside the analyzer from the symbol).
        seeds.insert(seeds.end(), subject.inherited_requires.begin(),
                     subject.inherited_requires.end());
        return seeds;
    };

    // Interprocedural analysis hooks. Silent passes only harvest facts; the
    // emitting pass additionally checks every call site it walks through.
    const auto analyze = [this, &globalScope, &result, &seeds_of](const Subject& subject, const bool emit,
                                                                  BodySemantics* semantics)
    {
        const auto seeds = seeds_of(subject);
        const auto on_call = [this, &globalScope, &result, emit](const FunctionCall& call, const FactSet& facts)
        {
            // Require decisions run on every pass (they poison postcondition
            // harvesting); only the emitting pass prints diagnostics.
            check_call_site(result, call, facts, globalScope, emit);
        };
        const auto postcondition = [this, &globalScope](const FunctionCall& call)
        {
            return call_postcondition(call, globalScope);
        };
        if (subject.function)
            return analyze_function_body(*subject.function, on_call, postcondition, semantics, seeds);
        return analyze_method_body(*subject.method, on_call, postcondition, semantics, seeds);
    };

    const auto contracts_of = [](const Subject& subject) -> const std::vector<const ContractClause*>&
    {
        return subject.contracts;
    };
    const auto params_of = [](const Subject& subject) -> const std::vector<std::string>&
    {
        return subject.function ? subject.function->paramNames : subject.method->paramNames;
    };
    const auto sections_of = [](const Subject& subject) -> const std::vector<const SemanticSection*>&
    {
        return subject.function ? subject.function->sections : subject.method->sections;
    };
    const auto name_of = [](const Subject& subject) -> std::string
    {
        return subject.function ? subject.function->name
                                : subject.method->structName + "::" + subject.method->name;
    };

    // Convergence: postconditions extracted from proven ensures and effect
    // sets assembled from callee summaries feed the next round of body
    // analyses, so chains (transfer → debit → credit) settle in a few passes.
    // Churning summaries mean a cycle — drop propagation entirely rather
    // than trust an unstable fixpoint.
    bool converged = false;
    for (int iteration = 0; iteration < 8 && !converged; iteration++)
    {
        converged = true;
        for (const auto& subject : subjects)
        {
            PostconditionSummary before_post;
            if (const auto it = _postconditions.find(subject.key); it != _postconditions.end())
                before_post = it->second;
            std::set<SemanticEffect> before_effects;
            if (const auto it = _effect_sets.find(subject.key); it != _effect_sets.end())
                before_effects = it->second;

            BodySemantics raw;
            if (trace)
            {
                LOG_INFO("[verification] analyzing %s (iteration %d, key=%p)", name_of(subject).c_str(),
                         iteration, static_cast<const void*>(subject.key));
            }
            derive_postcondition(*subject.key, contracts_of(subject), params_of(subject),
                                 analyze(subject, false, &raw));

            std::set<SemanticEffect> effects;
            for (const auto& callee : raw.calls)
            {
                if (callee.find("::") == std::string::npos)
                {
                    if (const auto* builtin = builtin_effects(callee))
                        effects.insert(builtin->begin(), builtin->end());
                }
                if (const Symbol* callee_symbol = resolve_callee_by_name(callee, globalScope))
                {
                    if (const auto it = _effect_sets.find(callee_symbol); it != _effect_sets.end())
                        effects.insert(it->second.begin(), it->second.end());
                }
            }

            _semantics[subject.key] = std::move(raw);
            _effect_sets[subject.key] = effects;

            PostconditionSummary after_post;
            if (const auto it = _postconditions.find(subject.key); it != _postconditions.end())
                after_post = it->second;

            if (!summary_equal(before_post, after_post) || before_effects != effects)
                converged = false;
        }
    }
    if (!converged)
    {
        _postconditions.clear();
        _effect_sets.clear();
    }

    // Emitting pass: decisions and diagnostics run on the converged facts.
    for (const auto& subject : subjects)
    {
        if (trace)
        {
            LOG_INFO("[verification] checking %s (key=%p)", name_of(subject).c_str(),
                     static_cast<const void*>(subject.key));
        }
        const auto summary = analyze(subject, true, nullptr);
        diff_contracts(result, contracts_of(subject), subject.obligation_rows, name_of(subject), summary);
        diff_sections(result, sections_of(subject), _semantics[subject.key], _effect_sets[subject.key],
                      name_of(subject));
        if (subject.method)
        {
            if (const auto strct = globalScope->lookupStruct(subject.method->structName))
                check_invariants(result, *subject.method, *strct, summary);
        }
    }

    if (trace)
    {
        LOG_INFO("[verification] %zu symbol(s) scanned, %zu obligation(s) collected", symbols,
                 result.ir.obligations.size());
    }

    // uow membership comes from the analyzed subjects.
    for (const auto& subject : subjects)
    {
        if (!subject.uow) continue;
        uow_members[subject.uow].push_back(
            {subject.key, subject.function, subject.method, subject.uow_phase});
    }

    // Static uow checks (§9/§10) with the program-wide ordering graph (§7.3).
    std::map<std::string, std::pair<std::string, bool>> ordering_keys;
    for (const auto& program : programs)
    {
        for (const auto& uow : program->uows)
            verify_uow(result, *uow, uow_members, globalScope, ordering_keys);
    }

    return result;
}

Verifier::CalleeView Verifier::resolve_callee(const FunctionCall& call,
                                              const std::shared_ptr<ScopedSymbolTable>& globalScope) const
{
    if (!globalScope || call.resolvedCalleeName.empty())
        return {};

    CalleeView view;
    view.displayName = call.resolvedCalleeName;

    if (!call.resolvedCalleeStruct.empty())
    {
        const auto strct = globalScope->lookupStruct(call.resolvedCalleeStruct);
        if (!strct) return {};
        const auto method = strct->getMethod(call.resolvedCalleeName);
        if (!method) return {};
        view.symbol = method.get();
        view.contracts = &method->contracts;
        view.paramNames = &method->paramNames;
        view.displayName = method->structName + "::" + method->name;
        return view;
    }

    const auto fn = globalScope->lookupFunction(call.resolvedCalleeName);
    if (!fn) return {};
    view.symbol = fn.get();
    view.contracts = &fn->contracts;
    view.paramNames = &fn->paramNames;
    return view;
}

void Verifier::check_call_site(VerificationResult& result, const FunctionCall& call,
                               const djinn::verification::FactSet& facts,
                               const std::shared_ptr<ScopedSymbolTable>& globalScope, const bool emit)
{
    // Spec-first stubs (§12.3): a callee with contracts but no visible body
    // — extern or prototype — is checked exactly like an implemented one.
    const CalleeView callee = resolve_callee(call, globalScope);
    if (!callee.symbol || callee.contracts->empty()) return;
    if (call.arguments.size() < callee.paramNames->size()) return;

    // Bind the callee's parameters to the argument values the fragment can
    // express; everything else stays Unknown in the callee's view.
    FactSet bindings = facts;
    for (size_t i = 0; i < callee.paramNames->size(); i++)
    {
        const auto argument = translate_linear(*call.arguments[i]);
        if (argument) bindings.set_equality((*callee.paramNames)[i], *argument);
        else bindings.kill((*callee.paramNames)[i]);
    }

    for (const auto* clause : *callee.contracts)
    {
        if (!clause || !clause->isRequire() || !clause->condition) continue;
        // `assume`d requires are trusted; `ignore`d ones are documentation.
        if (clause->mode != ContractClause::Mode::Check && clause->mode != ContractClause::Mode::Prove)
            continue;

        const auto condition = translate_condition(*clause->condition);
        if (!condition) continue;
        if (bindings.decide(*condition) != TriBool::False) continue;

        // The callee provably throws here: it has no normal return, so no
        // postcondition may be harvested from this call site. The decision
        // happens on every pass; the diagnostic only on the emitting one.
        _failed_calls.insert(&call);
        if (!emit) continue;

        Diagnostic diagnostic(Severity::Error,
                              DiagnosticCode::E_CONTRACT_PRECONDITION_NOT_ESTABLISHED,
                              "call to '" + callee.displayName + "' violates require '" +
                              condition_to_string(*condition) + "'",
                              call.name.location);
        const std::string evidence = evidence_constants(bindings, *callee.paramNames);
        if (!evidence.empty())
            diagnostic.withNote(evidence);
        diagnostic.withHelp("pass arguments that satisfy the require, or weaken it");
        _diagnostics.emitAndPrint(diagnostic);
        result.success = false;
    }
}

std::optional<Affine> Verifier::call_postcondition(
    const FunctionCall& call, const std::shared_ptr<ScopedSymbolTable>& globalScope) const
{
    if (_postconditions.empty()) return std::nullopt;
    if (_failed_calls.contains(&call)) return std::nullopt;

    const CalleeView callee = resolve_callee(call, globalScope);
    if (!callee.symbol) return std::nullopt;

    const auto it = _postconditions.find(callee.symbol);
    if (it == _postconditions.end() || !it->second.propagated) return std::nullopt;

    const auto& params = *callee.paramNames;
    if (call.arguments.size() < params.size()) return std::nullopt;

    // Two-phase substitution: parameters are first renamed to staging names
    // so an argument that mentions a caller variable sharing a parameter's
    // name cannot be captured by a later substitution.
    Affine result = it->second.value;
    for (size_t i = 0; i < params.size(); i++)
    {
        const auto staged = result.substituted(params[i],
                                                Affine::of_variable("\x01" + std::to_string(i)));
        if (!staged) return std::nullopt;
        result = *staged;
    }
    for (size_t i = 0; i < params.size(); i++)
    {
        const auto argument = translate_linear(*call.arguments[i]);
        if (!argument) return std::nullopt;
        const auto expanded = result.substituted("\x01" + std::to_string(i), *argument);
        if (!expanded) return std::nullopt;
        result = *expanded;
    }
    return result;
}

void Verifier::derive_postcondition(const Symbol& subject,
                                    const std::vector<const ContractClause*>& contracts,
                                    const std::vector<std::string>& paramNames,
                                    const BodySummary& body)
{
    _postconditions.erase(&subject);
    if (body.returns.empty()) return;

        for (const auto* clause : contracts)
        {
            if (!clause || !clause->isEnsure() || !clause->condition) continue;

            // Ensures talk about the exit state, so field paths name the
            // post-values and `old(path)` names the entry snapshot.
            const auto condition = translate_condition(*clause->condition, true);
        if (!condition || condition->kind != Condition::Kind::Comparison ||
            condition->op != TokenType::EQUAL_EQUAL)
            continue;

        // ensure(return == E) where E only mentions the callee's parameters —
        // anything else (fields, globals) has no caller-visible meaning.
        const Affine* affine_side = nullptr;
        std::string name;
        if (condition->left.is_single_variable(&name) && name == "return")
            affine_side = &condition->right;
        else if (condition->right.is_single_variable(&name) && name == "return")
            affine_side = &condition->left;
        if (!affine_side) continue;

        if (std::any_of(affine_side->coefficients.begin(), affine_side->coefficients.end(),
                        [&paramNames](const auto& entry)
                        {
                            return std::find(paramNames.begin(), paramNames.end(), entry.first) ==
                                   paramNames.end();
                        }))
            continue;

        // Same discharge rule diff_contracts uses to prove: the equality must
        // be decidable and hold on every return path.
        bool holds_everywhere = true;
        for (const auto& path : body.returns)
        {
            if (!path.value)
            {
                holds_everywhere = false;
                break;
            }
            FactSet facts = path.facts;
            facts.set_equality("return", *path.value);
            if (facts.decide(*condition) != TriBool::True)
            {
                holds_everywhere = false;
                break;
            }
        }
        if (!holds_everywhere) continue;

        PostconditionSummary summary;
        summary.propagated = true;
        summary.value = *affine_side;
        _postconditions.emplace(&subject, std::move(summary));
        return;
    }
}

void Verifier::diff_contracts(VerificationResult& result,
                              const std::vector<const ContractClause*>& contracts,
                              const std::vector<size_t>& obligation_rows,
                              const std::string& subject, const BodySummary& body)
{
    const bool trace = _mode == VerificationMode::Trace;

    const auto emit_unprovable = [&](const std::string& kind_text, const std::string& clause_text,
                                     const SourceLocation& location)
    {
        Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_UNPROVABLE,
                              "cannot prove " + kind_text + " '" + clause_text + "' of '" + subject +
                              "' in mode prove",
                              location);
        diagnostic.withLabel("not decidable at compile time")
                  .withHelp("use 'in mode check' to keep the runtime check, or 'in mode assume' to trust it");
        _diagnostics.emitAndPrint(diagnostic);
        result.success = false;
    };

    for (size_t i = 0; i < contracts.size(); i++)
    {
        const auto* clause = contracts[i];
        if (!clause || !clause->condition || clause->mode == ContractClause::Mode::Ignore) continue;

        Obligation* row = nullptr;
        if (i < obligation_rows.size() && obligation_rows[i] != NO_ROW &&
            obligation_rows[i] < result.ir.obligations.size())
            row = &result.ir.obligations[obligation_rows[i]];

        // Requires are preconditions: assumed in the body, runtime-checked,
        // and checked at call sites. `in mode prove` additionally demands a
        // context-free proof (spec §4: subsumes constant-folded requires).
        if (clause->isRequire())
        {
            if (clause->mode != ContractClause::Mode::Prove) continue;
            const auto condition = translate_condition(*clause->condition);
            const auto decided = condition ? FactSet{}.decide(*condition) : TriBool::Unknown;
            const std::string clause_text = condition ? condition_to_string(*condition) : "?";
            if (decided == TriBool::True)
            {
                if (row)
                {
                    row->status = ObligationStatus::Proven;
                    row->justification = "constant-decidable: " + clause_text;
                }
                if (trace)
                    LOG_INFO("[verification] require @ %s proven: %s", subject.c_str(), clause_text.c_str());
            }
            else
            {
                emit_unprovable("require", clause_text, clause->condition->location);
            }
            continue;
        }

        if (clause->mode == ContractClause::Mode::Assume) continue;

        // Ensures talk about the exit state: field paths name the post-values
        // and `old(path)` names the entry snapshot (spec §6.3).
        const auto condition = translate_condition(*clause->condition, true);
        if (!condition)
        {
            if (clause->mode == ContractClause::Mode::Prove)
                emit_unprovable("ensure", "?", clause->condition->location);
            continue;
        }

        // Every return path must establish the ensured property. A single
        // path proving otherwise is a contradiction (spec §6.2); undecided
        // paths keep the runtime check in place.
        auto overall = TriBool::True;
        if (body.returns.empty()) overall = TriBool::Unknown;

        for (const auto& path : body.returns)
        {
            FactSet facts = path.facts;
            if (path.value) facts.set_equality("return", *path.value);
            const auto decided = facts.decide(*condition);
            if (decided == TriBool::False)
            {
                overall = TriBool::False;
                break;
            }
            if (decided == TriBool::Unknown)
                overall = TriBool::Unknown;
        }

        const std::string clause_text = condition_to_string(*condition);

        if (overall == TriBool::False)
        {
            Diagnostic diagnostic(Severity::Error,
                                  DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "ensure '" + clause_text + "' does not hold in '" + subject + "'",
                                  clause->condition->location);
            diagnostic.withLabel("violated on some return path")
                      .withHelp("fix the body, or weaken the ensure clause");
            _diagnostics.emitAndPrint(diagnostic);
            result.success = false;
        }
        else if (overall == TriBool::True)
        {
            if (row)
            {
                row->status = ObligationStatus::Proven;
                row->justification = "inferred from body: every return path establishes " + clause_text;
            }
            if (trace)
            {
                LOG_INFO("[verification] ensure @ %s proven: %s", subject.c_str(), clause_text.c_str());
            }
        }
        else if (clause->mode == ContractClause::Mode::Prove)
        {
            emit_unprovable("ensure", clause_text, clause->condition->location);
        }
    }
}

void Verifier::diff_sections(VerificationResult& result, const std::vector<const SemanticSection*>& sections,
                             const BodySemantics& semantics, const std::set<SemanticEffect>& effects,
                             const std::string& subject)
{
    const auto fail_claim = [&](const std::string& message, const std::string& declared,
                                const std::string& inferred, const std::string& help,
                                const SourceLocation& location)
    {
        Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                              message, location);
        diagnostic.withNote("declared: " + declared + " | body: " + inferred).withHelp(help);
        _diagnostics.emitAndPrint(diagnostic);
        result.success = false;
    };

    std::set<std::string> declared_reads, declared_writes;
    std::set<SemanticEffect> declared_effects;
    const SemanticSection* writes_section = nullptr;
    const SemanticSection* reads_section = nullptr;
    const SemanticSection* effects_section = nullptr;

    for (const auto* section : sections)
    {
        if (!section) continue;
        if (section->kind == SemanticKind::Reads || section->kind == SemanticKind::Writes ||
            section->kind == SemanticKind::Effects)
        {
            for (const auto& item : section->items)
            {
                if (section->kind == SemanticKind::Effects)
                {
                    declared_effects.insert(item.effect);
                    continue;
                }
                const std::string path = normalize_path(join_path(item.path));
                if (section->kind == SemanticKind::Reads)
                    declared_reads.insert(path);
                else
                    declared_writes.insert(path);
            }
            if (section->kind == SemanticKind::Reads && !reads_section) reads_section = section;
            if (section->kind == SemanticKind::Writes && !writes_section) writes_section = section;
            if (section->kind == SemanticKind::Effects && !effects_section) effects_section = section;
        }
        // access / atomic / isolation / ordering / retry / strategy /
        // propagation have no function-level checks in this slice: they are
        // uow-scope claims. Recorded on the uow path only.
    }

    // inferred ⊆ declared for effects and reads; inferred == declared for
    // writes (VERIFICATION-SPEC.md §6.2).
    if (effects_section)
    {
        std::set<SemanticEffect> hidden;
        for (const auto& effect : effects)
            if (!declared_effects.contains(effect)) hidden.insert(effect);
        if (!hidden.empty())
        {
            fail_claim("'" + subject + "' performs undeclared effect(s): " + join_set(hidden),
                       declared_effects.empty() ? "(none)" : join_set(declared_effects),
                       effects.empty() ? "(none)" : join_set(effects),
                       "add them to the effects section", effects_section->location);
        }
    }
    // Inferred paths record the source spelling ("this.balance"); claims are
    // canonically "self.field" — normalize both sides before comparing.
    std::set<std::string> inferred_reads, inferred_writes;
    for (const auto& read : semantics.reads) inferred_reads.insert(normalize_path(read));
    for (const auto& write : semantics.writes) inferred_writes.insert(normalize_path(write));

    if (reads_section)
    {
        std::set<std::string> hidden;
        for (const auto& read : inferred_reads)
            if (!declared_reads.contains(read)) hidden.insert(read);
        if (!hidden.empty())
        {
            fail_claim("'" + subject + "' reads undeclared resource(s): " + join_set(hidden),
                       declared_reads.empty() ? "(none)" : join_set(declared_reads),
                       inferred_reads.empty() ? "(none)" : join_set(inferred_reads),
                       "add them to the reads section", reads_section->location);
        }
    }
    if (writes_section)
    {
        std::set<std::string> missing, extra;
        for (const auto& write : inferred_writes)
            if (!declared_writes.contains(write)) missing.insert(write);
        for (const auto& write : declared_writes)
            if (!inferred_writes.contains(write)) extra.insert(write);
        if (!missing.empty())
        {
            fail_claim("'" + subject + "' writes " + join_set(missing) +
                           ", which the writes section does not list",
                       declared_writes.empty() ? "(none)" : join_set(declared_writes),
                       inferred_writes.empty() ? "(none)" : join_set(inferred_writes),
                       "add them to the writes section", writes_section->location);
        }
        if (!extra.empty())
        {
            fail_claim("the writes section of '" + subject + "' lists " + join_set(extra) +
                           ", but the body never writes it",
                       join_set(declared_writes),
                       inferred_writes.empty() ? "(none)" : join_set(inferred_writes),
                       "remove it from the writes section", writes_section->location);
        }
    }

    ClaimSet claim;
    claim.subject = subject;
    claim.reads.assign(inferred_reads.begin(), inferred_reads.end());
    claim.writes.assign(inferred_writes.begin(), inferred_writes.end());
    for (const auto& effect : effects)
        claim.effects.push_back(semantic_effect_name(effect));
    result.ir.claims.push_back(std::move(claim));
}

void Verifier::check_invariants(VerificationResult& result, const MethodSymbol& method,
                                const StructSymbol& strct, const BodySummary& body)
{
    if (strct.invariants.empty()) return;

    const bool trace = _mode == VerificationMode::Trace;
    const std::string subject = method.structName + "::" + method.name;

    for (const auto* invariant : strct.invariants)
    {
        if (!invariant || !invariant->condition) continue;
        if (invariant->mode == ContractClause::Mode::Ignore) continue;

        if (invariant->mode != ContractClause::Mode::Prove)
        {
            // `assume` is trusted; `check` would need runtime injection,
            // which this phase does not do — never a silent downgrade.
            if (invariant->mode == ContractClause::Mode::Check)
            {
                Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_UNPROVABLE,
                                      "runtime-checked invariants are not supported yet (struct '" +
                                      method.structName + "')",
                                      invariant->condition->location);
                diagnostic.withHelp("use 'in mode prove' (compile-time proof) or 'in mode assume'");
                _diagnostics.emitAndPrint(diagnostic);
                result.success = false;
            }
            continue;
        }

        const auto condition = translate_condition(*invariant->condition);
        const std::string clause_text = condition ? condition_to_string(*condition) : "?";
        if (!condition)
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_UNPROVABLE,
                                  "cannot prove invariant '" + clause_text + "' of struct '" +
                                      method.structName + "'",
                                  invariant->condition->location);
            diagnostic.withLabel("outside the decidable fragment")
                      .withHelp("keep the invariant in linear arithmetic, or use 'in mode assume'");
            _diagnostics.emitAndPrint(diagnostic);
            result.success = false;
            continue;
        }

        // The invariant is assumed at entry (seeded by the analysis), so
        // methods that never touch the field discharge it trivially; writers
        // must re-establish it on every path (spec §10).
        auto overall = body.returns.empty() ? TriBool::Unknown : TriBool::True;
        for (const auto& path : body.returns)
        {
            const auto decided = path.facts.decide(*condition);
            if (decided == TriBool::False)
            {
                overall = TriBool::False;
                break;
            }
            if (decided == TriBool::Unknown)
                overall = TriBool::Unknown;
        }

        if (overall == TriBool::False)
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_INVARIANT_NOT_PRESERVED,
                                  "method '" + subject + "' breaks invariant '" + clause_text +
                                      "' of struct '" + method.structName + "'",
                                  invariant->condition->location);
            diagnostic.withLabel("violated on some return path")
                      .withHelp("make every exit restore the invariant, or weaken it");
            _diagnostics.emitAndPrint(diagnostic);
            result.success = false;
        }
        else if (overall == TriBool::Unknown)
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_UNPROVABLE,
                                  "cannot prove invariant '" + clause_text + "' at every exit of '" +
                                      subject + "'",
                                  invariant->condition->location);
            diagnostic.withHelp("make the write decidable at compile time, or use 'in mode assume'");
            _diagnostics.emitAndPrint(diagnostic);
            result.success = false;
        }
        else if (trace)
        {
            LOG_INFO("[verification] invariant @ %s proven: %s", subject.c_str(), clause_text.c_str());
        }
    }
}

void Verifier::expand_callee_paths(const FunctionCall& call, BodySemantics& aggregate,
                                   const std::shared_ptr<ScopedSymbolTable>& globalScope,
                                   std::map<std::string, SourceLocation>* write_sites,
                                   const std::set<std::string>* callLockRoots) const
{
    const CalleeView callee = resolve_callee(call, globalScope);
    if (!callee.symbol) return;
    const auto paths = _semantics.find(callee.symbol);
    if (paths == _semantics.end()) return;

    // Substitute the callee's resource paths by plain-identifier arguments:
    // debit(from, amount) contributing "acct.balance" becomes "from.balance";
    // a method call binds its receiver to the callee's self/this spelling.
    std::map<std::string, std::string> binding;
    if (callee.paramNames)
    {
        const size_t count = std::min(callee.paramNames->size(), call.arguments.size());
        for (size_t i = 0; i < count; i++)
        {
            if (const auto* identifier = dynamic_cast<const Identifier*>(call.arguments[i].get()))
                binding[(*callee.paramNames)[i]] = identifier->name();
        }
    }
    if (call.receiver)
    {
        if (const auto* receiver = dynamic_cast<const Identifier*>(call.receiver.get()))
        {
            binding["self"] = receiver->name();
            binding["this"] = receiver->name();
        }
    }
    const auto rewrite = [&binding](const std::string& path)
    {
        const auto dot = path.find('.');
        if (dot == std::string::npos) return path;
        const auto it = binding.find(path.substr(0, dot));
        if (it == binding.end()) return path;
        return it->second + path.substr(dot);
    };

    for (const auto& path : paths->second.writes)
    {
        const std::string rewritten = normalize_path(rewrite(path));
        aggregate.writes.insert(rewritten);
        if (write_sites) write_sites->emplace(rewritten, call.name.location);
        // A callee write inherits the lock evidence of its call site.
        if (callLockRoots && !callLockRoots->empty())
        {
            auto& roots = aggregate.write_lock_roots[rewritten];
            roots.insert(callLockRoots->begin(), callLockRoots->end());
        }
        else
        {
            aggregate.unlocked_writes.insert(rewritten);
        }
    }
    for (const auto& path : paths->second.reads)
        aggregate.reads.insert(normalize_path(rewrite(path)));
}

void Verifier::verify_uow(VerificationResult& result, const UowDeclaration& uow,
                          const std::map<const UowDeclaration*, std::vector<UowMember>>& members,
                          const std::shared_ptr<ScopedSymbolTable>& globalScope,
                          std::map<std::string, std::pair<std::string, bool>>& ordering_keys)
{
    const bool trace = _mode == VerificationMode::Trace;
    const std::string subject = "uow " + uow.name.token_name;

    static const std::vector<UowMember> no_members;
    const auto mine = members.find(&uow);
    const std::vector<UowMember>& attached = mine != members.end() ? mine->second : no_members;

    if (attached.empty())
    {
        Diagnostic diagnostic(Severity::Warning, DiagnosticCode::E_CONTRACT_UOW_NO_MEMBERS,
                              "uow '" + uow.name.token_name + "' has no participating functions",
                              uow.name.location);
        diagnostic.withHelp("attach one with 'uow (" + uow.name.token_name +
                            ")' next to the function's body");
        _diagnostics.emitAndPrint(diagnostic);
    }

    const auto span = [](const SourceLocation& loc)
    {
        return loc.fileId + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
    };

    const auto fail = [&](Diagnostic diagnostic)
    {
        diagnostic.notes.insert(diagnostic.notes.begin(),
                                "uow '" + uow.name.token_name + "' defined at " + span(uow.name.location));
        _diagnostics.emitAndPrint(diagnostic);
        result.success = false;
    };

    const auto member_name = [](const UowMember& member)
    {
        return member.function ? member.function->name
                               : member.method->structName + "::" + member.method->name;
    };

    std::set<std::string> declared_reads, declared_writes, declared_atomic;
    std::set<SemanticEffect> declared_effects;
    std::map<std::string, SemanticQualifier> access_modes;
    SemanticStrategy strategy = SemanticStrategy::None;
    SemanticQualifier retry = SemanticQualifier::Disabled;
    std::vector<std::pair<std::string, bool>> orderings;
    SourceLocation writes_location = uow.name.location;
    SourceLocation reads_location = uow.name.location;
    SourceLocation effects_location = uow.name.location;
    SourceLocation atomic_location = uow.name.location;

    for (const auto& section : uow.sections)
    {
        if (section.kind == SemanticKind::Reads || section.kind == SemanticKind::Writes ||
            section.kind == SemanticKind::Atomic)
        {
            auto& target = section.kind == SemanticKind::Reads
                               ? declared_reads
                               : section.kind == SemanticKind::Writes ? declared_writes : declared_atomic;
            for (const auto& item : section.items)
                target.insert(normalize_path(join_path(item.path)));
            if (section.kind == SemanticKind::Writes) writes_location = section.location;
            if (section.kind == SemanticKind::Reads) reads_location = section.location;
            if (section.kind == SemanticKind::Atomic) atomic_location = section.location;
        }
        else if (section.kind == SemanticKind::Effects)
        {
            for (const auto& item : section.items)
                declared_effects.insert(item.effect);
            effects_location = section.location;
        }
        else if (section.kind == SemanticKind::Strategy)
        {
            strategy = section.strategy;
        }
        else if (section.kind == SemanticKind::Retry)
        {
            if (!section.items.empty()) retry = section.items.front().qualifier;
        }
        else if (section.kind == SemanticKind::Access)
        {
            for (const auto& item : section.items)
            {
                const std::string key = normalize_path(join_path(item.path));
                const auto existing = access_modes.find(key);
                if (existing != access_modes.end() && existing->second != item.qualifier)
                {
                    Diagnostic diagnostic(Severity::Error,
                                          DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                          "'" + key + "' is declared both " +
                                              semantic_qualifier_name(existing->second) + " and " +
                                              semantic_qualifier_name(item.qualifier) + " in uow '" +
                                              uow.name.token_name + "'",
                                          section.location);
                    diagnostic.withHelp("pick one access mode per operand");
                    fail(diagnostic);
                }
                else
                {
                    access_modes[key] = item.qualifier;
                }
            }
        }
        else if (section.kind == SemanticKind::Ordering)
        {
            for (const auto& item : section.items)
                orderings.emplace_back(normalize_path(join_path(item.path)),
                                       item.qualifier == SemanticQualifier::Ascending);
        }
        // isolation / propagation: recorded vocabulary, no additional v1 check.
    }

    // Aggregate inference across the attached members: their direct field
    // paths, the resource paths of the callees they invoke (one expansion
    // level), and the transitive effect sets converged by the fixpoint.
    // after_commit members are tracked apart — they sit outside the
    // transaction and the retry window.
    BodySemantics transaction, post_commit;
    std::set<SemanticEffect> transaction_effects, post_effects;
    std::map<SemanticEffect, SourceLocation> transaction_effect_sites, post_effect_sites;
    std::map<std::string, std::string> write_member;

    const auto attribute_effects = [](const BodySemantics& source, std::set<SemanticEffect>& effects,
                                      std::map<SemanticEffect, SourceLocation>& sites,
                                      const std::shared_ptr<ScopedSymbolTable>& scope,
                                      const std::unordered_map<const Symbol*, std::set<SemanticEffect>>&
                                          effectSets)
    {
        for (const auto& [calleeName, callSite] : source.call_sites)
        {
            std::set<SemanticEffect> contributed;
            if (calleeName.find("::") == std::string::npos)
            {
                if (const auto* builtin = builtin_effects(calleeName))
                    contributed = *builtin;
            }
            if (const Symbol* calleeSymbol = resolve_callee_by_name(calleeName, scope))
            {
                const auto it = effectSets.find(calleeSymbol);
                if (it != effectSets.end())
                    contributed.insert(it->second.begin(), it->second.end());
            }
            for (const auto& effect : contributed)
                sites.emplace(effect, callSite);
            effects.insert(contributed.begin(), contributed.end());
        }
    };

    const auto merge_into = [](BodySemantics& target, const BodySemantics& source,
                               const SourceLocation& fallback)
    {
        for (const auto& path : source.writes)
        {
            target.writes.insert(path);
            const auto site = source.write_sites.find(path);
            target.write_sites.emplace(path, site != source.write_sites.end()
                                                 ? site->second : fallback);
        }
        for (const auto& path : source.reads)
        {
            target.reads.insert(path);
            const auto site = source.read_sites.find(path);
            target.read_sites.emplace(path, site != source.read_sites.end()
                                                ? site->second : fallback);
        }
        for (const auto& [path, roots] : source.write_lock_roots)
        {
            auto& target_roots = target.write_lock_roots[path];
            target_roots.insert(roots.begin(), roots.end());
        }
        target.unlocked_writes.insert(source.unlocked_writes.begin(), source.unlocked_writes.end());
        for (const auto& [callee, roots] : source.call_lock_roots)
        {
            auto& target_roots = target.call_lock_roots[callee];
            target_roots.insert(roots.begin(), roots.end());
        }
        target.unlocked_calls.insert(source.unlocked_calls.begin(), source.unlocked_calls.end());
        for (const auto& [calleeName, callSite] : source.call_sites)
            target.call_sites.emplace(calleeName, callSite);
    };

    for (const auto& member : attached)
    {
        const auto sem_it = _semantics.find(member.key);
        if (sem_it == _semantics.end()) continue;
        const BodySemantics& semantics = sem_it->second;
        const bool after = member.phase == UowPhase::AfterCommit;
        BodySemantics& aggregate = after ? post_commit : transaction;

        const std::string name = member_name(member);
        for (const auto& path : semantics.writes)
        {
            aggregate.writes.insert(path);
            const auto site = semantics.write_sites.find(path);
            aggregate.write_sites.emplace(path, site != semantics.write_sites.end()
                                                    ? site->second : uow.name.location);
            if (!after) write_member.emplace(path, name);
        }
        for (const auto& path : semantics.reads)
        {
            aggregate.reads.insert(path);
            const auto site = semantics.read_sites.find(path);
            aggregate.read_sites.emplace(path, site != semantics.read_sites.end()
                                                   ? site->second : uow.name.location);
        }

        // Lock evidence: direct writes and per-callee lock roots join into the
        // aggregate; any lock-free occurrence stays flagged as unlocked.
        for (const auto& [path, roots] : semantics.write_lock_roots)
        {
            auto& target = aggregate.write_lock_roots[path];
            target.insert(roots.begin(), roots.end());
        }
        aggregate.unlocked_writes.insert(semantics.unlocked_writes.begin(),
                                         semantics.unlocked_writes.end());

        for (const auto* call : semantics.call_nodes)
        {
            // The call's own lock evidence: union semantics — the same callee
            // invoked without a lock anywhere counts as unlocked everywhere.
            const auto& calleeName = call->resolvedCalleeName;
            std::set<std::string> callRoots;
            if (!semantics.unlocked_calls.contains(calleeName))
            {
                if (const auto roots = semantics.call_lock_roots.find(calleeName);
                    roots != semantics.call_lock_roots.end())
                    callRoots = roots->second;
            }
            expand_callee_paths(*call, aggregate, globalScope,
                                after ? nullptr : &aggregate.write_sites,
                                callRoots.empty() ? nullptr : &callRoots);
        }

        // Inline lifecycle blocks: before_commit joins the transaction
        // aggregate (retry and placement apply), after_commit joins
        // post_commit (claims apply, the retry window does not).
        if (const auto before = semantics.phase_blocks.find(static_cast<int>(UowPhase::BeforeCommit));
            before != semantics.phase_blocks.end() && before->second)
        {
            merge_into(transaction, *before->second, uow.name.location);
            attribute_effects(*before->second, transaction_effects, transaction_effect_sites,
                              globalScope, _effect_sets);
        }
        if (const auto commit = semantics.phase_blocks.find(static_cast<int>(UowPhase::AfterCommit));
            commit != semantics.phase_blocks.end() && commit->second)
        {
            merge_into(post_commit, *commit->second, uow.name.location);
            attribute_effects(*commit->second, post_effects, post_effect_sites,
                              globalScope, _effect_sets);
        }

        auto& target_effects = after ? post_effects : transaction_effects;
        auto& target_sites = after ? post_effect_sites : transaction_effect_sites;
        const auto eff_it = _effect_sets.find(member.key);
        if (eff_it != _effect_sets.end())
            target_effects.insert(eff_it->second.begin(), eff_it->second.end());
        attribute_effects(semantics, target_effects, target_sites, globalScope, _effect_sets);
    }

    const auto site_of = [](const auto& sites, const auto& key, const SourceLocation& fallback)
        -> SourceLocation
    {
        const auto it = sites.find(key);
        return it != sites.end() ? it->second : fallback;
    };

    // §6.2 claim-vs-inference: the transaction members jointly realize exactly
    // the declared writes; reads and effects stay subsets of the claims.
    if (!declared_writes.empty())
    {
        std::set<std::string> missing, extra;
        for (const auto& write : transaction.writes)
            if (!declared_writes.contains(write)) missing.insert(write);
        for (const auto& write : declared_writes)
            if (!transaction.writes.contains(write)) extra.insert(write);
        if (!missing.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "write to '" + *missing.begin() + "' is not declared in uow '" +
                                      uow.name.token_name + "'",
                                  site_of(transaction.write_sites, *missing.begin(),
                                          uow.name.location));
            const auto writer = write_member.find(*missing.begin());
            diagnostic.withNote("declared at " + span(writes_location) + ": " +
                                join_set(declared_writes) +
                                (writer != write_member.end()
                                     ? " | written in member '" + writer->second + "'"
                                     : ""))
                      .withHelp("add it to the writes section");
            fail(diagnostic);
        }
        if (!extra.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "the writes section lists '" + *extra.begin() +
                                      "', but no member of uow '" + uow.name.token_name +
                                      "' writes it",
                                  writes_location);
            diagnostic.withNote("declared at " + span(writes_location) + ": " + join_set(declared_writes))
                      .withHelp("remove it from the writes section");
            fail(diagnostic);
        }
    }
    if (!declared_reads.empty())
    {
        std::set<std::string> all_reads = transaction.reads;
        all_reads.insert(post_commit.reads.begin(), post_commit.reads.end());
        std::set<std::string> hidden;
        for (const auto& read : all_reads)
            if (!declared_reads.contains(read)) hidden.insert(read);
        if (!hidden.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "read of '" + *hidden.begin() + "' is not declared in uow '" +
                                      uow.name.token_name + "'",
                                  site_of(transaction.read_sites, *hidden.begin(),
                                          site_of(post_commit.read_sites, *hidden.begin(),
                                                  uow.name.location)));
            diagnostic.withNote("reads declared at " + span(reads_location))
                      .withHelp("add it to the reads section");
            fail(diagnostic);
        }
    }
    if (!declared_effects.empty())
    {
        std::set<SemanticEffect> all_effects = transaction_effects;
        all_effects.insert(post_effects.begin(), post_effects.end());
        std::set<SemanticEffect> hidden;
        for (const auto& effect : all_effects)
            if (!declared_effects.contains(effect)) hidden.insert(effect);
        if (!hidden.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  std::string("undeclared effect '") +
                                      semantic_effect_name(*hidden.begin()) + "' in uow '" +
                                      uow.name.token_name + "'",
                                  site_of(transaction_effect_sites, *hidden.begin(),
                                          site_of(post_effect_sites, *hidden.begin(),
                                                  uow.name.location)));
            diagnostic.withNote("effects declared at " + span(effects_location))
                      .withHelp("add it to the effects section");
            fail(diagnostic);
        }
    }
    // §10: the transaction members realize exactly the declared atomic set —
    // no more, no less.
    if (!declared_atomic.empty())
    {
        std::set<std::string> missing, extra;
        for (const auto& write : transaction.writes)
            if (!declared_atomic.contains(write)) missing.insert(write);
        for (const auto& write : declared_atomic)
            if (!transaction.writes.contains(write)) extra.insert(write);
        if (!missing.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "write to '" + *missing.begin() + "' is not atomic in uow '" +
                                      uow.name.token_name + "'",
                                  site_of(transaction.write_sites, *missing.begin(),
                                          uow.name.location));
            diagnostic.withNote("atomic section at " + span(atomic_location) + ": " +
                                join_set(declared_atomic) + " | uow writes: " +
                                (transaction.writes.empty() ? "(none)" : join_set(transaction.writes)))
                      .withHelp("add it to the atomic section, or drop the write");
            fail(diagnostic);
        }
        if (!extra.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "the atomic section lists '" + *extra.begin() +
                                      "', but uow '" + uow.name.token_name + "' never writes it",
                                  atomic_location);
            diagnostic.withHelp("remove it from the atomic section");
            fail(diagnostic);
        }
    }

    // §10 atomicity discharge: under strategy database the DB transaction
    // provides atomicity (coverage + ordering above). Any other strategy —
    // or none — requires lock evidence: every atomic path (and every path
    // under an exclusive access claim) must be written only inside a lock
    // scope naming its root object.
    if (!declared_atomic.empty() && strategy != SemanticStrategy::Database)
    {
        std::set<std::string> demand = declared_atomic;
        for (const auto& [operand, mode] : access_modes)
        {
            if (mode != SemanticQualifier::Exclusive) continue;
            for (const auto& write : transaction.writes)
            {
                const auto dot = write.find('.');
                if (write.substr(0, dot) == operand) demand.insert(write);
            }
        }
        for (const auto& path : demand)
        {
            if (!transaction.writes.contains(path)) continue;
            const auto dot = path.find('.');
            const std::string root = path.substr(0, dot);
            bool discharged = !transaction.unlocked_writes.contains(path);
            if (discharged)
            {
                const auto roots = transaction.write_lock_roots.find(path);
                discharged = roots != transaction.write_lock_roots.end() &&
                             roots->second.contains(root);
            }
            if (!discharged)
            {
                Diagnostic diagnostic(Severity::Error,
                                      DiagnosticCode::E_CONTRACT_ATOMIC_NOT_DISCHARGED,
                                      "atomic claim on '" + path + "' is not discharged by a lock "
                                          "scope in uow '" + uow.name.token_name + "'",
                                      site_of(transaction.write_sites, path, atomic_location));
                diagnostic.withNote("strategy '" +
                                        std::string(strategy == SemanticStrategy::None
                                                        ? "(none)"
                                                        : semantic_strategy_name(strategy)) +
                                    "' provides no atomicity")
                          .withHelp("wrap the writes in lock (" + root + ") { ... }, or use "
                                    "strategy: database");
                fail(diagnostic);
            }
        }
    }

    // §10 all-or-nothing: a body-phase member that can fail mid-transaction
    // (a call to a throwing callee) and writes atomic paths must declare a
    // rollback set in its catch arms covering every such path.
    {
        const auto collect_paths = [](const Block& block, std::set<std::string>& out, bool& bare)
        {
            const std::function<void(const Block&)> walk = [&](const Block& b)
            {
                for (const auto& stmt : b.statements)
                {
                    if (!stmt) continue;
                    if (const auto* rollback = dynamic_cast<const RollbackStatement*>(stmt.get()))
                    {
                        if (rollback->paths.empty())
                        {
                            bare = true;
                            continue;
                        }
                        for (const auto& path : rollback->paths)
                            out.insert(normalize_path(join_path(path)));
                    }
                    else if (const auto* nested = dynamic_cast<const Block*>(stmt.get()))
                    {
                        walk(*nested);
                    }
                    else if (const auto* lock = dynamic_cast<const LockStatement*>(stmt.get()))
                    {
                        if (lock->body) walk(*lock->body);
                    }
                }
            };
            walk(block);
        };

        for (const auto& member : attached)
        {
            if (member.phase != UowPhase::Body) continue;
            const auto sem_it = _semantics.find(member.key);
            if (sem_it == _semantics.end()) continue;
            const BodySemantics& semantics = sem_it->second;

            bool fallible = false;
            SourceLocation fallible_site;
            for (const auto& [calleeName, callSite] : semantics.call_sites)
            {
                const Symbol* calleeSymbol = resolve_callee_by_name(calleeName, globalScope);
                const auto* fn = dynamic_cast<const FunctionSymbol*>(calleeSymbol);
                const auto* method = dynamic_cast<const MethodSymbol*>(calleeSymbol);
                if ((fn && fn->effectivelyThrowing()) || (method && method->effectivelyThrowing()))
                {
                    fallible = true;
                    fallible_site = callSite;
                    break;
                }
            }
            if (!fallible) continue;

            std::set<std::string> member_atomic;
            for (const auto& write : semantics.writes)
                if (declared_atomic.contains(write)) member_atomic.insert(write);
            for (const auto* call : semantics.call_nodes)
            {
                BodySemantics scratch;
                expand_callee_paths(*call, scratch, globalScope, nullptr, nullptr);
                for (const auto& write : scratch.writes)
                    if (declared_atomic.contains(write)) member_atomic.insert(write);
            }
            if (member_atomic.empty()) continue;

            std::set<std::string> rollback_set;
            bool bare_rollback = false;
            const auto arms = member.function ? member.function->catchArms : member.method->catchArms;
            for (const auto* arm : arms)
            {
                if (arm && arm->body) collect_paths(*arm->body, rollback_set, bare_rollback);
            }

            // A bare `rollback;` restores every struct parameter (and self)
            // wholesale — it covers whatever the callees wrote. Listed paths
            // must cover the atomic set themselves.
            std::set<std::string> uncovered;
            if (!bare_rollback)
            {
                for (const auto& write : member_atomic)
                    if (!rollback_set.contains(write)) uncovered.insert(write);
            }
            if (!uncovered.empty())
            {
                Diagnostic diagnostic(Severity::Error,
                                      DiagnosticCode::E_CONTRACT_ROLLBACK_INCOMPLETE,
                                      "member '" + member_name(member) + "' can fail after writing '" +
                                          *uncovered.begin() + "' without rolling it back",
                                      fallible_site);
                diagnostic.withNote("rollback set: " +
                                        (rollback_set.empty() ? std::string("(none)")
                                                              : join_set(rollback_set)))
                          .withHelp("add a bare 'rollback;' to the catch arm (restores every "
                                    "parameter), or list the missing path");
                fail(diagnostic);
            }
        }
    }

    // §10 ContractViolation coverage: a uow member whose body can violate a
    // check-mode contract (its own or a callee's) must handle it — otherwise
    // the violation escapes and the all-or-nothing property is unverified.
    {
        const auto has_check_contract = [](const auto symbol)
        {
            for (const auto* contract : symbol->contracts)
            {
                if (contract && contract->condition &&
                    (contract->isRequire() || contract->isEnsure()) &&
                    contract->mode == ContractClause::Mode::Check)
                    return true;
            }
            return false;
        };
        const auto error_covers = [&](const std::string& thrown, const std::string& caught)
        {
            if (caught == "Error" || caught == "_") return true;
            if (thrown == caught) return true;
            auto s = globalScope->lookupStruct(caught);
            while (s && s->isErrorType)
            {
                if (s->name == thrown) return true;
                if (s->errorBase.empty()) return false;
                s = globalScope->lookupStruct(s->errorBase);
            }
            return false;
        };

        for (const auto& member : attached)
        {
            if (member.phase != UowPhase::Body) continue;
            const auto* fn = member.function;
            const auto* method = member.method;
            const std::vector<const ContractClause*>& own =
                fn ? fn->contracts : method->contracts;
            const std::vector<const CatchClause*>& arms =
                fn ? fn->catchArms : method->catchArms;
            const bool catchesAll = fn ? fn->catchesAllErrors : method->catchesAllErrors;

            bool mayViolate = false;
            for (const auto* contract : own)
            {
                if (contract && contract->condition &&
                    (contract->isRequire() || contract->isEnsure()) &&
                    contract->mode == ContractClause::Mode::Check)
                    mayViolate = true;
            }
            if (!mayViolate)
            {
                const auto sem_it = _semantics.find(member.key);
                if (sem_it != _semantics.end())
                {
                    for (const auto& calleeName : sem_it->second.calls)
                    {
                        const Symbol* calleeSymbol = resolve_callee_by_name(calleeName, globalScope);
                        const auto* calleeFn = dynamic_cast<const FunctionSymbol*>(calleeSymbol);
                        const auto* calleeMethod = dynamic_cast<const MethodSymbol*>(calleeSymbol);
                        const bool calleeViolates =
                            (calleeFn && has_check_contract(calleeFn)) ||
                            (calleeMethod && has_check_contract(calleeMethod));
                        if (calleeViolates)
                        {
                            mayViolate = true;
                            break;
                        }
                    }
                }
            }
            if (!mayViolate) continue;

            bool covered = catchesAll;
            for (const auto* arm : arms)
            {
                if (arm && error_covers("ContractViolation", arm->errorType.token_name))
                {
                    covered = true;
                    break;
                }
            }
            if (!covered)
            {
                Diagnostic diagnostic(Severity::Error,
                                      DiagnosticCode::E_CONTRACT_VIOLATION_UNHANDLED,
                                      "member '" + member_name(member) +
                                          "' of uow '" + uow.name.token_name +
                                          "' does not handle ContractViolation",
                                      fn ? fn->location : method->location);
                diagnostic.withHelp("add a catch arm for it, or declare the clause "
                                    "'in mode prove'");
                fail(diagnostic);
            }
        }
    }

    // §9/§11 retry-safety: non-idempotent effects (output, spawned
    // processes, durable file mutations) cannot run twice. memory, time and
    // random are safe — a retry allocates fresh resources and re-observes
    // the clock/RNG, duplicating nothing observable.
    const auto first_unsafe = [&](const std::set<SemanticEffect>& effects,
                                  const std::map<SemanticEffect, SourceLocation>& sites)
        -> std::optional<std::pair<SemanticEffect, SourceLocation>>
    {
        static const SemanticEffect non_idempotent[] = {
            SemanticEffect::ExternalIO, SemanticEffect::Process, SemanticEffect::Filesystem};
        for (const auto& effect : non_idempotent)
            if (effects.contains(effect))
                return std::make_pair(effect, site_of(sites, effect, uow.name.location));
        return std::nullopt;
    };

    if (retry == SemanticQualifier::Allowed)
    {
        if (auto offending = first_unsafe(transaction_effects, transaction_effect_sites))
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_RETRY_UNSAFE_EFFECT,
                                  "impure call inside a retryable uow", offending->second);
            diagnostic.withNote(std::string("effect '") +
                                semantic_effect_name(offending->first) +
                                "' may run twice when the uow retries")
                      .withHelp("move it to a member attached with 'uow (" + uow.name.token_name +
                                ".after_commit)', or set retry: disabled");
            fail(diagnostic);
        }
    }

    // §8/§11 effect placement: under strategy database, non-idempotent
    // effects belong in after_commit — they run exactly once, after
    // durability.
    if (strategy == SemanticStrategy::Database)
    {
        if (auto offending = first_unsafe(transaction_effects, transaction_effect_sites);
            offending && retry != SemanticQualifier::Allowed)
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_EFFECT_PLACEMENT,
                                  "impure call inside the transaction", offending->second);
            diagnostic.withNote(std::string("effect '") +
                                semantic_effect_name(offending->first) +
                                "' cannot run inside the transaction")
                      .withHelp("under strategy database, attach it with 'uow (" +
                                uow.name.token_name +
                                ".after_commit)' — it runs exactly once, after the commit");
            fail(diagnostic);
        }
    }

    // uow invariants: static-only phase — nothing discharges them at runtime.
    for (const auto& invariant : uow.invariants)
    {
        if (invariant.mode == ContractClause::Mode::Ignore ||
            invariant.mode == ContractClause::Mode::Assume)
            continue;
        Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_UNPROVABLE,
                              "invariants inside a uow are not checked yet",
                              invariant.condition ? invariant.condition->location : uow.name.location);
        diagnostic.withHelp("declare it 'in mode assume'");
        fail(diagnostic);
    }

    UnitOfWork unit;
    unit.name = uow.name.token_name;
    unit.subject = subject;
    unit.retry = retry == SemanticQualifier::Allowed ? RetryPolicy::Allowed : RetryPolicy::Disabled;
    unit.strategy = semantic_strategy_name(strategy);
    for (const auto& member : attached)
        unit.members.push_back(member_name(member));
    unit.claims.subject = subject;
    unit.claims.reads.assign(declared_reads.begin(), declared_reads.end());
    unit.claims.writes.assign(declared_writes.begin(), declared_writes.end());
    for (const auto& effect : declared_effects)
        unit.claims.effects.push_back(semantic_effect_name(effect));
    result.ir.unitsOfWork.push_back(std::move(unit));

    for (const auto& [key, ascending] : orderings)
    {
        result.ir.ordering.add_edge(subject, key, ascending);
        const auto [it, inserted] = ordering_keys.try_emplace(key, std::make_pair(subject, ascending));
        if (!inserted && it->second.second != ascending)
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_ORDERING_CYCLE,
                                  "uows '" + it->second.first + "' and '" + subject + "' order '" + key +
                                      "' in opposite directions",
                                  uow.name.location);
            diagnostic.withLabel("potential circular wait")
                      .withHelp("use the same direction for the same key in every uow");
            fail(diagnostic);
        }
    }

    for (const auto& nested : uow.nested)
        verify_uow(result, *nested, members, globalScope, ordering_keys);

    if (trace)
    {
        LOG_INFO("[verification] uow @ %s checked (%zu member(s), retry=%s, strategy=%s)",
                 subject.c_str(), attached.size(), semantic_qualifier_name(retry),
                 semantic_strategy_name(strategy));
        for (const auto& member : attached)
            LOG_INFO("[verification] uow member @ %s (phase=%s)", member_name(member).c_str(),
                     uow_phase_name(member.phase));
    }
}
