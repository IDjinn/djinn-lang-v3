#include "Facts.h"

#include <algorithm>
#include <utility>

namespace djinn::verification
{
    namespace
    {
        bool checked_add(const int64_t a, const int64_t b, int64_t& out)
        {
            if (b > 0 && a > INT64_MAX - b) return false;
            if (b < 0 && a < INT64_MIN - b) return false;
            out = a + b;
            return true;
        }

        bool checked_mul(const int64_t a, const int64_t b, int64_t& out)
        {
            if (a == 0 || b == 0)
            {
                out = 0;
                return true;
            }
            const int64_t result = a * b;
            if (result / b != a) return false;
            out = result;
            return true;
        }

        bool affine_equal(const Affine& a, const Affine& b)
        {
            return a.constant == b.constant && a.coefficients == b.coefficients;
        }

        // Flip a comparison under negation: not (a op b) is a negate_op(op) b.
        TokenType negate_op(const TokenType op)
        {
            switch (op)
            {
                case TokenType::LESS: return TokenType::GREATER_EQUAL;
                case TokenType::LESS_EQUAL: return TokenType::GREATER;
                case TokenType::GREATER: return TokenType::LESS_EQUAL;
                case TokenType::GREATER_EQUAL: return TokenType::LESS;
                case TokenType::EQUAL_EQUAL: return TokenType::BANG_EQUAL;
                case TokenType::BANG_EQUAL: return TokenType::EQUAL_EQUAL;
                default: return op;
            }
        }

        // Canonical orientation: LESS/LESS_EQUAL swap the sides so every
        // stored relation is (diff, op) with op in the positive forms.
        TokenType canonical_op(const TokenType op, const bool swap_sides)
        {
            if (!swap_sides) return op;
            switch (op)
            {
                case TokenType::LESS: return TokenType::GREATER;
                case TokenType::LESS_EQUAL: return TokenType::GREATER_EQUAL;
                case TokenType::GREATER: return TokenType::LESS;
                case TokenType::GREATER_EQUAL: return TokenType::LESS_EQUAL;
                default: return op;
            }
        }

        // What a known fact (fact op 0) implies about a query (query op 0)
        // over the same difference. Both ops are canonical positive forms.
        TriBool imply(const TokenType fact, const TokenType query)
        {
            switch (fact)
            {
                case TokenType::GREATER:
                    switch (query)
                    {
                        case TokenType::GREATER:
                        case TokenType::BANG_EQUAL: return TriBool::True;
                        case TokenType::GREATER_EQUAL: return TriBool::True;
                        case TokenType::EQUAL_EQUAL:
                        case TokenType::LESS_EQUAL: return TriBool::False;
                        default: return TriBool::Unknown;
                    }
                case TokenType::GREATER_EQUAL:
                    if (query == TokenType::GREATER_EQUAL) return TriBool::True;
                    if (query == TokenType::LESS) return TriBool::False;
                    return TriBool::Unknown;
                case TokenType::EQUAL_EQUAL:
                    switch (query)
                    {
                        case TokenType::EQUAL_EQUAL:
                        case TokenType::GREATER_EQUAL:
                        case TokenType::LESS_EQUAL: return TriBool::True;
                        case TokenType::BANG_EQUAL:
                        case TokenType::GREATER:
                        case TokenType::LESS: return TriBool::False;
                        default: return TriBool::Unknown;
                    }
                case TokenType::BANG_EQUAL:
                    if (query == TokenType::BANG_EQUAL) return TriBool::True;
                    if (query == TokenType::EQUAL_EQUAL) return TriBool::False;
                    return TriBool::Unknown;
                default:
                    return TriBool::Unknown;
            }
        }

        // Same table mirrored: the fact is known about -diff.
        TokenType mirrored_op(const TokenType op)
        {
            switch (op)
            {
                case TokenType::GREATER: return TokenType::LESS;
                case TokenType::GREATER_EQUAL: return TokenType::LESS_EQUAL;
                case TokenType::LESS: return TokenType::GREATER;
                case TokenType::LESS_EQUAL: return TokenType::GREATER_EQUAL;
                default: return op;
            }
        }

