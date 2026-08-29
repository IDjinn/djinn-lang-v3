#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "DjinnCompiler.h"

TEST(Math, IntegerSubFunction) {
    const auto source = R"(
        i32 sub(i32 a, i32 b) {
            return a - b;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
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

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
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

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    
    EXPECT_EQ(result.returnCode, DJINN_EXIT(-7));
}
