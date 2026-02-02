#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Method, BasicMethodWithBlock) {
    const auto source = R"(
        struct Counter {
            i32 value;

            i32 getValue() {
                return this.value;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->structs.size(), 1);
    EXPECT_EQ(result.program->structs[0]->methods.size(), 1);
}

TEST(Method, ExpressionBodyArrow) {
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;

            i32 getX() => this.x;
            i32 getY() => this.y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->structs[0]->methods.size(), 2);
    EXPECT_TRUE(result.program->structs[0]->methods[0]->isExpressionBody());
    EXPECT_TRUE(result.program->structs[0]->methods[1]->isExpressionBody());
}

TEST(Method, MethodWithParameters) {
    const auto source = R"(
        struct Calculator {
            i32 result;

            i32 add(i32 a, i32 b) {
                return a + b;
            }

            i32 multiply(i32 a, i32 b) => a * b;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->structs[0]->methods.size(), 2);
    EXPECT_EQ(result.program->structs[0]->methods[0]->parameters.size(), 2);
}

TEST(Method, PublicPrivateModifiers) {
    const auto source = R"(
        struct Person {
            i32 age;

            public i32 getAge() => this.age;

            private void setAge(i32 newAge) {
                this.age = newAge;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->structs[0]->methods.size(), 2);
}

TEST(Method, StaticMethod) {
    const auto source = R"(
        struct Math {
            static i32 abs(i32 x) {
                if (x < 0) {
                    return 0 - x;
                }
                return x;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->structs[0]->methods.size(), 1);
}

TEST(Method, VoidMethodWithBlock) {
    const auto source = R"(
        struct Logger {
            i32 count;

            void log() {
                this.count = this.count + 1;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Method, MixedFieldsAndMethods) {
    const auto source = R"(
        struct User {
            i32 id;
            i32 age;

            i32 getId() => this.id;

            void setId(i32 newId) {
                this.id = newId;
            }

            i32 getAge() {
                return this.age;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->structs[0]->fields.size(), 2);
    EXPECT_EQ(result.program->structs[0]->methods.size(), 3);
}