        // Query (diff op 0) against the interval [lo, hi] of diff.
        TriBool decide_by_interval(const TokenType op, const Interval& interval)
        {
            const auto lo = interval.lo;
            const auto hi = interval.hi;

            switch (op)
            {
                case TokenType::GREATER:
                    if (lo && *lo >= 1) return TriBool::True;
                    if (hi && *hi <= 0) return TriBool::False;
                    break;
                case TokenType::GREATER_EQUAL:
                    if (lo && *lo >= 0) return TriBool::True;
                    if (hi && *hi <= -1) return TriBool::False;
                    break;
                case TokenType::EQUAL_EQUAL:
                    if (lo && hi && *lo == 0 && *hi == 0) return TriBool::True;
                    if ((lo && *lo > 0) || (hi && *hi < 0)) return TriBool::False;
                    break;
                case TokenType::BANG_EQUAL:
                    if ((lo && *lo > 0) || (hi && *hi < 0)) return TriBool::True;
                    if (lo && hi && *lo == 0 && *hi == 0) return TriBool::False;
                    break;
                default:
                    break;
            }
            return TriBool::Unknown;
        }
    }

    Affine FactSet::substitute_all(const Affine& affine) const
    {
        Affine current = affine;
        for (int steps = 0; steps < 16; steps++)
        {
            bool changed = false;
            for (const auto& [name, value] : _equalities)
            {
                const auto replaced = current.substituted(name, value);
                if (replaced && !affine_equal(*replaced, current))
                {
                    current = *replaced;
                    changed = true;
                }
            }
            if (!changed) break;
        }
        return current;
    }

    void FactSet::tighten(const std::string& var, const TokenType op, const int64_t bound)
    {
        Interval interval = _intervals[var];
        switch (op)
        {
            case TokenType::LESS:
                if (!interval.hi || bound - 1 < *interval.hi) interval.hi = bound - 1;
                break;
            case TokenType::LESS_EQUAL:
                if (!interval.hi || bound < *interval.hi) interval.hi = bound;
                break;
            case TokenType::GREATER:
                if (!interval.lo || bound + 1 > *interval.lo) interval.lo = bound + 1;
                break;
            case TokenType::GREATER_EQUAL:
                if (!interval.lo || bound > *interval.lo) interval.lo = bound;
                break;
            case TokenType::EQUAL_EQUAL:
                if (!interval.lo || bound > *interval.lo) interval.lo = bound;
                if (!interval.hi || bound < *interval.hi) interval.hi = bound;
                break;
            default:
                break;
        }
        if (!interval.is_empty()) _intervals[var] = interval;
        else _intervals.erase(var);
    }

    void FactSet::rename_variable(const std::string& from, const std::string& to)
    {
        const auto ghost = Affine::of_variable(to);

        if (auto it = _equalities.find(from); it != _equalities.end())
        {
            Affine value = it->second;
            if (const auto rewritten = value.substituted(from, ghost))
                value = *rewritten;
            _equalities.erase(it);
            _equalities[to] = std::move(value);
        }
        if (auto it = _intervals.find(from); it != _intervals.end())
        {
            const Interval interval = it->second;
            _intervals.erase(it);
            _intervals[to] = interval;
        }
        // Preserve the meaning of every fact that talks about the old name:
        // relations seeded by requires keep describing the pre-update value.
        for (auto it = _equalities.begin(); it != _equalities.end();)
        {
            if (it->first == to)
            {
                ++it;
                continue;
            }
            if (const auto rewritten = it->second.substituted(from, ghost))
            {
                it->second = *rewritten;
                ++it;
            }
            else
            {
                it = _equalities.erase(it);
            }
        }
        std::erase_if(_relations, [&from, &ghost](auto& relation)
        {
            const auto rewritten = relation.first.substituted(from, ghost);
            if (!rewritten) return true;
            relation.first = *rewritten;
            return false;
        });
    }

