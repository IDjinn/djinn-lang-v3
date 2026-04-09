#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Math, IntegerDivFunction)
{
    const auto source = R"(
        i32 div(i32 a, i32 b) {
            return a / b;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.print_ast = true, .print_ir = true, .optimizationLevel = 0});
    EXPECT_EQ(result.program->functions.size(), 1);
}

TEST(Math, IntegerDiv)
{
    const auto source = R"(
        i32 div(i32 a, i32 b) {
            return a / b;
        }

        i32 main() {
            return div(10, 2);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 5);
    EXPECT_EQ(result.program->functions.size(), 2);
}

TEST(Math, IntegerDivWithRemainder)
{
    const auto source = R"(
        i32 div(i32 a, i32 b) {
            return a / b;
        }

        i32 main() {
            return div(7, 2);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.returnCode, 3);
}
