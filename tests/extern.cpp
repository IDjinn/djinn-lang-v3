#include <gtest/gtest.h>
#include "../DjinnCompiler.h"

TEST(Extern, PrintfHelloWorld) {
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        void main() {
            printf("Hello from extern!\n");
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.returnCode, 19);
    EXPECT_EQ(result.program->externFunctions.size(), 1);
    EXPECT_EQ(result.program->externFunctions[0]->name, "printf");
    EXPECT_EQ(result.program->externFunctions[0]->isVariadic, true);
}

TEST(Extern, BlockSyntax) {
    const std::string source = R"(
        extern "C" {
            i32 printf(i8* format, ...);
            i32 puts(i8* s);
        }

        void main() {
            printf("Hello %s!\n", "World");
            puts("From extern block");
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->externFunctions.size(), 2);
    EXPECT_EQ(result.program->externFunctions[0]->name, "printf");
    EXPECT_EQ(result.program->externFunctions[1]->name, "puts");
}

TEST(Extern, MultipleExternBlocks) {
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        extern "C" {
            i32 puts(i8* s);
        }

        void main() {
            printf("Test 1\n");
            puts("Test 2");
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->externFunctions.size(), 2);
}