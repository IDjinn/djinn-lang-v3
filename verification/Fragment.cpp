#include "Fragment.h"

#include <algorithm>

#include "../parser/ast/Statement.h"

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

        std::string strip_digit_separators(const std::string& raw)
        {
            std::string digits;
            digits.reserve(raw.size());
            for (const char c : raw)
            {
                if (c != '_' && c != '\'') digits += c;
            }
            return digits;
        }

        std::optional<int64_t> parse_integer_literal(const IntegerLiteral& literal)
        {
            std::string digits = strip_digit_separators(literal.value);
            int base = 10;
            if (digits.size() > 2 && digits[0] == '0')
            {
                if (digits[1] == 'x' || digits[1] == 'X')
                {
                    base = 16;
                    digits = digits.substr(2);
                }
                else if (digits[1] == 'b' || digits[1] == 'B')
                {
                    base = 2;
                    digits = digits.substr(2);
                }
            }
            if (digits.empty()) return std::nullopt;

            try
            {
                size_t consumed = 0;
                // Literal magnitudes above int64 stay outside the fragment.
                // `sign` is the signedness of the literal's TYPE (10i vs
                // 10u), not its value — a negative literal arrives here as
                // UnaryExpression(MINUS, literal).
                const int64_t parsed = std::stoll(digits, &consumed, base);
                if (consumed != digits.size()) return std::nullopt;
                return parsed;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
    }

    Affine Affine::of_constant(const int64_t value)
    {
        Affine affine;
        affine.constant = value;
        return affine;
    }

    Affine Affine::of_variable(const std::string& name)
    {
        Affine affine;
        affine.coefficients[name] = 1;
        return affine;
    }

    std::optional<int64_t> Affine::as_constant() const
    {
        if (!is_constant()) return std::nullopt;
        return constant;
    }

    bool Affine::is_single_variable(std::string* name) const
    {
        if (constant != 0 || coefficients.size() != 1) return false;
        const auto& [var, coefficient] = *coefficients.begin();
        if (coefficient != 1) return false;
        if (name) *name = var;
        return true;
    }

    std::optional<Affine> Affine::scaled(const int64_t factor) const
    {
        Affine result;
        if (!checked_mul(constant, factor, result.constant)) return std::nullopt;
        for (const auto& [name, coefficient] : coefficients)
        {
            int64_t scaled_coefficient = 0;
            if (!checked_mul(coefficient, factor, scaled_coefficient)) return std::nullopt;
            if (scaled_coefficient != 0) result.coefficients[name] = scaled_coefficient;
        }
        return result;
    }

    std::optional<Affine> Affine::plus(const Affine& other, const int64_t scale) const
    {
        Affine result = *this;
        int64_t scaled_constant = 0;
        if (!checked_mul(other.constant, scale, scaled_constant)) return std::nullopt;
        if (!checked_add(result.constant, scaled_constant, result.constant)) return std::nullopt;

        for (const auto& [name, coefficient] : other.coefficients)
        {
            int64_t scaled_coefficient = 0;
            if (!checked_mul(coefficient, scale, scaled_coefficient)) return std::nullopt;
            if (scaled_coefficient == 0) continue;
            int64_t merged = result.coefficients[name];
            if (!checked_add(merged, scaled_coefficient, merged)) return std::nullopt;
            if (merged == 0)
                result.coefficients.erase(name);
            else
                result.coefficients[name] = merged;
        }
        return result;
    }

    std::optional<Affine> Affine::substituted(const std::string& name, const Affine& value) const
    {
        const auto it = coefficients.find(name);
        if (it == coefficients.end()) return *this;
        const int64_t coefficient = it->second;

        Affine reduced = *this;
        reduced.coefficients.erase(name);
        const auto replacement = value.scaled(coefficient);
        if (!replacement) return std::nullopt;
        return reduced.plus(*replacement);
    }

    std::string Affine::to_string() const
    {
        std::string text;
        if (constant != 0 || coefficients.empty())
        {
            text += std::to_string(constant);
        }
        for (const auto& [name, coefficient] : coefficients)
        {
            if (coefficient > 0)
            {
                text += text.empty() ? "" : " + ";
            }
            else
            {
                text += text.empty() ? "-" : " - ";
            }
            const int64_t magnitude = coefficient < 0 ? -coefficient : coefficient;
            if (magnitude != 1) text += std::to_string(magnitude) + "*";
            // Ghost names ("#this.balance") are pre-state snapshots: render
            // them in the source vocabulary.
            text += name.starts_with('#') ? "old(" + name.substr(1) + ")" : name;
        }
        return text.empty() ? "0" : text;
    }

    std::optional<Affine> translate_linear(const Expression& expr, const bool pre_state)
    {
        if (const auto* literal = dynamic_cast<const IntegerLiteral*>(&expr))
        {
            const auto value = parse_integer_literal(*literal);
            if (!value) return std::nullopt;
            return Affine::of_constant(*value);
        }

        if (const auto* identifier = dynamic_cast<const Identifier*>(&expr))
        {
            return Affine::of_variable(identifier->name());
        }

        // `object.field` on a named object becomes a tracked variable
        // ("this.balance" / "from.balance") so entity invariants and field
        // writes stay decidable. Anything deeper stays outside the fragment.
        // Under pre_state the name denotes the entry snapshot the walker
        // preserves as a ghost ("#this.balance") across the field's first
        // write.
        if (const auto* field_access = dynamic_cast<const FieldAccess*>(&expr))
        {
            if (const auto* object = dynamic_cast<const Identifier*>(field_access->object.get()))
            {
                const std::string path = object->name() + "." + field_access->fieldName.token_name;
                return Affine::of_variable(pre_state ? "#" + path : path);
            }
            return std::nullopt;
        }

        // `old(expr)` in a claim selects the entry snapshot of its field
        // paths; in pre-state contexts it is its own meaning already, so a
        // nested old() stays outside the fragment.
        if (const auto* call = dynamic_cast<const FunctionCall*>(&expr))
        {
            if (!pre_state && call->name.token_name == "old" && call->arguments.size() == 1)
                return translate_linear(*call->arguments.front(), true);
            return std::nullopt;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expr))
        {
            if (unary->op != TokenType::MINUS) return std::nullopt;
            const auto operand = translate_linear(*unary->operand, pre_state);
            if (!operand) return std::nullopt;
            return operand->negated();
        }

        if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expr))
        {
            const auto left = translate_linear(*binary->left, pre_state);
            if (!left) return std::nullopt;
            const auto right = translate_linear(*binary->right, pre_state);
            if (!right) return std::nullopt;

            switch (binary->op)
            {
                case TokenType::PLUS:
                    return left->plus(*right);
                case TokenType::MINUS:
                    return left->plus(*right, -1);
                case TokenType::STAR:
                    if (const auto constant = right->as_constant())
                        return left->scaled(*constant);
                    if (const auto constant = left->as_constant())
                        return right->scaled(*constant);
                    return std::nullopt;
                default:
                    return std::nullopt;
            }
        }

        return std::nullopt;
    }

    namespace
    {
        std::unique_ptr<Condition> comparison_condition(const TokenType op,
                                                        const Expression& left_expr,
                                                        const Expression& right_expr,
                                                        const bool pre_state)
        {
            const auto left = translate_linear(left_expr, pre_state);
            if (!left) return nullptr;
            const auto right = translate_linear(right_expr, pre_state);
            if (!right) return nullptr;

            auto condition = std::make_unique<Condition>();
            condition->kind = Condition::Kind::Comparison;
            condition->op = op;
            condition->left = *left;
            condition->right = *right;
            return condition;
        }
    }

    std::unique_ptr<Condition> translate_condition(const Expression& expr, const bool pre_state)
    {
        if (const auto* literal = dynamic_cast<const BooleanLiteral*>(&expr))
        {
            auto condition = std::make_unique<Condition>();
            condition->kind = Condition::Kind::Literal;
            condition->value = literal->value == "true";
            return condition;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpression*>(&expr))
        {
            if (unary->op != TokenType::BANG) return nullptr;
            auto child = translate_condition(*unary->operand, pre_state);
            if (!child) return nullptr;
            auto condition = std::make_unique<Condition>();
            condition->kind = Condition::Kind::Negation;
            condition->child = std::move(child);
            return condition;
        }

        if (const auto* binary = dynamic_cast<const BinaryExpression*>(&expr))
        {
            switch (binary->op)
            {
                case TokenType::EQUAL_EQUAL:
                case TokenType::BANG_EQUAL:
                case TokenType::LESS:
                case TokenType::LESS_EQUAL:
                case TokenType::GREATER:
                case TokenType::GREATER_EQUAL:
                    return comparison_condition(binary->op, *binary->left, *binary->right, pre_state);
                case TokenType::AND_AND:
                case TokenType::OR_OR:
                    {
                        auto lhs = translate_condition(*binary->left, pre_state);
                        if (!lhs) return nullptr;
                        auto rhs = translate_condition(*binary->right, pre_state);
                        if (!rhs) return nullptr;
                        auto condition = std::make_unique<Condition>();
                        condition->kind = binary->op == TokenType::AND_AND
                                              ? Condition::Kind::Conjunction
                                              : Condition::Kind::Disjunction;
                        condition->lhs = std::move(lhs);
                        condition->rhs = std::move(rhs);
                        return condition;
                    }
                default:
                    return nullptr;
            }
        }

        return nullptr;
    }

    namespace
    {
        std::string render_operand(const Condition& condition)
        {
            std::string text;
            switch (condition.kind)
            {
                case Condition::Kind::Comparison:
                    text = condition.left.to_string() + " " + tokenTypeToString(condition.op) + " " +
                           condition.right.to_string();
                    break;
                case Condition::Kind::Conjunction:
                    text = render_operand(*condition.lhs) + " && " + render_operand(*condition.rhs);
                    break;
                case Condition::Kind::Disjunction:
                    text = render_operand(*condition.lhs) + " || " + render_operand(*condition.rhs);
                    break;
                case Condition::Kind::Negation:
                    text = "!" + render_operand(*condition.child);
                    break;
                case Condition::Kind::Literal:
                    text = condition.value ? "true" : "false";
                    break;
            }
            return condition.kind == Condition::Kind::Comparison ||
                           condition.kind == Condition::Kind::Literal
                       ? text
                       : "(" + text + ")";
        }
    }

    std::string condition_to_string(const Condition& condition)
    {
        return render_operand(condition);
    }
}
