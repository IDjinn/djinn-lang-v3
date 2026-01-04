#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Import, BasicImport) {
    const auto source = R"(
        import math;

        namespace math {
            i32 add(i32 a, i32 b) {
                return a + b;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->imports.size(), 1);
    EXPECT_EQ(result.program->imports[0]->namespacePath.toString(), "math");
}

TEST(Import, QualifiedImport) {
    const auto source = R"(
        import std::io;

        namespace std {
            namespace io {
                i32 dummy() {
                    return 0;
                }
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->imports.size(), 1);
    EXPECT_EQ(result.program->imports[0]->namespacePath.toString(), "std::io");
}

TEST(Import, MultipleImports) {
    const auto source = R"(
        import math;
        import utils;

        namespace math {
            i32 add(i32 a, i32 b) {
                return a + b;
            }
        }

        namespace utils {
            i32 identity(i32 x) {
                return x;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->imports.size(), 2);
}

TEST(Import, ImportParsesQualifiedName) {
    const auto source = R"(
        import a::b::c;
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->imports[0]->namespacePath.parts.size(), 3);
    EXPECT_EQ(result.program->imports[0]->namespacePath.parts[0], "a");
    EXPECT_EQ(result.program->imports[0]->namespacePath.parts[1], "b");
    EXPECT_EQ(result.program->imports[0]->namespacePath.parts[2], "c");
}