    void FactSet::set_equality(const std::string& var, const Affine& raw_value)
    {
        const Affine value = substitute_all(raw_value);

        // v = <expr mentioning v>: keep the old value under a ghost name so
        // facts about it (requires-seeded relations, intervals) still decide
        // queries over the new v, which is defined in terms of the ghost.
        if (value.coefficients.contains(var))
        {
            std::string ghost = "#" + var;
            while (_equalities.contains(ghost) || _intervals.contains(ghost))
                ghost += "#";
            rename_variable(var, ghost);
            const auto rewritten = value.substituted(var, Affine::of_variable(ghost));
            if (!rewritten)
            {
                kill(var);
                return;
            }
            set_equality(var, *rewritten);
            return;
        }

        _equalities[var] = value;

        // Propagate the new equality through the other equalities; drop
        // relations that mention the variable (conservative).
        for (auto it = _equalities.begin(); it != _equalities.end();)
        {
            if (it->first == var)
            {
                ++it;
                continue;
            }
            const auto replaced = it->second.substituted(var, value);
            if (replaced)
            {
                it->second = *replaced;
                ++it;
            }
            else
            {
                it = _equalities.erase(it);
            }
        }

        for (auto it = _relations.begin(); it != _relations.end();)
        {
            if (it->first.coefficients.contains(var))
                it = _relations.erase(it);
            else
                ++it;
        }

        // Refresh the variable's interval from the new definition.
        _intervals.erase(var);
        if (const auto constant = value.as_constant())
        {
            _intervals[var] = {*constant, *constant};
        }
        else if (const auto derived = interval_of(value))
        {
            _intervals[var] = *derived;
        }
    }

    void FactSet::kill(const std::string& var)
    {
        _equalities.erase(var);
        _intervals.erase(var);
        std::erase_if(_relations, [&](const auto& relation)
        {
            return relation.first.coefficients.contains(var);
        });
    }

    void FactSet::record_relation(Affine difference, const TokenType op)
    {
        if (difference.is_constant()) return;
        // Drop exact duplicates.
        std::erase_if(_relations, [&](const auto& existing)
        {
            return affine_equal(existing.first, difference) && existing.second == op;
        });
        _relations.emplace_back(std::move(difference), op);
    }

    void FactSet::assume_comparison(Affine left, const TokenType op, Affine right)
    {
        left = substitute_all(left);
        right = substitute_all(right);

        // Equality between a variable and an affine value becomes an equality
        // fact (the set_equality route also refreshes intervals).
        if (op == TokenType::EQUAL_EQUAL)
        {
            std::string name;
            if (left.is_single_variable(&name)) set_equality(name, right);
            else if (right.is_single_variable(&name)) set_equality(name, left);
        }

        // Tighten the interval when the difference reduces to one variable.
        if (const auto difference = left.plus(right, -1))
        {
            const Affine diff = substitute_all(*difference);
            if (diff.coefficients.size() == 1)
            {
                const auto& [name, coefficient] = *diff.coefficients.begin();
                if (coefficient == 1)
                {
                    // v + c op 0  is  v op -c
                    tighten(name, op, -diff.constant);
                }
                else if (coefficient == -1)
                {
                    // -v + c op 0  is  v (mirror of op) c
                    tighten(name, canonical_op(op, true), diff.constant);
                }
            }

            // Canonicalize once for the relation store: a < b  is  b - a > 0.
            const bool swap = op == TokenType::LESS || op == TokenType::LESS_EQUAL;
            std::optional<Affine> oriented;
            if (swap) oriented = diff.negated();
            else oriented = diff;
            if (oriented) record_relation(*oriented, canonical_op(op, swap));
        }
    }

