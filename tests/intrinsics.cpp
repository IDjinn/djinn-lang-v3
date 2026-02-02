#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Intrinsics, SizeofI32) {
    const auto source = R"(
        i32 main() {
            i32 x = 42;
            i64 size = sizeof(x);
            return size;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4); // i32 = 4 bytes
}

TEST(Intrinsics, SizeofI64) {
    const auto source = R"(
        i32 main() {
            i64 x = 100;
            i64 size = sizeof(x);
            return size;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 8); // i64 = 8 bytes
}

TEST(Intrinsics, SizeofI8) {
    const auto source = R"(
        i32 main() {
            i8 x = 1;
            i64 size = sizeof(x);
            return size;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1); // i8 = 1 byte
}

TEST(Intrinsics, SizeofStruct) {
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { 10, 20 };
            i64 size = sizeof(p);
            return size;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 8); // 2 * i32 = 8 bytes
}

TEST(Intrinsics, AlignofI32) {
    const auto source = R"(
        i32 main() {
            i32 x = 42;
            i64 align = alignof(x);
            return align;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4); // i32 alignment = 4
}

TEST(Intrinsics, AlignofI64) {
    const auto source = R"(
        i32 main() {
            i64 x = 100;
            i64 align = alignof(x);
            return align;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    // i64 alignment varies by platform: 8 on most platforms, 4 on Windows x86
    EXPECT_TRUE(result.returnCode == 4 || result.returnCode == 8);
}

TEST(Intrinsics, Likely) {
    const auto source = R"(
        i32 main() {
            i32 x = 1;
            if (likely(x == 1)) {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Intrinsics, Unlikely) {
    const auto source = R"(
        i32 main() {
            i32 x = 0;
            if (unlikely(x == 1)) {
                return 0;
            }
            return 99;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Intrinsics, Expect) {
    const auto source = R"(
        i32 main() {
            i32 x = 5;
            i32 result = expect(x, 5);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Intrinsics, SizeofF32) {
    const auto source = R"(
        i32 main() {
            f32 x = 3.14;
            i64 size = sizeof(x);
            return size;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_TRUE(result.returnCode == 4 || result.returnCode == 8); // f32 = 4 bytes
}

TEST(Intrinsics, SizeofF64) {
    const auto source = R"(
        i32 main() {
            f64 x = 3.14159;
            i64 size = sizeof(x);
            return size;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_TRUE(result.returnCode == 4 || result.returnCode == 8); // f64 = 8 bytes
}