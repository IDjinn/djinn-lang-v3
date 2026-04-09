#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Namespace, BasicDefinition)
{
    const auto source = R"(
        namespace math {
            i32 add(i32 a, i32 b) {
                return a + b;
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->namespaces.size(), 1);
    EXPECT_EQ(result.program->namespaces[0]->name.token_name, "math");
}

TEST(Namespace, StructInNamespace)
{
    const auto source = R"(
        namespace geom {
            struct Point {
                i32 x;
                i32 y;
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->namespaces.size(), 1);
    EXPECT_EQ(result.program->namespaces[0]->structs.size(), 1);
}

TEST(Namespace, NestedNamespace)
{
    const auto source = R"(
        namespace std {
            namespace io {
                i32 dummy() {
                    return 42;
                }
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->namespaces.size(), 1);
    EXPECT_EQ(result.program->namespaces[0]->namespaces.size(), 1);
    EXPECT_EQ(result.program->namespaces[0]->namespaces[0]->name.token_name, "io");
}

TEST(Namespace, MultipleFunctionsInNamespace)
{
    const auto source = R"(
        namespace math {
            i32 add(i32 a, i32 b) {
                return a + b;
            }

            i32 sub(i32 a, i32 b) {
                return a - b;
            }

            i32 mul(i32 a, i32 b) {
                return a * b;
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->namespaces[0]->functions.size(), 3);
}

TEST(Namespace, StructAndFunctionInNamespace)
{
    const auto source = R"(
        namespace geom {
            struct Vector {
                f32 x;
                f32 y;
            }

            f32 length() {
                return 0;
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->namespaces[0]->structs.size(), 1);
    EXPECT_EQ(result.program->namespaces[0]->functions.size(), 1);
}

TEST(Namespace, FileScopedNamespace)
{
    const auto source = R"(
        namespace myapp;

        i32 helper() {
            return 42;
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->fileNamespace, "myapp");
    EXPECT_TRUE(result.program->hasFileNamespace());
}

TEST(Namespace, FileScopedQualifiedNamespace)
{
    const auto source = R"(
        namespace myapp::utils;

        i32 helper() {
            return 42;
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->fileNamespace, "myapp::utils");
}

TEST(Namespace, NoNamespaceIsGlobal)
{
    const auto source = R"(
        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_FALSE(result.program->hasFileNamespace());
    EXPECT_EQ(result.program->fileNamespace, "");
}