#include <gtest/gtest.h>
#include "../DjinnCompiler.h"

TEST(Extern, PrintfHelloWorld)
{
    const std::string source = R"(
        extern i32 printf(i8* format, ...);

        void main() {
            printf("Hello from extern!\n");
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = false});
    EXPECT_EQ(result.program->externFunctions.size(), 1);
}

TEST(Extern, BlockSyntax)
{
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

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = false});
    EXPECT_EQ(result.program->externFunctions.size(), 2);
}

TEST(Extern, MultipleExternBlocks)
{
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

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = false});
    EXPECT_EQ(result.program->externFunctions.size(), 2);
}

TEST(Extern, StdImportNoDuplicateExternSymbols)
{
    const std::string source = R"(
        import std::types;
        import std::sys;
        import std::libc;
        import std::collections;

        namespace test_extern;

        i32 main() {
            printf("hello %d\n", 42);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);

    // Verify no numeric suffixes on extern functions in the IR (e.g., printf.50, malloc.51)
    EXPECT_EQ(result.ir.find("@printf."), std::string::npos) <<
        "printf has numeric suffix in IR — duplicate extern declaration";
    EXPECT_EQ(result.ir.find("@malloc."), std::string::npos) <<
        "malloc has numeric suffix in IR — duplicate extern declaration";
    EXPECT_EQ(result.ir.find("@free."), std::string::npos) <<
        "free has numeric suffix in IR — duplicate extern declaration";
    EXPECT_EQ(result.ir.find("@strlen."), std::string::npos) <<
        "strlen has numeric suffix in IR — duplicate extern declaration";
    EXPECT_EQ(result.ir.find("@realloc."), std::string::npos) <<
        "realloc has numeric suffix in IR — duplicate extern declaration";

    // Verify the correct names exist
    EXPECT_NE(result.ir.find("@printf"), std::string::npos) << "printf should be declared in IR";
    EXPECT_NE(result.ir.find("@malloc"), std::string::npos) << "malloc should be declared in IR";
}

TEST(Extern, ArrayWithStdImportsLinksCorrectly)
{
    const std::string source = R"(
        import std::types;
        import std::collections;
        import std::sys;
        import std::libc;

        namespace test_array;

        i32 main() {
            array<i32> array = array<i32>();
            array.push(42);
            printf("val = %d\n", array.get(0));
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);

    // No suffixed extern names
    EXPECT_EQ(result.ir.find("@malloc."), std::string::npos) << "malloc has numeric suffix — duplicate extern";
    EXPECT_EQ(result.ir.find("@realloc."), std::string::npos) << "realloc has numeric suffix — duplicate extern";
    EXPECT_EQ(result.ir.find("@free."), std::string::npos) << "free has numeric suffix — duplicate extern";
}