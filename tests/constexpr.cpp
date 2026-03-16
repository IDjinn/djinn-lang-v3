//
// Created by lucas on 15/03/2026.
//

#include "../DjinnCompiler.h"
#include "gtest/gtest.h"

TEST(ConstExpr, SimpleAddConstExpr)
{
    const auto source = R"(
        constexpr i31 add(i32 a, i32 b) {
            return a + b;
        }

        i32 main() {
            auto test = add(1, 2);
            return test;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(ConstExpr, InvalidConstExprAndAsyncModifier)
{
    const auto source = R"(
        constexpr async i31 add(i32 a, i32 b) {
            return a + b;
        }

        i32 main() {
            auto test = add(1, 2);
            return test;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
}