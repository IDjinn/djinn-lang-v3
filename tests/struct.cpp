#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Struct, Definition)
{
    const auto source = R"(
        struct Result {
            i32 value;
        }

        void main() {
            Result result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->functions.size(), 1);
    EXPECT_EQ(result.program->structs.size(), 1);
}

TEST(Struct, BraceInitializerPositional)
{
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { 10, 20 };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->structs.size(), 1);
}

TEST(Struct, BraceInitializerDesignated)
{
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { .x = 5, .y = 15 };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->structs.size(), 1);
}

TEST(Struct, FieldAccess)
{
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { .x = 42, .y = 10 };
            return p.x;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Struct, FieldAccessSecondField)
{
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { .x = 10, .y = 99 };
            return p.y;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Struct, MethodReturnWithBraceInit)
{
    const auto source = R"(
        struct Result {
            i32 value;
        }

        Result sum(i32 a, i32 b) {
            return { .value = a + b };
        }

        i32 main() {
            auto res = sum(10, 20);
            return res.value;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

TEST(Struct, AutoInferStructType)
{
    const auto source = R"(
        struct Data {
            i32 num;
        }

        Data createData(i32 n) {
            return { .num = n };
        }

        i32 main() {
            auto d = createData(77);
            return d.num;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 77);
}

TEST(Struct, MultipleFieldsPositional)
{
    const auto source = R"(
        struct Vec3 {
            i32 x;
            i32 y;
            i32 z;
        }

        i32 main() {
            Vec3 v = { 1, 2, 3 };
            return v.x + v.y + v.z;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 6);
}

TEST(Struct, FieldAccessInExpression)
{
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { .x = 10, .y = 5 };
            return p.x - p.y;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Struct, StructAsParameter)
{
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 getX(Point p) {
            return p.x;
        }

        i32 main() {
            Point p = { .x = 123, .y = 456 };
            return getX(p);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 123);
}

TEST(Struct, SimpleBraceInitScalar)
{
    const auto source = R"(
        i32 main() {
            i32 x = { 42 };
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

// ========================
// impl blocks
// ========================

TEST(Impl, ConstFieldOnPrimitive)
{
    const auto source = R"(
        impl i32 {
            const i32 MAX_VALUE = 0x7FFFFFFF;
        }

        i32 main() {
            return i32.MAX_VALUE / 10000000;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 214);
}

TEST(Impl, ConstFieldWithArithmetic)
{
    const auto source = R"(
        impl i32 {
            const i32 BITS = 32;
            const i32 BYTES = 4;
        }

        i32 main() {
            return i32.BITS + i32.BYTES;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 36);
}

TEST(Impl, MethodOnStruct)
{
    const auto source = R"(
        struct Counter {
            i32 value;
        }

        impl Counter {
            i32 get() {
                return this.value;
            }
        }

        i32 main() {
            Counter c = { 42 };
            return c.get();
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Impl, ConstAndMethodTogether)
{
    const auto source = R"(
        struct Config {
            i32 timeout;
        }

        impl Config {
            const i32 DEFAULT_TIMEOUT = 30;

            i32 getTimeout() {
                return this.timeout;
            }
        }

        i32 main() {
            Config c = { Config.DEFAULT_TIMEOUT };
            return c.getTimeout();
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

TEST(Impl, StaticMethodOnPrimitive)
{
    const auto source = R"(
        impl i32 {
            public static i32 clamp(i32 val, i32 lo, i32 hi) {
                if (val < lo) { return lo; }
                if (val > hi) { return hi; }
                return val;
            }
        }

        i32 main() {
            return i32.clamp(150, 0, 100);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 100);
}