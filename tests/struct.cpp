#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Struct, Definition) {
    const auto source = R"(
        struct Result {
            i32 value;
        }

        void main() {
            Result result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->functions.size(), 1);
    EXPECT_EQ(result.program->structs.size(), 1);
}

TEST(Struct, BraceInitializerPositional) {
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

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->structs.size(), 1);
}

TEST(Struct, BraceInitializerDesignated) {
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

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->structs.size(), 1);
}

TEST(Struct, FieldAccess) {
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

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Struct, FieldAccessSecondField) {
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

TEST(Struct, MethodReturnWithBraceInit) {
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

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

TEST(Struct, AutoInferStructType) {
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

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.returnCode, 77);
}

TEST(Struct, MultipleFieldsPositional) {
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

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 6);
}

TEST(Struct, FieldAccessInExpression) {
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

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Struct, StructAsParameter) {
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

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 123);
}

TEST(Struct, SimpleBraceInitScalar) {
    const auto source = R"(
        i32 main() {
            i32 x = { 42 };
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}