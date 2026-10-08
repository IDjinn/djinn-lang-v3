//
// Decidable fragment for value-level verification (VERIFICATION-SPEC.md §6.3):
// quantifier-free linear integer arithmetic over named values. Expressions
// outside the fragment translate to null / Unknown and every check over them
// stays conservative — the verifier may under-approximate what it can prove,
// never claim a violation it did not decide.
//

#ifndef DJINN_VERIFICATION_FRAGMENT_H
#define DJINN_VERIFICATION_FRAGMENT_H

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "../lexer/TokenType.h"

struct Expression;

namespace djinn::verification
{
    enum class TriBool : uint8_t { False, True, Unknown };

    // c + c1*v1 + ... + cn*vn with exact int64 arithmetic; every operation
    // that would overflow returns failure instead of wrapping.
    struct Affine
    {
        int64_t constant = 0;
        std::map<std::string, int64_t> coefficients;

        static Affine of_constant(int64_t value);
        static Affine of_variable(const std::string& name);

        [[nodiscard]] bool is_constant() const { return coefficients.empty(); }
        [[nodiscard]] std::optional<int64_t> as_constant() const;
        [[nodiscard]] bool is_single_variable(std::string* name = nullptr) const;

        [[nodiscard]] std::optional<Affine> scaled(int64_t factor) const;
        [[nodiscard]] std::optional<Affine> plus(const Affine& other, int64_t scale = 1) const;
        [[nodiscard]] std::optional<Affine> negated() const { return scaled(-1); }

        // Inline one variable occurrence; used for equality substitution.
        [[nodiscard]] std::optional<Affine> substituted(const std::string& name, const Affine& value) const;

        [[nodiscard]] std::string to_string() const;
    };

    // Boolean structure over affine comparisons; built by the translator and
    // consumed by FactSet (assume/decide) and the body analyzer.
    struct Condition
    {
        enum class Kind : uint8_t { Comparison, Conjunction, Disjunction, Negation, Literal };

        Kind kind = Kind::Literal;
        bool value = false; // Literal

        // Comparison
        TokenType op = TokenType::EQUAL_EQUAL;
        Affine left;
        Affine right;

        std::unique_ptr<Condition> lhs;  // Conjunction / Disjunction
        std::unique_ptr<Condition> rhs;  // Conjunction / Disjunction
        std::unique_ptr<Condition> child; // Negation
    };

    // Translate an expression into the linear fragment; null = outside fragment.
    [[nodiscard]] std::unique_ptr<Condition> translate_condition(const Expression& expr);

    // Translate an integer-valued expression; nullopt = outside fragment.
    [[nodiscard]] std::optional<Affine> translate_linear(const Expression& expr);

    [[nodiscard]] std::string condition_to_string(const Condition& condition);
}

#endif //DJINN_VERIFICATION_FRAGMENT_H
