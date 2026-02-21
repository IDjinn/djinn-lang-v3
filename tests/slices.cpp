//
// Tests for str, arr<T>, and string slice/struct types
//

#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

// ========================
// str (string slice)
// ========================

TEST(Str, LiteralCreatesStrStruct)
{
    const auto source = R"(
        i32 main() {
            str s = "hello";
            return s.len;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Str, EmptyLiteral)
{
    const auto source = R"(

        i32 main() {
            str s = "";
            return s.len;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Str, CoercionToI8PtrForPrintf)
{
    const auto source = R"(
        i32 main() {
            str s = "hello";
            printf("%s", s);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Str, DirectLiteralCoercion)
{
    const auto source = R"(

        extern "C" {
            i32 printf(i8* format, ...);
        }

        i32 main() {
            printf("direct: %s\n", "world");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Str, LenField)
{
    const auto source = R"(

        i32 main() {
            str s = "abcdefghij";
            return s.len;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(Str, IsEmptyMethodTrue)
{
    const auto source = R"(

        i32 main() {
            str s = "";
            if (s.is_empty()) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(Str, IsEmptyMethodFalse)
{
    const auto source = R"(

        i32 main() {
            str s = "hello";
            if (s.is_empty()) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Str, IndexAccess)
{
    const auto source = R"(

        i32 main() {
            str s = "ABCDE";
            i32 ch = s[0];
            return ch;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 65); // 'A' = 65
}

TEST(Str, PassAsParameter)
{
    const auto source = R"(

        i32 getLen(str s) {
            return s.len;
        }

        i32 main() {
            str s = "test";
            return getLen(s);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4);
}

// ========================
// arr<T> (array slice)
// ========================

TEST(Arr, LiteralCreatesArrStruct)
{
    const auto source = R"(

        i32 main() {
            i32[] nums = [10, 20, 30];
            return nums.len;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(Arr, IndexRead)
{
    const auto source = R"(

        i32 main() {
            i32[] nums = [10, 20, 30];
            return nums[1];
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 20);
}

TEST(Arr, IndexWrite)
{
    const auto source = R"(

        i32 main() {
            i32[] nums = [1, 2, 3];
            nums[0] = 99;
            return nums[0];
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Arr, LenField)
{
    const auto source = R"(

        i32 main() {
            i32[] nums = [1, 2, 3, 4, 5];
            return nums.len;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Arr, IsEmptyMethodFalse)
{
    const auto source = R"(

        i32 main() {
            i32[] nums = [42];
            if (nums.is_empty()) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Arr, SumElements)
{
    const auto source = R"(

        i32 main() {
            i32[] nums = [10, 20, 30, 40];
            mut i32 sum = 0;
            for (mut i32 i = 0; i < nums.len; i = i + 1) {
                sum = sum + nums[i];
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 100);
}

TEST(Arr, WithPrintf)
{
    const auto source = R"(
        i32 main() {
            i32[] nums = [5, 10, 15];
            printf("len: %d\n", nums.len);
            return nums[2];
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 15);
}

// ========================
// string (heap string struct)
// ========================

TEST(String, StructDefinition)
{
    const auto source = R"(

        i32 main() {
            string s = { .data = 0, .len = 0, .capacity = 0 };
            return s.len;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(String, IsEmptyMethod)
{
    const auto source = R"(

        i32 main() {
            string s = { .data = 0, .len = 0, .capacity = 0 };
            if (s.is_empty()) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(String, CapacityField)
{
    const auto source = R"(

        i32 main() {
            string s = { .data = 0, .len = 5, .capacity = 16 };
            return s.capacity;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 16);
}
