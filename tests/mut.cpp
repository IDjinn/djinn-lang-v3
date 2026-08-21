#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Mutable, BasicMutableVariables)
{
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

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(Mutable, InvalidImmutableAssingment)
{
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

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    // The binder emits E4004 and re-throws it as a CompileError; the engine
    // deduplicates the top-level re-emit, so exactly one diagnostic remains.
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_EQ(result.returnCode, 1);
}