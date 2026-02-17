#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Math, IntegerMultFunction) {
    const auto source = R"(
        i32 mult(i32 a, i32 b) {
            return a * b;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->functions.size(), 1);
}

TEST(Math, IntegerMult) {
    const auto source = R"(
        i32 mult(i32 a, i32 b) {
            return a * b;
        }

        i32 main() {
            return mult(2, 3);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 6);
    EXPECT_EQ(result.program->functions.size(), 2);
}