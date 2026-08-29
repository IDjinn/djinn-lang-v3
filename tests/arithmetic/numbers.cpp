//
// Created by lucas on 15/03/2026.
//

#include "DjinnCompiler.h"
#include "gtest/gtest.h"

TEST(Numbers, MultipleNumbersDeclaration)
{
    const auto source = R"(
        async i32 main() {
            i32 first = 69;
            i32 second = 420_000;
            i32 third = 800'000'000;
            f32 forth = 1e9;

            i32 hexa = 0x93fa34;
            i32 bin = 0b001010;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .generateBinary = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}


TEST(Numbers, CompileTimeOverflowCheck)
{
    const auto source = R"(
        void main() {
            i8 first = 128; // up to 127
            i16 second = 32_768; // up to 32,767
            i32 third =  2_147_483_648; // up to  2,147,483,647
            i64 forth = 9'223'372'036'854'775'809; // up to 9,223,372,036,854,775,808
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .generateBinary = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}