    TriBool FactSet::decide_comparison(Affine left, const TokenType op, Affine right) const
    {
        const auto difference = left.plus(right, -1);
        if (!difference) return TriBool::Unknown;
        const Affine diff = substitute_all(*difference);
        const bool swap = op == TokenType::LESS || op == TokenType::LESS_EQUAL;
        std::optional<Affine> oriented;
        if (swap) oriented = diff.negated();
        else oriented = diff;
        if (!oriented) return TriBool::Unknown;
        const Affine canonical = *oriented;
        const TokenType canonical_compare = canonical_op(op, swap);

        if (const auto constant = canonical.as_constant())
        {
            const int64_t c = *constant;
            switch (canonical_compare)
            {
                case TokenType::GREATER: return c > 0 ? TriBool::True : TriBool::False;
                case TokenType::GREATER_EQUAL: return c >= 0 ? TriBool::True : TriBool::False;
                case TokenType::EQUAL_EQUAL: return c == 0 ? TriBool::True : TriBool::False;
                case TokenType::BANG_EQUAL: return c != 0 ? TriBool::True : TriBool::False;
                default: break;
            }
        }

        if (const auto interval = interval_of(canonical))
        {
            const auto by_interval = decide_by_interval(canonical_compare, *interval);
            if (by_interval != TriBool::Unknown) return by_interval;
        }

        const std::string key = canonical.to_string();
        // Several facts may exist for one difference; any one proving the
        // query True wins, otherwise any proving False decides False.
        bool any_false = false;
        for (const auto& [fact, fact_op] : _relations)
        {
            if (fact.to_string() != key) continue;
            const auto implied = imply(fact_op, canonical_compare);
            if (implied == TriBool::True) return TriBool::True;
            if (implied == TriBool::False) any_false = true;
        }
        // Query the negation: a stored fact for -diff decides the query too.
        const auto negated = canonical.negated();
        if (negated)
        {
            const std::string negated_key = negated->to_string();
            for (const auto& [fact, fact_op] : _relations)
            {
                if (fact.to_string() != negated_key) continue;
                const auto implied = imply(mirrored_op(fact_op), canonical_compare);
                if (implied == TriBool::True) return TriBool::True;
                if (implied == TriBool::False) any_false = true;
            }
        }
        return any_false ? TriBool::False : TriBool::Unknown;
    }

    std::optional<Interval> FactSet::interval_of(const Affine& affine) const
    {
        Interval total{int64_t{0}, int64_t{0}};
        for (const auto& [name, coefficient] : affine.coefficients)
        {
            const auto it = _intervals.find(name);
            if (it == _intervals.end()) return std::nullopt;
            const Interval& var_interval = it->second;

            Interval scaled;
            if (coefficient > 0)
            {
                int64_t value = 0;
                if (var_interval.lo)
                {
                    if (!checked_mul(*var_interval.lo, coefficient, value)) return std::nullopt;
                    scaled.lo = value;
                }
                if (var_interval.hi)
                {
                    if (!checked_mul(*var_interval.hi, coefficient, value)) return std::nullopt;
                    scaled.hi = value;
                }
            }
            else
            {
                if (var_interval.hi)
                {
                    int64_t value = 0;
                    if (!checked_mul(*var_interval.hi, coefficient, value)) return std::nullopt;
                    scaled.lo = value;
                }
                if (var_interval.lo)
                {
                    int64_t value = 0;
                    if (!checked_mul(*var_interval.lo, coefficient, value)) return std::nullopt;
                    scaled.hi = value;
                }
            }

            // Interval addition: a missing (unbounded) side absorbs any
            // finite contribution — treating it as 0 here once produced
            // unsound bounds like balance - amount <= -1.
            if (scaled.lo && total.lo)
            {
                int64_t merged = 0;
                if (!checked_add(*total.lo, *scaled.lo, merged)) return std::nullopt;
                total.lo = merged;
            }
            else total.lo = std::nullopt;
            if (scaled.hi && total.hi)
            {
                int64_t merged = 0;
                if (!checked_add(*total.hi, *scaled.hi, merged)) return std::nullopt;
                total.hi = merged;
            }
            else total.hi = std::nullopt;
        }
        return total;
    }

