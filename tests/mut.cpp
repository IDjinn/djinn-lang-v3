#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Mutable, BasicMutableVariables) {
    const auto source = R"(
        namespace math {
            i32 add(i32 a, i32 b) {
                return a + b;
            }
        }

        import math;
        namespace test;

        i32 main() {
            auto mut result = 10;
            result = math::add(1, 1);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Mutable, InvalidImmutableAssingment) {
    const auto source = R"(
        namespace math {
            i32 add(i32 a, i32 b) {
                return a + b;
            }
        }

        import math;
        namespace test;

        i32 main() {
            auto result = 10;
            result = math::add(1, 1);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_EQ(result.returnCode, 1);
}
