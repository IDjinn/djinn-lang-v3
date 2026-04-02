#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Macro, SimpleExprMacro)
{
    const auto source = R"(
        macro double_it {
            (expr x) => {
                x + x
            }
        }

        i32 main() {
            return double_it(21);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Macro, MultipleParameters)
{
    const auto source = R"(
        macro add {
            (expr a, expr b) => {
                a + b
            }
        }

        i32 main() {
            return add(10, 32);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Macro, PrecedencePreserved)
{
    const auto source = R"(
        macro mul {
            (expr a, expr b) => {
                a * b
            }
        }

        i32 main() {
            return mul(2 + 3, 4 + 1);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 25);
}

TEST(Macro, LocalAvoidsDoubleEvaluation)
{
    const auto source = R"(
        macro square {
            (local expr v) => {
                v * v
            }
        }

        i32 counter = 0;

        i32 next() {
            counter = counter + 1;
            return counter;
        }

        i32 main() {
            i32 result = square(next());
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(Macro, WithoutLocalCausesDoubleEvaluation)
{
    const auto source = R"(
        macro square_no_local {
            (expr v) => {
                v * v
            }
        }

        i32 counter = 0;

        i32 next() {
            counter = counter + 1;
            return counter;
        }

        i32 main() {
            i32 result = square_no_local(next());
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(Macro, LocalWithArithmeticExpression)
{
    const auto source = R"(
        macro square {
            (local expr v) => {
                v * v
            }
        }

        i32 main() {
            i32 x = 5;
            return square(x + 2);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 49);
}

TEST(Macro, MixedLocalAndNonLocal)
{
    const auto source = R"(
        macro add_squared {
            (local expr a, expr b) => {
                a * a + b
            }
        }

        i32 main() {
            return add_squared(3, 10);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 19);
}

TEST(Macro, NestedMacroCall)
{
    const auto source = R"(
        macro square {
            (local expr v) => {
                v * v
            }
        }

        macro add {
            (expr a, expr b) => {
                a + b
            }
        }

        i32 main() {
            return add(square(3), square(4));
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 25);
}

TEST(Macro, WarnOnPossibleSideEffect)
{
    const auto source = R"(
        macro bad_square {
            (expr v) => {
                v * v
            }
        }

        i32 main() {
            return bad_square(5);
        }
    )";

    const auto result = DjinnCompiler::run(source);
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
            (local expr v) => {
                v * v
            }
        }

        i32 main() {
            return good_square(5);
        }
    )";

    const auto result = DjinnCompiler::run(source);
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
            (expr v) => {
                v
            }
        }

        i32 main() {
            return identity(42);
        }
    )";

    const auto result = DjinnCompiler::run(source);
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
            (expr x) => {
                x
            }
        }

        i32 main() {
            return my_macro(10);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.program->macros.size(), 1);
    EXPECT_EQ(result.program->macros[0]->name.token_name, "my_macro");
    EXPECT_EQ(result.returnCode, 10);
}