//
// FactSet: the abstract state of one program point during body inference —
// affine equalities (assignments), integer intervals (branch conditions and
// requires) and relational facts that span several variables. Decision
// procedure: normalize the queried difference, substitute equalities to a
// fixpoint, try the constant and interval routes, then exact relational
// matches. Everything else is Unknown (never guessed).
//

#ifndef DJINN_VERIFICATION_FACTS_H
#define DJINN_VERIFICATION_FACTS_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Fragment.h"

namespace djinn::verification
{
    struct Interval
    {
        std::optional<int64_t> lo;
        std::optional<int64_t> hi;

        [[nodiscard]] bool is_empty() const
        {
            return lo.has_value() && hi.has_value() && *lo > *hi;
        }
    };

    class FactSet
    {
    public:
        // var == value; self-referential updates degrade to interval shifts
        // where possible, otherwise to Unknown.
        void set_equality(const std::string& var, const Affine& value);
        void kill(const std::string& var);

        // Strengthen the state with a condition known to hold (or not hold,
        // when truth == false, for else branches).
        void assume(const Condition& condition, bool truth = true);

        [[nodiscard]] TriBool decide(const Condition& condition) const;

        // Merge two branch outcomes: equalities/relations must agree on both
        // sides, intervals intersect.
        void join(const FactSet& other);

        [[nodiscard]] const std::map<std::string, Affine>& equalities() const { return _equalities; }

        // Constant value of a variable if fully determined (diagnostics evidence).
        [[nodiscard]] std::optional<int64_t> constant_of(const std::string& var) const;

    private:
        std::map<std::string, Affine> _equalities;
        std::map<std::string, Interval> _intervals;
        // Canonical positive-form relations (diff vs zero); scanned linearly
        // so kills can filter by variable.
        std::vector<std::pair<Affine, TokenType>> _relations;

        [[nodiscard]] Affine substitute_all(const Affine& affine) const;
        void rename_variable(const std::string& from, const std::string& to);
        void assume_comparison(Affine left, TokenType op, Affine right);
        [[nodiscard]] TriBool decide_comparison(Affine left, TokenType op, Affine right) const;
        [[nodiscard]] std::optional<Interval> interval_of(const Affine& affine) const;
        void record_relation(Affine difference, TokenType op);
        void tighten(const std::string& var, TokenType op, int64_t bound);
    };
}

#endif //DJINN_VERIFICATION_FACTS_H
