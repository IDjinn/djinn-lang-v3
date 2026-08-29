#include <gtest/gtest.h>

#include "DjinnCompiler.h"

TEST(Destructuring, BasicStructFields)
{
    const std::string source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { 10, 20 };
            auto { x, y } = p;
            return x + y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

TEST(Destructuring, VariablesVisibleInEnclosingScope)
{
    const std::string source = R"(
        struct Pair {
            i32 a;
            i32 b;
        }

        i32 main() {
            Pair p = { 3, 4 };
            auto { a, b } = p;
            i32 sum = a + b;
            i32 product = a * b;
            return sum + product;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 19);
}

TEST(Destructuring, SingleField)
{
    const std::string source = R"(
        struct Wrapper {
            i32 value;
        }

        i32 main() {
            Wrapper w = { 99 };
            auto { value } = w;
            return value;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Destructuring, FromExpression)
{
    const std::string source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        Point make() {
            Point p = { 5, 6 };
            return p;
        }

        i32 main() {
            auto { x, y } = make();
            return x * 10 + y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 56);
}

TEST(Destructuring, BareBlockStillIntroducesScope)
{
    // sanity: regular bare blocks must continue to scope their vars
    const std::string source = R"(
        i32 main() {
            {
                i32 inner = 5;
            }
            return inner;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_GT(result.diagnostics.size(), 0);
    EXPECT_TRUE(result.diagnostics.at(0).message.contains("undefined variable 'inner'"));
}
