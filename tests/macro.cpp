#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "../DjinnCompiler.h"

TEST(Macro, SimpleExprMacro)
{
    const auto source = R"(
        macro double_it {
            (expression x) => {
                x + x
            }
        }

        i32 main() {
            return double_it(21);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 42);
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_TRUE(result.diagnostics.at(0).message.contains("times without 'local', which may cause side effects"));
}

TEST(Macro, MultipleParameters)
{
    const auto source = R"(
        macro add {
            (expression a, expression b) => {
                a + b
            }
        }

        i32 main() {
            return add(10, 32);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Macro, PrecedencePreserved)
{
    const auto source = R"(
        macro mul {
            (expression a, expression b) => {
                a * b
            }
        }

        i32 main() {
            return mul(2 + 3, 4 + 1);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 25);
}

TEST(Macro, LocalAvoidsDoubleEvaluation)
{
    const auto source = R"(
        macro square {
            (local expression v) => {
                v * v
            }
        }

        static mut i32 counter = 0;

        i32 next() {
            counter = counter + 1;
            return counter;
        }

        i32 main() {
            i32 result = square(next());
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 1);
}

TEST(Macro, WithoutLocalCausesDoubleEvaluation)
{
    const auto source = R"(
        macro square_no_local {
            (expression v) => {
                v * v
            }
        }

        static mut i32 counter = 0;

        i32 next() {
            counter = counter + 1;
            return counter;
        }

        i32 main() {
            i32 result = square_no_local(next());
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 2);
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_TRUE(result.diagnostics.at(0).message.contains("is used 2 times without 'local'"));
}

TEST(Macro, LocalWithArithmeticExpression)
{
    const auto source = R"(
        macro square {
            (local expression v) => {
                v * v
            }
        }

        i32 main() {
            i32 x = 5;
            return square(x + 2);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 49);
}

TEST(Macro, MixedLocalAndNonLocal)
{
    const auto source = R"(
        macro add_squared {
            (local expression a, expression b) => {
                a * a + b
            }
        }

        i32 main() {
            return add_squared(3, 10);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 19);
}

TEST(Macro, NestedMacroCall)
{
    const auto source = R"(
        macro square {
            (local expression v) => {
                v * v
            }
        }

        macro add {
            (expression a, expression b) => {
                a + b
            }
        }

        i32 main() {
            return add(square(3), square(4));
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 25);
}

TEST(Macro, WarnOnPossibleSideEffect)
{
    const auto source = R"(
        macro bad_square {
            (expression v) => {
                v * v
            }
        }

        i32 main() {
            return bad_square(5);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    bool hasWarning = false;
    for (const auto& d : result.diagnostics)
    {
        if (d.code == DiagnosticCode::MACRO_POSSIBLE_SIDE_EFFECT)
        {
            hasWarning = true;
            break;
        }
    }
    EXPECT_TRUE(hasWarning);
    EXPECT_EQ(result.returnCode, 25);
}

TEST(Macro, NoWarnWhenLocalUsed)
{
    const auto source = R"(
        macro good_square {
            (local expression v) => {
                v * v
            }
        }

        i32 main() {
            return good_square(5);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    bool hasWarning = false;
    for (const auto& d : result.diagnostics)
    {
        if (d.code == DiagnosticCode::MACRO_POSSIBLE_SIDE_EFFECT)
        {
            hasWarning = true;
            break;
        }
    }
    EXPECT_FALSE(hasWarning);
    EXPECT_EQ(result.returnCode, 25);
}

TEST(Macro, NoWarnWhenSingleUse)
{
    const auto source = R"(
        macro identity {
            (expression v) => {
                v
            }
        }

        i32 main() {
            return identity(42);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    bool hasWarning = false;
    for (const auto& d : result.diagnostics)
    {
        if (d.code == DiagnosticCode::MACRO_POSSIBLE_SIDE_EFFECT)
        {
            hasWarning = true;
            break;
        }
    }
    EXPECT_FALSE(hasWarning);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Macro, MacroStoredInProgram)
{
    const auto source = R"(
        macro my_macro {
            (expression x) => {
                x
            }
        }

        i32 main() {
            return my_macro(10);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.program->macros.size(), 1);
    EXPECT_EQ(result.program->macros[0]->name.token_name, "my_macro");
    EXPECT_EQ(result.returnCode, 10);
}

TEST(Macro, MultiRuleByArity)
{
    const auto source = R"(
        macro op {
            (local expression v, expression multiplier) => { v * multiplier }
            (local expression v) => { v * 2 }
        }

        i32 main() {
            i32 a = op(5, 3);
            i32 b = op(5);
            return a + b;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 25); // (5*3) + (5*2) = 15 + 10
}

TEST(Macro, AmbiguousRulesError)
{
    const auto source = R"(
        macro pick {
            (expression a, expression b) => { a }
            (expression a, expression b) => { b }
        }

        i32 main() {
            return pick(10, 99);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_GE(result.diagnostics.size(), 1);
    EXPECT_TRUE(result.diagnostics.at(0).message.contains("regra 1 contém a mesma assinatura"));
}

TEST(Macro, MultiRuleNoMatch)
{
    const auto source = R"(
        macro single {
            (expression a) => { a }
        }

        i32 main() {
            return single(1, 2, 3);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_GT(result.diagnostics.size(), 0);
}

TEST(Macro, LiteralTokenMatching)
{
    const auto source = R"(
        macro op {
            (add, expression a, expression b) => { a + b }
            (mul, expression a, expression b) => { a * b }
        }

        i32 main() {
            i32 x = op(add, 10, 5);
            i32 y = op(mul, 3, 7);
            return x + y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 36); // 15 + 21
}

TEST(Macro, LiteralTokenNoMatch)
{
    const auto source = R"(
        macro op {
            (add, expression a, expression b) => { a + b }
        }

        i32 main() {
            return op(sub, 10, 5);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_GT(result.diagnostics.size(), 0);
}

TEST(Macro, IdentifierFragment)
{
    const auto source = R"(
        macro get_val {
            (identifier name) => { name }
        }

        static i32 x_val = 42;

        i32 main() {
            return get_val(x_val);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Macro, LiteralTokenWithMultiRule)
{
    const auto source = R"(
        macro calc {
            (double, local expression v) => { v + v }
            (square, local expression v) => { v * v }
            (expression v) => { v }
        }

        i32 main() {
            i32 a = calc(double, 5);
            i32 b = calc(square, 4);
            i32 c = calc(7);
            return a + b + c;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 33); // 10 + 16 + 7
}

TEST(Macro, RuleLevelLocal)
{
    const auto source = R"(
        macro sum_squares {
            local (expression a, expression b) => { a * a + b * b }
        }

        static mut i32 counter = 0;

        i32 next() {
            counter = counter + 1;
            return counter;
        }

        i32 main() {
            i32 result = sum_squares(next(), next());
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 5); // 1*1 + 2*2 = 1 + 4
}

TEST(Macro, LogWithLiteralTokenLevels)
{
    const auto source = R"(
        macro log {
            (debug, expression msg) => { do_log(msg) }
            (info, expression msg)  => { do_log(msg) }
            (off, expression msg)   => void
        }

        static mut i32 log_count = 0;

        void do_log(i32 x) {
            log_count = log_count + x;
        }

        i32 main() {
            log(debug, 10);
            log(info, 20);
            log(off, 99);
            return log_count;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 30); // 10 + 20, off ignored
}

TEST(Macro, LogLiteralTokenNoMatchLevel)
{
    const auto source = R"(
        macro log {
            (debug, expression msg) => { msg }
            (info, expression msg)  => { msg }
        }

        i32 main() {
            return log(warn, 42);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(Macro, LogMultiRuleWithIdentifier)
{
    const auto source = R"(
        macro get_level {
            (debug) => { debug_val }
            (info)  => { info_val }
        }

        static i32 debug_val = 1;
        static i32 info_val = 2;

        i32 main() {
            i32 a = get_level(debug);
            i32 b = get_level(info);
            return a + b;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 3); // 1 + 2
}

TEST(Macro, LiteralTokenWithIdentifierAndExpression)
{
    const auto source = R"(
        macro dispatch {
            (get, identifier name) => { name }
            (set, identifier name, expression val) => { name = val }
            (add, expression a, expression b) => { a + b }
        }

        static i32 x = 10;
        static mut i32 y = 20;

        i32 main() {
            i32 a = dispatch(get, x);
            dispatch(set, y, 99);
            i32 b = dispatch(add, a, y);
            return b;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 109); // 10 + 99
}

TEST(Macro, LiteralTokenPatternMatchingComplex)
{
    const auto source = R"(
        macro math {
            (double, local expression v) => { v + v }
            (square, local expression v) => { v * v }
            (negate, expression v) => { 0 - v }
            (identity, expression v) => { v }
        }

        i32 main() {
            i32 a = math(double, 7);
            i32 b = math(square, 5);
            i32 c = math(negate, 3);
            i32 d = math(identity, 42);
            return a + b + c + d;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 78); // 14 + 25 + (-3) + 42
}

TEST(Macro, LogPatternLikeRust)
{
    const auto source = R"(
        macro log {
            (debug, expression msg) => { trace(msg) }
            (info, expression msg)  => { trace(msg) }
            (off, expression msg)   => void
        }

        static mut i32 call_count = 0;

        void trace(i32 val) {
            call_count = call_count + val;
        }

        i32 main() {
            log(debug, 1);
            log(info, 2);
            log(off, 100);
            log(debug, 4);
            log(off, 200);
            log(info, 8);
            return call_count;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 15); // 1 + 2 + 4 + 8, off calls ignored
}

TEST(Macro, ConditionalCodegenViaLiteralToken)
{
    const auto source = R"(
        macro feature {
            (enabled, expression code) => { code }
            (disabled, expression code) => void
        }

        i32 main() {
            i32 a = feature(enabled, 42);
            i32 b = feature(disabled, 99);
            i32 c = feature(enabled, 8);
            return a + b + c;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 50); // 42 + 0 + 8
}

TEST(Macro, LiteralFragment)
{
    const auto source = R"(
        macro double_lit {
            (literal val) => { val + val }
        }

        i32 main() {
            return double_lit(21);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Macro, LiteralFragmentRejectsExpression)
{
    const auto source = R"(
        macro lit_only {
            (literal val) => { val }
        }

        i32 main() {
            i32 x = 5;
            return lit_only(x);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(Macro, TypeFragment)
{
    const auto source = R"(
        macro default_val {
            (type T) => { 123 }
        }

        i32 main() {
            return default_val(i32);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 123);
}

TEST(Macro, LiteralTokenWithLiteralFragment)
{
    const auto source = R"(
        macro config {
            (max_size) => { 1024 }
            (min_size) => { 64 }
            (literal fallback) => { fallback }
        }

        i32 main() {
            i32 a = config(max_size);
            i32 b = config(min_size);
            i32 c = config(42);
            return a + b + c;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, DJINN_EXIT(1130)); // 1024 + 64 + 42
}