    void FactSet::assume(const Condition& condition, const bool truth)
    {
        switch (condition.kind)
        {
            case Condition::Kind::Literal:
                return;
            case Condition::Kind::Negation:
                assume(*condition.child, !truth);
                return;
            case Condition::Kind::Conjunction:
                if (truth)
                {
                    assume(*condition.lhs, true);
                    assume(*condition.rhs, true);
                }
                return;
            case Condition::Kind::Disjunction:
                if (!truth)
                {
                    // not (a || b) is (not a) and (not b)
                    assume(*condition.lhs, false);
                    assume(*condition.rhs, false);
                }
                return;
            case Condition::Kind::Comparison:
                assume_comparison(condition.left, truth ? condition.op : negate_op(condition.op),
                                  condition.right);
                return;
        }
    }

    TriBool FactSet::decide(const Condition& condition) const
    {
        switch (condition.kind)
        {
            case Condition::Kind::Literal:
                return condition.value ? TriBool::True : TriBool::False;
            case Condition::Kind::Negation:
                {
                    const auto child = decide(*condition.child);
                    if (child == TriBool::True) return TriBool::False;
                    if (child == TriBool::False) return TriBool::True;
                    return TriBool::Unknown;
                }
            case Condition::Kind::Conjunction:
                {
                    const auto lhs = decide(*condition.lhs);
                    if (lhs == TriBool::False) return TriBool::False;
                    const auto rhs = decide(*condition.rhs);
                    if (rhs == TriBool::False) return TriBool::False;
                    if (lhs == TriBool::True && rhs == TriBool::True) return TriBool::True;
                    return TriBool::Unknown;
                }
            case Condition::Kind::Disjunction:
                {
                    const auto lhs = decide(*condition.lhs);
                    if (lhs == TriBool::True) return TriBool::True;
                    const auto rhs = decide(*condition.rhs);
                    if (rhs == TriBool::True) return TriBool::True;
                    if (lhs == TriBool::False && rhs == TriBool::False) return TriBool::False;
                    return TriBool::Unknown;
                }
            case Condition::Kind::Comparison:
                return decide_comparison(condition.left, condition.op, condition.right);
        }
        return TriBool::Unknown;
    }

    void FactSet::join(const FactSet& other)
    {
        for (auto it = _equalities.begin(); it != _equalities.end();)
        {
            const auto found = other._equalities.find(it->first);
            if (found == other._equalities.end() || !affine_equal(found->second, it->second))
                it = _equalities.erase(it);
            else
                ++it;
        }

        for (auto it = _intervals.begin(); it != _intervals.end();)
        {
            const auto found = other._intervals.find(it->first);
            if (found == other._intervals.end())
            {
                it = _intervals.erase(it);
                continue;
            }
            Interval& mine = it->second;
            const Interval& theirs = found->second;
            if (mine.lo && theirs.lo) mine.lo = std::max(*mine.lo, *theirs.lo);
            else mine.lo = std::nullopt;
            if (mine.hi && theirs.hi) mine.hi = std::min(*mine.hi, *theirs.hi);
            else mine.hi = std::nullopt;
            if (mine.is_empty())
            {
                it = _intervals.erase(it);
                continue;
            }
            ++it;
        }

        std::erase_if(_relations, [&](const auto& mine)
        {
            return std::none_of(other._relations.begin(), other._relations.end(),
                                [&](const auto& theirs)
                                {
                                    return theirs.second == mine.second &&
                                        affine_equal(theirs.first, mine.first);
                                });
        });
    }

    std::optional<int64_t> FactSet::constant_of(const std::string& var) const
    {
        const auto equality = _equalities.find(var);
        if (equality != _equalities.end())
        {
            if (const auto constant = equality->second.as_constant()) return constant;
        }
        const auto interval = _intervals.find(var);
        if (interval != _intervals.end() && interval->second.lo && interval->second.hi &&
            *interval->second.lo == *interval->second.hi)
        {
            return *interval->second.lo;
        }
        return std::nullopt;
    }
}
