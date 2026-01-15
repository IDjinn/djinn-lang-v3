#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Math, IntegerSubFunction) {
    const auto source = R"(
        i32 sub(i32 a, i32 b) {
            return a - b;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->functions.size(), 1);
}

TEST(Math, IntegerSub) {
    const auto source = R"(
        i32 sub(i32 a, i32 b) {
            return a - b;
        }

        i32 main() {
            return sub(10, 3);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 7);
    EXPECT_EQ(result.program->functions.size(), 2);
}

TEST(Math, IntegerSubNegative) {
    const auto source = R"(
        i32 sub(i32 a, i32 b) {
            return a - b;
        }

        i32 main() {
            return sub(3, 10);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    // -7 em complemento de 2 como código de retorno
    EXPECT_EQ(result.returnCode, -7);
}