#ifndef DJINN_VERIFIER_H
#define DJINN_VERIFIER_H

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "BodyAnalyzer.h"
#include "Fragment.h"
#include "IR.h"
#include "VerificationMode.h"
#include "../diagnostics/Diagnostic.h"
#include "../parser/ast/Declaration.h"

struct Program;
struct Symbol;
struct FunctionSymbol;
struct MethodSymbol;
struct StructSymbol;
struct UowDeclaration;
struct FunctionCall;
struct ContractClause;
struct SemanticSection;
class ScopedSymbolTable;

namespace djinn::verification
{
    // The caller-visible postcondition a function's proven `ensure` clauses
    // license: an affine expression over the callee's parameters that every
    // return path establishes. Functions whose postconditions are conditional
    // or reach outside their parameters do not propagate.
    struct PostconditionSummary
    {
        bool propagated = false;
        Affine value;
    };
}

struct VerificationResult
{
    bool success = true;
    djinn::verification::VerificationIR ir;
};

// Semantic verification pass (VERIFICATION-SPEC.md §6): sits between binder
// and generator, consumes declared claims from the symbol table and infers
// from the AST bodies. Never emits code — `mode check` injection stays in
// the generator. The pass verifies the whole flow:
//
//   - `ensure` clauses against inferred return paths (E-CONTRACT-011),
//   - `require` clauses at every call site, free or method (E-CONTRACT-007),
//   - interprocedural postcondition propagation over call chains,
//   - semantic sections (reads/writes/effects) against body inference
//     (E-CONTRACT-011), uow static checks (atomic coverage, retry safety,
//     effect placement, ordering conflicts — E-CONTRACT-042/043/045),
//   - entity invariants at method exits (E-CONTRACT-044),
//   - obligation modes prove/assume/ignore (E-CONTRACT-046 on unprovable).
//
// Provable violations are hard errors; undecidable checks stay runtime
// (status `checked`).
class Verifier
{
public:
    explicit Verifier(DiagnosticEngine& diagnostics, VerificationMode mode = VerificationMode::Off);

    VerificationResult run(const std::vector<std::shared_ptr<Program>>& programs,
                           const std::shared_ptr<ScopedSymbolTable>& globalScope);

private:
    // Uniform view over the two callee shapes the pass can check call sites
    // against: free functions and struct methods.
    struct CalleeView
    {
        const Symbol* symbol = nullptr;
        const std::vector<const ContractClause*>* contracts = nullptr;
        const std::vector<std::string>* paramNames = nullptr;
        std::string displayName;
    };

    [[nodiscard]] CalleeView resolve_callee(const FunctionCall& call,
                                            const std::shared_ptr<ScopedSymbolTable>& globalScope) const;

    void check_call_site(VerificationResult& result, const FunctionCall& call,
                         const djinn::verification::FactSet& facts,
                         const std::shared_ptr<ScopedSymbolTable>& globalScope, bool emit);

    [[nodiscard]] std::optional<djinn::verification::Affine> call_postcondition(
        const FunctionCall& call, const std::shared_ptr<ScopedSymbolTable>& globalScope) const;

    void derive_postcondition(const Symbol& subject, const std::vector<const ContractClause*>& contracts,
                              const std::vector<std::string>& paramNames,
                              const djinn::verification::BodySummary& body);

    void diff_contracts(VerificationResult& result,
                        const std::vector<const ContractClause*>& contracts,
                        const std::vector<size_t>& obligation_rows, const std::string& subject,
                        const djinn::verification::BodySummary& body);

    // Claim-vs-inference over declared semantic sections (VERIFICATION-SPEC.md §6.2).
    void diff_sections(VerificationResult& result, const std::vector<const SemanticSection*>& sections,
                       const djinn::verification::BodySemantics& semantics,
                       const std::set<SemanticEffect>& effects,
                       const std::string& subject);

    // Entity invariants at every return path of the methods that write them (§10).
    void check_invariants(VerificationResult& result, const MethodSymbol& method,
                          const StructSymbol& strct, const djinn::verification::BodySummary& body);

    // Static uow checks (§9/§10) plus the program-wide ordering graph (§7.3).
    void verify_uow(VerificationResult& result, const UowDeclaration& uow,
                    const std::shared_ptr<ScopedSymbolTable>& globalScope,
                    std::map<std::string, std::pair<std::string, bool>>& ordering_keys);

    void apply_callee_to_uow(const FunctionCall& call,
                             djinn::verification::BodySemantics& paths,
                             std::set<SemanticEffect>& effects,
                             const std::shared_ptr<ScopedSymbolTable>& globalScope,
                             std::map<std::string, SourceLocation>* write_sites = nullptr) const;

    DiagnosticEngine& _diagnostics;
    VerificationMode _mode;
    std::unordered_map<const Symbol*, djinn::verification::PostconditionSummary> _postconditions;
    std::unordered_map<const Symbol*, djinn::verification::BodySemantics> _semantics;
    std::unordered_map<const Symbol*, std::set<SemanticEffect>> _effect_sets;
    // Call sites whose require was decided false: the callee never returns
    // normally, so its postcondition must not seed any facts.
    std::set<const FunctionCall*> _failed_calls;
};

#endif //DJINN_VERIFIER_H
