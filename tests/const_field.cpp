#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(ConstField, BasicConstFieldStaticAccess)
{
    const auto source = R"(
        struct Config {
            const i32 MAX_SIZE = 100;
        }

        i32 main() {
            return Config.MAX_SIZE;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 100);
}

TEST(ConstField, ConstFieldUsedInStaticMethod)
{
    const auto source = R"(
        struct Math {
            const i32 ZERO = 0;

            public static i32 getZero() {
                return ZERO;
            }
        }

        i32 main() {
            return Math.getZero();
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(ConstField, ConstFieldWithPublicModifier)
{
    const auto source = R"(
        struct Limits {
            public const i32 MIN = 1;
            public const i32 MAX = 42;
        }

        i32 main() {
            return Limits.MAX;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(ConstField, ConstFieldDoesNotAffectStructLayout)
{
    const auto source = R"(
        struct Data {
            i32 x;
            const i32 MAGIC = 7;
            i32 y;
        }

        i32 main() {
            Data d = { 10, 20 };
            return d.y;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 20);
}

TEST(ConstField, ConstFieldAccessViaInstance)
{
    const auto source = R"(
        struct Config {
            i32 value;
            const i32 DEFAULT = 5;
        }

        i32 main() {
            Config c = { 10 };
            return c.DEFAULT;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(ConstField, ConstFieldNegativeValue)
{
    const auto source = R"(
        struct Constants {
            const i32 NEG = -1;
        }

        i32 main() {
            i32 x = 10 + Constants.NEG;
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 9);
}

TEST(ConstField, MutAndConstError)
{
    const auto source = R"(
        struct Bad {
            mut const i32 X = 10;
        }

        void main() {}
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_GT(result.diagnostics.size(), 0);
}

TEST(ConstField, ConstFieldWithoutInitializerError)
{
    const auto source = R"(
        struct Bad {
            const i32 X;
        }

        void main() {}
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_GT(result.diagnostics.size(), 0);
}

TEST(ConstField, MultipleConstFields)
{
    const auto source = R"(
        struct Direction {
            const i32 UP = 0;
            const i32 DOWN = 1;
            const i32 LEFT = 2;
            const i32 RIGHT = 3;
        }

        i32 main() {
            return Direction.RIGHT;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(ConstField, ConstFieldWithMixedRegularFields)
{
    const auto source = R"(
        struct Player {
            const i32 MAX_HP = 100;
            i32 hp;
            i32 x;
            i32 y;
        }

        i32 main() {
            Player p = { 50, 10, 20 };
            return Player.MAX_HP;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 100);
}
