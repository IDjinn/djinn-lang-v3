#include <gtest/gtest.h>

#include "../DjinnCompiler.h"
#include "../binder/Binder.h"

TEST(Binder, AllowsValidCode) {
    const auto source = R"(
        i32 add(i32 a, i32 b) {
            return a + b;
        }

        i32 main() {
            i32 x = 10;
            i32 y = 20;
            return add(x, y);
        }
    )";

    const auto result = DjinnCompiler::run(source, {
                                               .optimize = false, .useTempDirectory = true, .runAfterCompile = true
                                           });
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

TEST(Binder, VariableDeclarationAndUsage) {
    const auto source = R"(
        i32 main() {
            i32 x = 42;
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .useTempDirectory = true, .runAfterCompile = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Binder, FunctionCallWithCorrectArgs) {
    const auto source = R"(
        i32 sum(i32 a, i32 b, i32 c) {
            return a + b + c;
        }

        i32 main() {
            return sum(1, 2, 3);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .useTempDirectory = true, .runAfterCompile = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 6);
}

TEST(Binder, NamespaceQualifiedNames) {
    const auto source = R"(
        namespace math {
            i32 add(i32 a, i32 b) {
                return a + b;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .useTempDirectory = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Binder, NestedNamespaceQualifiedNames) {
    const auto source = R"(
        namespace std {
            namespace io {
                i32 read() {
                    return 0;
                }
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .useTempDirectory = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Binder, FileScopedNamespaceBinding) {
    const auto source = R"(
        namespace mylib;

        i32 helper() {
            return 10;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .useTempDirectory = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Binder, StructFieldAccess) {
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;
        }

        i32 main() {
            Point p = { .x = 5, .y = 10 };
            return p.x + p.y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .useTempDirectory = true, .runAfterCompile = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 15);
}

TEST(Binder, MultipleVariables) {
    const auto source = R"(
        i32 main() {
            i32 a = 1;
            i32 b = 2;
            i32 c = 3;
            return a + b + c;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .useTempDirectory = true, .runAfterCompile = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 6);
}