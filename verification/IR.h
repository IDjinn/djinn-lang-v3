//
// Verification IR: data model shared by all verification checks
// (VERIFICATION-SPEC.md §6.1, VERIFICATION-IDEAS.md §2). Types only —
// no checks live here.
//

#ifndef DJINN_VERIFICATION_IR_H
#define DJINN_VERIFICATION_IR_H

#include <string>
#include <utility>
#include <vector>

namespace djinn::verification
{
    enum class ObligationMode
    {
        Check,
        Prove,
        Assume,
        Ignore
    };

    // `unverified` exists only mid-compile: a finished report never carries it.
    enum class ObligationStatus
    {
        Unverified,
        Proven,
        Checked,
        Assumed
    };

    enum class ObligationKind
    {
        Require,
        Ensure,
        Invariant,
        EffectClaim,
        AccessClaim,
        AtomicityClaim,
        OrderingClaim,
        RetryClaim,
        StrategyRule
    };

    // One checkable claim: a declared contract clause or semantic section
    // with its current discharge status.
    struct Obligation
    {
        ObligationKind kind = ObligationKind::Require;
        ObligationMode mode = ObligationMode::Check;
        ObligationStatus status = ObligationStatus::Unverified;
        std::string subject;
        std::string description;
        std::string justification;
    };

    // Declared sets from semantic sections. Names are resource keys
    // ("from.balance"); effects use the spec §11 vocabulary.
    struct ClaimSet
    {
        std::string subject;
        std::vector<std::string> reads;
        std::vector<std::string> writes;
        std::vector<std::string> effects;
        std::vector<std::string> deniedEffects;
    };

    struct OrderingEdge
    {
        std::string subject;
        std::string resourceKey;
        bool ascending = true;
    };

    struct OrderingGraph
    {
        std::vector<OrderingEdge> edges;

        void add_edge(std::string subject, std::string key, const bool ascending)
        {
            edges.push_back({std::move(subject), std::move(key), ascending});
        }
    };

    enum class RetryPolicy
    {
        Disabled,
        Allowed
    };

    struct StrategyContext
    {
        std::string name;
        std::vector<std::string> assumedGuarantees;
        std::vector<std::string> enforcedRules;
    };

    // UOW lifecycle states (spec §10); v1 models them statically only.
    enum class LifecycleState
    {
        Open,
        Active,
        Validating,
        Committing,
        Committed,
        AfterCommit,
        Aborted
    };

    struct UnitOfWork
    {
        std::string name;
        std::string subject;
        RetryPolicy retry = RetryPolicy::Disabled;
        std::string strategy;
        // Functions attached with `uow (Name)`, in subject spelling
        // ("transferMoney", "Account::withdraw").
        std::vector<std::string> members;
        ClaimSet claims;
        std::vector<Obligation> obligations;
        OrderingGraph ordering;
    };

    // Program-wide verification state produced by one run of the pass.
    struct VerificationIR
    {
        std::vector<ClaimSet> claims;
        std::vector<Obligation> obligations;
        OrderingGraph ordering;
        std::vector<UnitOfWork> unitsOfWork;
        std::vector<StrategyContext> strategies;
    };
}

#endif //DJINN_VERIFICATION_IR_H
