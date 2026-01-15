#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Hello, World) {
    const auto source = R"(
        extern "C" {
            i32 printf(i8* format, ...);
        }

        void main() {
            printf("hello world!");
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 12);
    EXPECT_EQ(result.program->functions.size(), 1);
    EXPECT_EQ(result.program->externFunctions.size(), 1);
}
