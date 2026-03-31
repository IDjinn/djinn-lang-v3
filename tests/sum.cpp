#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Math, IntegerSumFunction) {
    const auto source = R"(
        i32 sum(i32 a, i32 b) {
            return a + b;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->functions.size(), 1);
}

TEST(Math, IntegerSum) {
    const auto source = R"(
        i32 sum(i32 a, i32 b) {
            return a + b;
        }

        i32 main() {
            return sum(2, 3);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 5);
    EXPECT_EQ(result.program->functions.size(), 2);
}

TEST(Math, IntegerSumWithAnonStructReturn)
{
    const auto source = R"(
        { i32 result; } sum(i32 a, i32 b) {
            return { a + b };
        }

        i32 main() {
            return sum(2, 3).result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 5);
    EXPECT_EQ(result.program->functions.size(), 2);
}
