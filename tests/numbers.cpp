//
// Created by lucas on 15/03/2026.
//

#include "../DjinnCompiler.h"
#include "gtest/gtest.h"

TEST(Numbers, MultipleNumbersDeclaration)
{
    const auto source = R"(
        async i32 main() {
            i32 first = 69;
            i32 second = 420_000;
            i32 third = 800'000'000;
            f32 forth = 1e9;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .generateBinary = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}
