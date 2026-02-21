#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(IndexAccess, BasicRead)
{
    const auto source = R"(
        i32 main() {
            i32* data = malloc(12);
            data[0] = 10;
            data[1] = 20;
            data[2] = 30;
            i32 val = data[1];
            free(data);
            return val;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 20);
}

TEST(IndexAccess, Write)
{
    const auto source = R"(
        i32 main() {
            i32* data = malloc(12);
            data[0] = 42;
            i32 val = data[0];
            free(data);
            return val;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(IndexAccess, WithMalloc)
{
    const auto source = R"(
        i32 main() {
            i32* nums = malloc(20);
            for (mut i32 i = 0; i < 5; i = i + 1) {
                nums[i] = i * 10;
            }
            i32 res = nums[3];
            free(nums);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

TEST(IndexAccess, I8Pointer)
{
    const auto source = R"(
        i32 main() {
            i8* buf = malloc(4);
            buf[0] = 65;
            buf[1] = 66;
            i32 val = buf[0];
            free(buf);
            return val;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 65);
}