#include <gtest/gtest.h>
#include "../DjinnCompiler.h"

TEST(Cast, IntTruncation)
{
    const auto source = R"(
        i32 main() {
            i64 big = 42;
            i32 small = (i32)big;
            return small;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Cast, IntWidening)
{
    const auto source = R"(
        i32 main() {
            i8 small = 7;
            i32 big = (i32)small;
            return big;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 7);
}

TEST(Cast, FloatToInt)
{
    const auto source = R"(
        i32 main() {
            f64 pi = 3.14;
            i32 x = (i32)pi;
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(Cast, IntToFloat)
{
    const auto source = R"(
        i32 main() {
            i32 x = 5;
            f64 y = (f64)x;
            f64 z = y + 0.5;
            return (i32)z;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Cast, ChainedCast)
{
    const auto source = R"(
        i32 main() {
            i64 a = 100;
            i8 b = (i8)a;
            i32 c = (i32)b;
            return c;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 100);
}

TEST(Cast, SameTypeCastNoop)
{
    const auto source = R"(
        i32 main() {
            i32 x = 99;
            i32 y = (i32)x;
            return y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Cast, CastInExpression)
{
    const auto source = R"(
        i32 main() {
            i64 a = 10;
            i64 b = 3;
            i32 result = (i32)a + (i32)b;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 13);
}

TEST(Cast, FloatTruncation)
{
    const auto source = R"(
        i32 main() {
            f64 big = 2.718;
            f32 small = (f32)big;
            return (i32)small;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(Cast, PointerCast)
{
    const auto source = R"(
        i32 main() {
            i32 x = 42;
            i32* p = &x;
            i8* raw = (i8*)p;
            i32* back = (i32*)raw;
            return *back;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Cast, CastLiteral)
{
    const auto source = R"(
        i32 main() {
            i32 x = (i32)3.14;
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(Cast, NegativeFloatToInt)
{
    const auto source = R"(
        i32 main() {
            f64 neg = 0.0 - 9.7;
            i32 x = (i32)neg;
            return 0 - x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 9);
}