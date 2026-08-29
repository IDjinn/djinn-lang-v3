#include <gtest/gtest.h>

#include "DjinnCompiler.h"

TEST(StringInterpolation, SimpleIdentifier)
{
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        i32 main() {
            i32 name = 42;
            printf("hello {name}\n");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("hello {0}"), std::string::npos);
}

TEST(StringInterpolation, MultipleArgs)
{
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        i32 main() {
            i32 a = 1;
            i32 b = 2;
            printf("a={a} b={b}\n");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("a={0} b={1}"), std::string::npos);
}

TEST(StringInterpolation, MixedWithExplicitPositional)
{
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        i32 main() {
            i32 x = 7;
            i32 y = 9;
            printf("first={0} second={y}\n", x);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("first={0} second={1}"), std::string::npos);
}

TEST(StringInterpolation, NoInterpolationLeavesStringUnchanged)
{
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        i32 main() {
            printf("plain string\n");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("plain string"), std::string::npos);
}

TEST(StringInterpolation, FieldAccessInsideBraces)
{
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { 10, 20 };
            printf("p.x={p.x} p.y={p.y}\n");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("p.x={0} p.y={1}"), std::string::npos);
}

TEST(StringInterpolation, BlockStringTripleQuoted)
{
    const std::string source = R"DJINN(
        extern i32 printf(i8* format, ...);

        i32 main() {
            i32 name = 42;
            printf("""hello {name}
on next line""");
            return 0;
        }
    )DJINN";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("hello {0}"), std::string::npos);
    EXPECT_NE(result.ir.find("on next line"), std::string::npos);
}

TEST(StringInterpolation, BlockStringNoEscapes)
{
    const std::string source = R"DJINN(
        extern i32 printf(i8* format, ...);

        i32 main() {
            printf("""path: C:\Users\foo""");
            return 0;
        }
    )DJINN";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("C:\\\\Users\\\\foo"), std::string::npos);
}

TEST(StringInterpolation, ArithmeticExpressionInsideBraces)
{
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        i32 main() {
            i32 a = 3;
            i32 b = 4;
            printf("sum={a + b}\n");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("sum={0}"), std::string::npos);
}

TEST(StringInterpolation, FunctionCallExpandsAsArgument)
{
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        i32 get_value() { return 42; }

        i32 main() {
            printf("value={get_value()}\n");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("value={0}"), std::string::npos);
}

TEST(StringInterpolation, RepeatedExpressionEvaluatedTwice)
{
    // Document current limitation: each {expr} occurrence is appended as a separate
    // argument, so side-effecting expressions are evaluated multiple times. This test
    // pins the behavior — change it intentionally if dedup is added later.
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        i32 main() {
            i32 v = 5;
            printf("{v} and {v}\n");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("{0} and {1}"), std::string::npos);
}

TEST(StringInterpolation, EmptyBracesPreserved)
{
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        i32 main() {
            printf("nothing {} here\n");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("nothing {} here"), std::string::npos);
}

TEST(StringInterpolation, NonCallStringLiteralNotInterpolated)
{
    // Limitation: interpolation only triggers inside function-call args.
    // A bare string literal assigned to a variable is left as-is.
    const std::string source = R"(
        i32 main() {
            i32 name = 7;
            str s = "hello {name}";
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_NE(result.ir.find("hello {name}"), std::string::npos);
}
