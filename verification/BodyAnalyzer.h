//
// Body inference for the verification pass (VERIFICATION-SPEC.md §6.2):
// walks a function body and derives per-return-path facts plus the affine
// value of every returned expression. Control flow is handled with branch
// fact joins; loops are conservative — variables assigned anywhere inside a
// loop are treated as Unknown on every path through or after it. The same
// walk optionally collects a coarse behavior summary (resource reads/writes
// and call sites) used by the semantic claim checks.
//

#ifndef DJINN_VERIFICATION_BODY_ANALYZER_H
#define DJINN_VERIFICATION_BODY_ANALYZER_H

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../diagnostics/Diagnostic.h"
#include "Facts.h"

struct FunctionCall;
struct FunctionSymbol;
struct MethodSymbol;
struct ContractClause;

namespace djinn::verification
{
    // One `return e` execution: the facts holding on that path and the
    // affine value of e (nullopt = outside fragment / unknown).
    struct ReturnPath
    {
        FactSet facts;
        std::optional<Affine> value;
    };

    struct BodySummary
    {
        std::vector<ReturnPath> returns;
    };

    // Coarse behavior collected alongside the fact walk: dotted resource
    // paths ("this.balance" for method fields, "from.balance" for free
    // functions) and the resolved callee names for transitive effects. The
    // site maps keep the first occurrence of each path/callee so claim
    // diagnostics can point at the offending line.
    //
    // Lock evidence: writes made while at least one `lock` scope is open are
    // attributed to the union of the enclosing lock roots; call sites record
    // the same so callee path expansion can carry the evidence over. A path
    // with a lock-free occurrence lands in the unlocked sets — an atomic claim
    // on it is not discharged.
    struct BodySemantics
    {
        std::set<std::string> reads;
        std::set<std::string> writes;
        std::vector<std::string> calls;
        // Call nodes kept for callee path expansion in the uow member checks.
        std::vector<const FunctionCall*> call_nodes;
        std::map<std::string, SourceLocation> read_sites;
        std::map<std::string, SourceLocation> write_sites;
        std::map<std::string, SourceLocation> call_sites;
        // Lock evidence per path/callee (union of enclosing lock roots).
        std::map<std::string, std::set<std::string>> write_lock_roots;
        std::map<std::string, std::set<std::string>> call_lock_roots;
        std::set<std::string> unlocked_writes;
        std::set<std::string> unlocked_calls;
        // Inline uow lifecycle blocks, walked separately so claims only see
        // the member's transaction-phase behavior. Keyed by UowPhase; nested
        // phase blocks fold into their enclosing phase.
        std::map<int, std::shared_ptr<BodySemantics>> phase_blocks;
        // Path lists from listed `rollback (a.b, ...);` statements in the body.
        std::vector<std::vector<std::string>> rollback_paths;
    };

    // Invoked exactly once per FunctionCall in the body with the facts
    // holding at the call — the verifier's hook checks the callee's
    // `require` clauses here (it may emit diagnostics).
    using CallSiteVisitor = std::function<void(const FunctionCall&, const FactSet&)>;

    // Pure query for a callee's proven postcondition over its result, in
    // caller terms; nullopt = no interprocedural knowledge. May be invoked
    // repeatedly for the same call.
    using PostconditionQuery = std::function<std::optional<Affine>(const FunctionCall&)>;

    // seed_contracts are assumed at body entry like requires (used for
    // entity invariants on methods).
    BodySummary analyze_function_body(const FunctionSymbol& function, const CallSiteVisitor& on_call = {},
                                      const PostconditionQuery& postcondition = {},
                                      BodySemantics* semantics = nullptr,
                                      const std::vector<const ContractClause*>& seed_contracts = {});
    BodySummary analyze_method_body(const MethodSymbol& method, const CallSiteVisitor& on_call = {},
                                    const PostconditionQuery& postcondition = {},
                                    BodySemantics* semantics = nullptr,
                                    const std::vector<const ContractClause*>& seed_contracts = {});
}

#endif //DJINN_VERIFICATION_BODY_ANALYZER_H
