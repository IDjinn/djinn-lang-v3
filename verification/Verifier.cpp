#include "Verifier.h"

#include <algorithm>
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

    // Shallow scan of a uow body: dotted resource paths and call sites.
    // Statements outside these shapes contribute nothing (documented MVP
    // limitation — the uow is never executed, only checked).
    struct UowScan
    {
        BodySemantics paths;
        std::vector<const FunctionCall*> call_nodes;
        // First source site of each resource path / call name, so diagnostics
        // point at the offending line instead of the uow header.
        std::map<std::string, SourceLocation> write_sites;
        std::map<std::string, SourceLocation> read_sites;
        std::map<std::string, SourceLocation> call_sites;
    };

    void uow_scan_expression(const Expression& expression, UowScan& scan)
    {
        if (const auto* call = dynamic_cast<const FunctionCall*>(&expression))
        {
            // uow bodies are never bound, so unresolved callees fall back to
            // their written name (the builtin effect table still applies).
            const std::string name = call->resolvedCalleeStruct.empty()
                                         ? (call->resolvedCalleeName.empty() ? call->name.token_name
                                                                             : call->resolvedCalleeName)
                                         : call->resolvedCalleeStruct + "::" + call->resolvedCalleeName;
            scan.paths.calls.push_back(name);
            scan.call_sites.emplace(name, call->name.location);
            scan.call_nodes.push_back(call);
            for (const auto& argument : call->arguments)
                uow_scan_expression(*argument, scan);
            if (call->receiver) uow_scan_expression(*call->receiver, scan);
            return;
        }

        if (const auto* field_access = dynamic_cast<const FieldAccess*>(&expression))
        {
            if (const auto* object = dynamic_cast<const Identifier*>(field_access->object.get()))
            {
                const std::string path =
                    normalize_path(object->name() + "." + field_access->fieldName.token_name);
                scan.paths.reads.insert(path);
                scan.read_sites.emplace(path, field_access->location);
            }
            uow_scan_expression(*field_access->object, scan);
            return;
        }

        if (const auto* field_assignment = dynamic_cast<const FieldAssignment*>(&expression))
        {
            if (const auto* object = dynamic_cast<const Identifier*>(field_assignment->object.get()))
            {
                const std::string path =
                    normalize_path(object->name() + "." + field_assignment->fieldName.token_name);
                scan.paths.writes.insert(path);
                scan.write_sites.emplace(path, field_assignment->location);
            }
            uow_scan_expression(*field_assignment->object, scan);
            uow_scan_expression(*field_assignment->value, scan);
            return;
        }

        if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expression))
        {
            uow_scan_expression(*binary->left, scan);
            uow_scan_expression(*binary->right, scan);
            return;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expression))
        {
            uow_scan_expression(*unary->operand, scan);
            return;
        }

        if (const auto* ternary = dynamic_cast<const TernaryExpression*>(&expression))
        {
            uow_scan_expression(*ternary->condition, scan);
            uow_scan_expression(*ternary->trueExpr, scan);
            uow_scan_expression(*ternary->falseExpr, scan);
            return;
        }

        if (const auto* assignment = dynamic_cast<const Assignment*>(&expression))
        {
            uow_scan_expression(*assignment->value, scan);
            return;
        }

        if (const auto* init = dynamic_cast<const VariableInit*>(&expression))
        {
            uow_scan_expression(*init->value, scan);
            return;
        }

        if (const auto* index_access = dynamic_cast<const IndexAccess*>(&expression))
        {
            uow_scan_expression(*index_access->object, scan);
            uow_scan_expression(*index_access->index, scan);
            return;
        }

        if (const auto* try_expression = dynamic_cast<const TryExpression*>(&expression))
        {
            uow_scan_expression(*try_expression->expr, scan);
            uow_scan_expression(*try_expression->fallback, scan);
            return;
        }
    }

    void uow_scan_block(const Block& block, UowScan& scan)
    {
        for (const auto& statement : block.statements)
        {
            if (!statement) continue;
            if (const auto* expression_statement = dynamic_cast<const ExpressionStatement*>(statement.get()))
            {
                uow_scan_expression(*expression_statement->expression, scan);
                continue;
            }
            if (const auto* nested = dynamic_cast<const Block*>(statement.get()))
            {
                uow_scan_block(*nested, scan);
                continue;
            }
            if (const auto* if_statement = dynamic_cast<const IfStatement*>(statement.get()))
            {
                if (if_statement->condition) uow_scan_expression(*if_statement->condition, scan);
                if (if_statement->thenBranch) uow_scan_block(*if_statement->thenBranch, scan);
                if (if_statement->elseBranch) uow_scan_block(*if_statement->elseBranch, scan);
                continue;
            }
            if (const auto* return_statement = dynamic_cast<const ReturnStatement*>(statement.get()))
            {
                if (return_statement->value) uow_scan_expression(*return_statement->value, scan);
                continue;
            }
            if (const auto* while_statement = dynamic_cast<const WhileStatement*>(statement.get()))
            {
                if (while_statement->condition) uow_scan_expression(*while_statement->condition, scan);
                if (while_statement->body) uow_scan_block(*while_statement->body, scan);
                continue;
            }
            if (const auto* try_catch = dynamic_cast<const TryCatchStatement*>(statement.get()))
            {
                if (try_catch->tryBlock) uow_scan_block(*try_catch->tryBlock, scan);
                for (const auto& clause : try_catch->catches)
                    if (clause.body) uow_scan_block(*clause.body, scan);
                if (try_catch->finallyBlock) uow_scan_block(*try_catch->finallyBlock, scan);
                continue;
            }
        }
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

    // One subject per function/method with a body the pass can analyze.
    // Imports register aliases to the same symbol; collect each symbol once.
    struct Subject
    {
        const Symbol* key;
        const FunctionSymbol* function;
        const MethodSymbol* method;
        std::vector<size_t> obligation_rows;
    };
    std::vector<Subject> subjects;
    std::set<const Symbol*> seen;
    size_t symbols = 0;

    for (const auto& symbol : globalScope->symbols() | std::views::values)
    {
        if (!symbol || !seen.insert(symbol.get()).second)
            continue;

        if (const auto fn = std::dynamic_pointer_cast<FunctionSymbol>(symbol))
        {
            if (fn->kind == SymbolKind::ExternFunction)
                continue;
            symbols++;
            const std::vector<size_t> rows = collect_obligations(result.ir, trace, fn->name,
                                                                 fn->contracts, fn->paramTypes,
                                                                 fn->paramNames);
            if (fn->kind == SymbolKind::Function && fn->hasBody() && !fn->isFromLibrary)
                subjects.push_back({fn.get(), fn.get(), nullptr, rows});
        }
        else if (const auto strct = std::dynamic_pointer_cast<StructSymbol>(symbol))
        {
            for (const auto& method : strct->methods)
            {
                if (!method || !seen.insert(method.get()).second)
                    continue;
                symbols++;
                const std::string subject = method->structName + "::" + method->name;
                const std::vector<size_t> rows = collect_obligations(result.ir, trace, subject,
                                                                     method->contracts,
                                                                     method->paramTypes,
                                                                     method->paramNames);
                if (method->hasBody() && !method->isAbstract && !method->isFromLibrary)
                    subjects.push_back({method.get(), nullptr, method.get(), rows});
            }
        }
    }

    const auto seeds_of = [&globalScope](const Subject& subject) -> std::vector<const ContractClause*>
    {
        if (!subject.method || !globalScope) return {};
        if (const auto strct = globalScope->lookupStruct(subject.method->structName))
            return strct->invariants;
        return {};
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
        return subject.function ? subject.function->contracts : subject.method->contracts;
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

    // Static uow checks (§9/§10) with the program-wide ordering graph (§7.3).
    std::map<std::string, std::pair<std::string, bool>> ordering_keys;
    for (const auto& program : programs)
    {
        for (const auto& uow : program->uows)
            verify_uow(result, *uow, globalScope, ordering_keys);
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

        const auto condition = translate_condition(*clause->condition);
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

        const auto condition = translate_condition(*clause->condition);
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
    if (reads_section)
    {
        std::set<std::string> hidden;
        for (const auto& read : semantics.reads)
            if (!declared_reads.contains(read)) hidden.insert(read);
        if (!hidden.empty())
        {
            fail_claim("'" + subject + "' reads undeclared resource(s): " + join_set(hidden),
                       declared_reads.empty() ? "(none)" : join_set(declared_reads),
                       semantics.reads.empty() ? "(none)" : join_set(semantics.reads),
                       "add them to the reads section", reads_section->location);
        }
    }
    if (writes_section)
    {
        std::set<std::string> missing, extra;
        for (const auto& write : semantics.writes)
            if (!declared_writes.contains(write)) missing.insert(write);
        for (const auto& write : declared_writes)
            if (!semantics.writes.contains(write)) extra.insert(write);
        if (!missing.empty())
        {
            fail_claim("'" + subject + "' writes " + join_set(missing) +
                           ", which the writes section does not list",
                       declared_writes.empty() ? "(none)" : join_set(declared_writes),
                       semantics.writes.empty() ? "(none)" : join_set(semantics.writes),
                       "add them to the writes section", writes_section->location);
        }
        if (!extra.empty())
        {
            fail_claim("the writes section of '" + subject + "' lists " + join_set(extra) +
                           ", but the body never writes it",
                       join_set(declared_writes),
                       semantics.writes.empty() ? "(none)" : join_set(semantics.writes),
                       "remove it from the writes section", writes_section->location);
        }
    }

    ClaimSet claim;
    claim.subject = subject;
    claim.reads.assign(semantics.reads.begin(), semantics.reads.end());
    claim.writes.assign(semantics.writes.begin(), semantics.writes.end());
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

void Verifier::apply_callee_to_uow(const FunctionCall& call, BodySemantics& paths,
                                   std::set<SemanticEffect>& effects,
                                   const std::shared_ptr<ScopedSymbolTable>& globalScope,
                                   std::map<std::string, SourceLocation>* write_sites) const
{
    const CalleeView callee = resolve_callee(call, globalScope);
    if (!callee.symbol) return;

    // Substitute the callee's resource paths by plain-identifier arguments:
    // debit(acct.balance) contributing "param.balance" becomes "from.balance".
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
    const auto rewrite = [&binding](const std::string& path)
    {
        const auto dot = path.find('.');
        if (dot == std::string::npos) return path;
        const auto it = binding.find(path.substr(0, dot));
        if (it == binding.end()) return path;
        return it->second + path.substr(dot);
    };

    if (const auto it = _semantics.find(callee.symbol); it != _semantics.end())
    {
        for (const auto& path : it->second.writes)
        {
            const std::string rewritten = normalize_path(rewrite(path));
            paths.writes.insert(rewritten);
            if (write_sites) write_sites->emplace(rewritten, call.name.location);
        }
        for (const auto& path : it->second.reads)
            paths.reads.insert(normalize_path(rewrite(path)));
    }
    if (const auto it = _effect_sets.find(callee.symbol); it != _effect_sets.end())
        effects.insert(it->second.begin(), it->second.end());
}

void Verifier::verify_uow(VerificationResult& result, const UowDeclaration& uow,
                          const std::shared_ptr<ScopedSymbolTable>& globalScope,
                          std::map<std::string, std::pair<std::string, bool>>& ordering_keys)
{
    const bool trace = _mode == VerificationMode::Trace;
    const std::string subject = "uow " + uow.name.token_name;

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

    UowScan body_scan, before_scan, after_scan;
    if (uow.body) uow_scan_block(*uow.body, body_scan);
    if (uow.beforeCommit) uow_scan_block(*uow.beforeCommit, before_scan);
    if (uow.afterCommit) uow_scan_block(*uow.afterCommit, after_scan);

    const auto enrich = [&](UowScan& scan, std::set<SemanticEffect>& effects,
                            std::map<SemanticEffect, SourceLocation>& effect_sites)
    {
        for (const auto* call : scan.call_nodes)
            apply_callee_to_uow(*call, scan.paths, effects, globalScope, &scan.write_sites);
        for (const auto& callee : scan.paths.calls)
        {
            std::set<SemanticEffect> contributed;
            if (callee.find("::") == std::string::npos)
            {
                if (const auto* builtin = builtin_effects(callee))
                    contributed = *builtin;
            }
            if (const Symbol* callee_symbol = resolve_callee_by_name(callee, globalScope))
            {
                if (const auto it = _effect_sets.find(callee_symbol); it != _effect_sets.end())
                    contributed.insert(it->second.begin(), it->second.end());
            }
            const auto site = scan.call_sites.find(callee);
            for (const auto& effect : contributed)
            {
                effects.insert(effect);
                if (site != scan.call_sites.end())
                    effect_sites.emplace(effect, site->second);
            }
        }
    };

    std::set<SemanticEffect> body_effects, before_effects, after_effects;
    std::map<SemanticEffect, SourceLocation> body_effect_sites, before_effect_sites, after_effect_sites;
    enrich(body_scan, body_effects, body_effect_sites);
    enrich(before_scan, before_effects, before_effect_sites);
    enrich(after_scan, after_effects, after_effect_sites);

    const auto site_of = [](const auto& sites, const auto& key, const SourceLocation& fallback)
        -> SourceLocation
    {
        const auto it = sites.find(key);
        return it != sites.end() ? it->second : fallback;
    };

    // §6.2 claim-vs-inference: inferred ⊆ declared for reads/effects,
    // inferred == declared for writes.
    if (!declared_writes.empty())
    {
        std::set<std::string> missing, extra;
        for (const auto& write : body_scan.paths.writes)
            if (!declared_writes.contains(write)) missing.insert(write);
        for (const auto& write : declared_writes)
            if (!body_scan.paths.writes.contains(write)) extra.insert(write);
        if (!missing.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "write to '" + *missing.begin() + "' is not declared in uow '" +
                                      uow.name.token_name + "'",
                                  site_of(body_scan.write_sites, *missing.begin(), uow.name.location));
            diagnostic.withNote("declared at " + span(writes_location) + ": " + join_set(declared_writes))
                      .withHelp("add it to the writes section");
            fail(diagnostic);
        }
        if (!extra.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "the writes section lists '" + *extra.begin() +
                                      "', but uow '" + uow.name.token_name + "' never writes it",
                                  writes_location);
            diagnostic.withNote("declared at " + span(writes_location) + ": " + join_set(declared_writes))
                      .withHelp("remove it from the writes section");
            fail(diagnostic);
        }
    }
    if (!declared_reads.empty())
    {
        std::set<std::string> all_reads = body_scan.paths.reads;
        all_reads.insert(before_scan.paths.reads.begin(), before_scan.paths.reads.end());
        std::set<std::string> hidden;
        for (const auto& read : all_reads)
            if (!declared_reads.contains(read)) hidden.insert(read);
        if (!hidden.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "read of '" + *hidden.begin() + "' is not declared in uow '" +
                                      uow.name.token_name + "'",
                                  site_of(body_scan.read_sites, *hidden.begin(),
                                          site_of(before_scan.read_sites, *hidden.begin(),
                                                  uow.name.location)));
            diagnostic.withNote("reads declared at " + span(reads_location))
                      .withHelp("add it to the reads section");
            fail(diagnostic);
        }
    }
    if (!declared_effects.empty())
    {
        std::set<SemanticEffect> all_effects = body_effects;
        all_effects.insert(before_effects.begin(), before_effects.end());
        all_effects.insert(after_effects.begin(), after_effects.end());
        std::set<SemanticEffect> hidden;
        for (const auto& effect : all_effects)
            if (!declared_effects.contains(effect)) hidden.insert(effect);
        if (!hidden.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  std::string("undeclared effect '") +
                                      semantic_effect_name(*hidden.begin()) + "' in uow '" +
                                      uow.name.token_name + "'",
                                  site_of(body_effect_sites, *hidden.begin(),
                                          site_of(before_effect_sites, *hidden.begin(),
                                                  site_of(after_effect_sites, *hidden.begin(),
                                                          uow.name.location))));
            diagnostic.withNote("effects declared at " + span(effects_location))
                      .withHelp("add it to the effects section");
            fail(diagnostic);
        }
    }
    // §10: the body realizes exactly the declared atomic set — no more, no less.
    if (!declared_atomic.empty())
    {
        std::set<std::string> missing, extra;
        for (const auto& write : body_scan.paths.writes)
            if (!declared_atomic.contains(write)) missing.insert(write);
        for (const auto& write : declared_atomic)
            if (!body_scan.paths.writes.contains(write)) extra.insert(write);
        if (!missing.empty())
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_CLAIM_INFERENCE_MISMATCH,
                                  "write to '" + *missing.begin() + "' is not atomic in uow '" +
                                      uow.name.token_name + "'",
                                  site_of(body_scan.write_sites, *missing.begin(), uow.name.location));
            diagnostic.withNote("atomic section at " + span(atomic_location) + ": " +
                                join_set(declared_atomic) + " | body writes: " +
                                (body_scan.paths.writes.empty() ? "(none)" : join_set(body_scan.paths.writes)))
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

    // §9 retry-safety: every effect in a retryable region must be idempotent.
    if (retry == SemanticQualifier::Allowed &&
        (body_effects.contains(SemanticEffect::ExternalIO) ||
         before_effects.contains(SemanticEffect::ExternalIO)))
    {
        const SourceLocation site = body_effects.contains(SemanticEffect::ExternalIO)
                                        ? site_of(body_effect_sites, SemanticEffect::ExternalIO,
                                                  uow.name.location)
                                        : site_of(before_effect_sites, SemanticEffect::ExternalIO,
                                                  uow.name.location);
        Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_RETRY_UNSAFE_EFFECT,
                              "impure call inside a retryable uow", site);
        diagnostic.withNote("external_io may run twice when the uow retries")
                  .withHelp("move it to after_commit, or set retry: disabled");
        fail(diagnostic);
    }

    // §8/§11 effect placement: under strategy database, external_io belongs
    // in after_commit — it runs exactly once, after durability.
    if (strategy == SemanticStrategy::Database)
    {
        if (before_effects.contains(SemanticEffect::ExternalIO))
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_EFFECT_PLACEMENT,
                                  "impure call in before_commit",
                                  site_of(before_effect_sites, SemanticEffect::ExternalIO,
                                          uow.name.location));
            diagnostic.withHelp("under strategy database, move it to after_commit");
            fail(diagnostic);
        }
        if (body_effects.contains(SemanticEffect::ExternalIO) &&
            retry != SemanticQualifier::Allowed)
        {
            Diagnostic diagnostic(Severity::Error, DiagnosticCode::E_CONTRACT_EFFECT_PLACEMENT,
                                  "impure call inside the uow body",
                                  site_of(body_effect_sites, SemanticEffect::ExternalIO,
                                          uow.name.location));
            diagnostic.withHelp("under strategy database, move it to after_commit — "
                                "it runs exactly once, after the commit");
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

    // Contracts on the uow scope: nothing executes it in this phase, so they
    // are recorded as assumed obligations for the audit trail.
    for (const auto& clause : uow.contracts)
    {
        if (clause.mode == ContractClause::Mode::Ignore) continue;
        Obligation obligation;
        obligation.kind = clause.isEnsure() ? ObligationKind::Ensure : ObligationKind::Require;
        obligation.mode = obligation_mode(clause.mode);
        obligation.status = ObligationStatus::Assumed;
        obligation.subject = subject;
        obligation.description = clause.isEnsure() ? "ensure" : "require";
        obligation.justification = "uow scope: static verification only in this phase";
        if (trace)
        {
            LOG_INFO("[verification] %s @ %s (mode=%s, status=%s)", obligation.description.c_str(),
                     subject.c_str(), mode_name(obligation.mode), status_name(obligation.status));
        }
        result.ir.obligations.push_back(std::move(obligation));
    }

    UnitOfWork unit;
    unit.name = uow.name.token_name;
    unit.subject = subject;
    unit.retry = retry == SemanticQualifier::Allowed ? RetryPolicy::Allowed : RetryPolicy::Disabled;
    unit.strategy = semantic_strategy_name(strategy);
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
        verify_uow(result, *nested, globalScope, ordering_keys);

    if (trace)
    {
        LOG_INFO("[verification] uow @ %s checked (retry=%s, strategy=%s)", subject.c_str(),
                 semantic_qualifier_name(retry),
                 semantic_strategy_name(strategy));
    }
}
