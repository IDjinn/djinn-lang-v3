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
            return s.length;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Str, EmptyLiteral)
{
    const auto source = R"(

        i32 main() {
            str s = "";
            return s.length;
        }
    )";

    const auto result = DjinnCompiler::run(source);
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

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Str, DirectLiteralCoercion)
{
    const auto source = R"(
        i32 main() {
            printf("direct: %s\n", "world");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Str, LenField)
{
    const auto source = R"(

        i32 main() {
            str s = "abcdefghij";
            return s.length;
        }
    )";

    const auto result = DjinnCompiler::run(source);
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

    const auto result = DjinnCompiler::run(source);
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

    const auto result = DjinnCompiler::run(source);
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

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 65); // 'A' = 65
}

TEST(Str, PassAsParameter)
{
    const auto source = R"(

        i32 getLen(str s) {
            return s.length;
        }

        i32 main() {
            str s = "test";
            return getLen(s);
        }
    )";

    const auto result = DjinnCompiler::run(source);
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
            return nums.length;
        }
    )";

    const auto result = DjinnCompiler::run(source);
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

    const auto result = DjinnCompiler::run(source);
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

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Arr, LenField)
{
    const auto source = R"(

        i32 main() {
            i32[] nums = [1, 2, 3, 4, 5];
            return nums.length;
        }
    )";

    const auto result = DjinnCompiler::run(source);
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

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Arr, SumElements)
{
    const auto source = R"(

        i32 main() {
            i32[] nums = [10, 20, 30, 40];
            mut i32 sum = 0;
            for (mut u32 i = 0; i < nums.length; i = i + 1u) {
                sum = sum + nums[i];
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 100);
}

TEST(Arr, WithPrintf)
{
    const auto source = R"(
        i32 main() {
            i32[] nums = [5, 10, 15];
            printf("len: %d\n", nums.length);
            return nums[2];
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 15);
}

// ========================
// type[length] (fixed-size stack array)
// ========================

TEST(FixedArray, BasicAllocation)
{
    const auto source = R"(
        i32 main() {
            i8* buf = i8[64];
            buf[0] = 42;
            return (i32)buf[0];
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(FixedArray, WriteAndReadMultiple)
{
    const auto source = R"(
        i32 main() {
            i32* arr = i32[10];
            arr[0] = 10;
            arr[1] = 20;
            arr[2] = 30;
            return arr[0] + arr[1] + arr[2];
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 60);
}

TEST(FixedArray, WithConstSize)
{
    const auto source = R"(
        const i32 SIZE = 8;
        i32 main() {
            i32* data = i32[SIZE];
            data[7] = 77;
            return data[7];
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 77);
}

TEST(FixedArray, LoopFill)
{
    const auto source = R"(
        i32 main() {
            i32* arr = i32[5];
            for (mut i32 i = 0; i < 5; i = i + 1) {
                arr[i] = i * 10;
            }
            return arr[3];
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

TEST(FixedArray, LargeBuffer)
{
    const auto source = R"(
        i32 main() {
            i8* buf = i8[8192];
            buf[0] = 1;
            buf[8191] = 99;
            return (i32)buf[8191];
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

// ========================
// string (heap string struct)
// ========================

TEST(String, StructDefinition)
{
    const auto source = R"(

        i32 main() {
            string s = { .data = 0, .length = 0, .capacity = 0 };
            return s.length;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(String, IsEmptyMethod)
{
    const auto source = R"(

        i32 main() {
            string s = { .data = 0, .length = 0, .capacity = 0 };
            if (s.is_empty()) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(String, CapacityField)
{
    const auto source = R"(

        i32 main() {
            string s = { .data = 0, .length = 5, .capacity = 16 };
            return s.capacity;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 16);
}