#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Intrinsics, SizeofI32)
{
    const auto source = R"(
        i32 main() {
            i32 x = 42;
            i64 sz = sizeof(x);
            return sz;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4); // i32 = 4 bytes
}

TEST(Intrinsics, SizeofI64)
{
    const auto source = R"(
        i32 main() {
            i64 x = 100;
            i64 sz = sizeof(x);
            return sz;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 8); // i64 = 8 bytes
}

TEST(Intrinsics, SizeofI8)
{
    const auto source = R"(
        i32 main() {
            i8 x = 1;
            i64 sz = sizeof(x);
            return sz;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1); // i8 = 1 byte
}

TEST(Intrinsics, SizeofStruct)
{
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { 10, 20 };
            i64 sz = sizeof(p);
            return sz;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 8); // 2 * i32 = 8 bytes
}

TEST(Intrinsics, AlignofI32)
{
    const auto source = R"(
        i32 main() {
            i32 x = 42;
            i64 align = alignof(x);
            return align;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4); // i32 alignment = 4
}

TEST(Intrinsics, AlignofI64)
{
    const auto source = R"(
        i32 main() {
            i64 x = 100;
            i64 align = alignof(x);
            return align;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    // i64 alignment varies by platform: 8 on most platforms, 4 on Windows x86
    EXPECT_TRUE(result.returnCode == 4 || result.returnCode == 8);
}

TEST(Intrinsics, Likely)
{
    const auto source = R"(
        i32 main() {
            i32 x = 1;
            if (likely(x == 1)) {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Intrinsics, Unlikely)
{
    const auto source = R"(
        i32 main() {
            i32 x = 0;
            if (unlikely(x == 1)) {
                return 0;
            }
            return 99;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Intrinsics, Expect)
{
    const auto source = R"(
        i32 main() {
            i32 x = 5;
            i32 res = expect(x, 5);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Intrinsics, SizeofF32)
{
    const auto source = R"(
        i32 main() {
            f32 x = 3.14;
            i64 sz = sizeof(x);
            return sz;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4); // f32 = 4 bytes
}

TEST(Intrinsics, SizeofF64)
{
    const auto source = R"(
        i32 main() {
            f64 x = 3.14159;
            i64 sz = sizeof(x);
            return sz;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 8); // f64 = 8 bytes
}

// sizeof on monomorphized generic struct instances
// Note: Type<Args>.staticMethod() syntax not yet supported by parser,
// so we test sizeof on instances of monomorphized generic structs instead.

TEST(Intrinsics, SizeofGenericStructI32)
{
    const auto source = R"(
        struct Container<T> {
            T value;
        }

        i32 main() {
            Container<i32> c = { 42 };
            i64 sz = sizeof(c);
            return sz;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4); // sizeof(Container<i32>) = sizeof(i32) = 4
}

TEST(Intrinsics, SizeofGenericStructI64)
{
    const auto source = R"(
        struct Container<T> {
            T value;
        }

        i32 main() {
            Container<i64> c = { 100 };
            i64 sz = sizeof(c);
            return sz;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 8); // sizeof(Container<i64>) = sizeof(i64) = 8
}

TEST(Intrinsics, SizeofGenericStructPoint)
{
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        struct Container<T> {
            T value;
        }

        i32 main() {
            Container<Point> c = { { 1, 2 } };
            i64 sz = sizeof(c);
            return sz;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 8); // sizeof(Container<Point>) = sizeof(Point) = 2*i32 = 8
}

// typeof intrinsic tests

TEST(Intrinsics, TypeofI32)
{
    const auto source = R"(
        import std::types;

        i32 main() {
            i32 x = 42;
            str t = typeof(x);
            return t.length;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3); // "i32" = 3 chars
}

TEST(Intrinsics, TypeofF64)
{
    const auto source = R"(
        import std::types;

        i32 main() {
            f64 x = 3.14;
            str t = typeof(x);
            return t.length;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3); // "f64" = 3 chars
}

TEST(Intrinsics, TypeofStruct)
{
    const auto source = R"(
        import std::types;

        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { 10, 20 };
            str t = typeof(p);
            return t.length;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5); // "Point" = 5 chars
}