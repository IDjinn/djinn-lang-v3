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
#include <set>
#include <string>
#include <vector>

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
    // functions) and the resolved callee names for transitive effects.
    struct BodySemantics
    {
        std::set<std::string> reads;
        std::set<std::string> writes;
        std::vector<std::string> calls;
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
