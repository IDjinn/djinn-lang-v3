#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(StaticVar, SimpleImmutable)
{
    const auto source = R"(
        static i32 value = 42;

        i32 main() {
            return value;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(StaticVar, Mutable)
{
    const auto source = R"(
        static mut i32 counter = 0;

        void increment() {
            counter = counter + 1;
        }

        i32 main() {
            increment();
            increment();
            increment();
            return counter;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(StaticVar, MultipleGlobals)
{
    const auto source = R"(
        static i32 a = 10;
        static i32 b = 20;
        static i32 c = 12;

        i32 main() {
            return a + b + c;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(StaticVar, MutableWithFunctions)
{
    const auto source = R"(
        static mut i32 state = 0;

        void set_state(i32 val) {
            state = val;
        }

        i32 get_state() {
            return state;
        }

        i32 main() {
            set_state(42);
            return get_state();
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(StaticVar, UsedInExpressions)
{
    const auto source = R"(
        static i32 base = 10;
        static i32 multiplier = 4;

        i32 compute() {
            return base * multiplier + 2;
        }

        i32 main() {
            return compute();
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(StaticVar, MutableAccumulator)
{
    const auto source = R"(
        static mut i32 total = 0;

        void add(i32 x) {
            total = total + x;
        }

        i32 main() {
            add(10);
            add(20);
            add(12);
            return total;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 42);
}
