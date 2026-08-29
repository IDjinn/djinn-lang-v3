#include <gtest/gtest.h>

#include "DjinnCompiler.h"

TEST(OperatorOverload, ParseOperatorInInterface)
{
    const auto source = R"(
        interface IEquatable<T> {
            operator ==(T left, T right) -> bool;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(OperatorOverload, ParseOperatorInImpl)
{
    const auto source = R"(
        struct Vec2 {
            i32 x;
            i32 y;
        }

        impl Vec2 {
            operator +(Vec2 left, Vec2 right) -> Vec2 {
                return { .x = left.x + right.x, .y = left.y + right.y };
            }
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(OperatorOverload, StructAddition)
{
    const auto source = R"(
        struct Vec2 {
            i32 x;
            i32 y;
        }

        impl Vec2 {
            operator +(Vec2 left, Vec2 right) -> Vec2 {
                return { .x = left.x + right.x, .y = left.y + right.y };
            }
        }

        i32 main() {
            Vec2 a = { .x = 1, .y = 2 };
            Vec2 b = { .x = 3, .y = 4 };
            Vec2 c = a + b;
            return c.x + c.y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .runAfterCompile = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(OperatorOverload, StructEquality)
{
    const auto source = R"(

        struct Point {
            i32 x;
            i32 y;
        }

        impl Point {
            operator ==(Point left, Point right) -> bool {
                return left.x == right.x && left.y == right.y;
            }
        }

        i32 main() {
            Point a = { .x = 5, .y = 10 };
            Point b = { .x = 5, .y = 10 };
            if (a == b) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .runAfterCompile = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(OperatorOverload, NotEqualDerivedFromEqual)
{
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        impl Point {
            operator ==(Point left, Point right) -> bool {
                return left.x == right.x && left.y == right.y;
            }
        }

        i32 main() {
            Point a = { .x = 1, .y = 2 };
            Point b = { .x = 3, .y = 4 };
            if (a != b) {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .runAfterCompile = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(OperatorOverload, BitwiseOperatorsOnPrimitives)
{
    const auto source = R"(
        i32 main() {
            i32 a = 12;
            i32 b = 10;

            i32 and_result = a & b;
            i32 or_result = a | b;
            i32 xor_result = a ^ b;
            i32 not_result = ~a;
            i32 shl_result = 1 << 3;
            i32 shr_result = 16 >> 2;

            // and: 1000 = 8, or: 1110 = 14, xor: 0110 = 6
            // not: depends on width, shl: 8, shr: 4
            // Return sum of and + or + xor + shl + shr = 8 + 14 + 6 + 8 + 4 = 40
            return and_result + or_result + xor_result + shl_result + shr_result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .runAfterCompile = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 40);
}

TEST(OperatorOverload, OperatorInStructBody)
{
    const auto source = R"(
        struct Vec2 {
            i32 x;
            i32 y;

            operator +(Vec2 left, Vec2 right) -> Vec2 {
                return { .x = left.x + right.x, .y = left.y + right.y };
            }
        }

        i32 main() {
            Vec2 a = { .x = 10, .y = 20 };
            Vec2 b = { .x = 5, .y = 7 };
            Vec2 c = a + b;
            return c.x + c.y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(OperatorOverload, MultipleOperatorsOnSameStruct)
{
    const auto source = R"(
        struct bool : i1;
        struct Vec2 {
            i32 x;
            i32 y;

            operator +(Vec2 left, Vec2 right) -> Vec2 {
                return { .x = left.x + right.x, .y = left.y + right.y };
            }

            operator ==(Vec2 left, Vec2 right) -> bool {
                return left.x == right.x && left.y == right.y;
            }
        }

        i32 main() {
            Vec2 a = { .x = 3, .y = 4 };
            Vec2 b = { .x = 3, .y = 4 };
            Vec2 c = { .x = 6, .y = 8 };
            Vec2 sum = a + b;
            if (sum == c) {
                return 99;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}
