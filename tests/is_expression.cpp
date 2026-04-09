//
// Tests for 'is' expression — runtime type checking for boxed object values
//

#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

// ============================================================================
// Basic 'is' expression tests
// ============================================================================

TEST(IsExpression, ObjectIsI32True)
{
    const auto source = R"(
        struct Inspector {
            public static i32 check(...args) {
                object first = args[0];
                if (first is i32) {
                    return 1;
                }
                return 0;
            }
        }

        i32 main() {
            return Inspector.check(42);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(IsExpression, ObjectIsI32False)
{
    const auto source = R"(
        struct Inspector {
            public static i32 check(...args) {
                object first = args[0];
                if (first is i32) {
                    return 1;
                }
                return 0;
            }
        }

        i32 main() {
            return Inspector.check(3.14);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(IsExpression, ObjectIsF64)
{
    const auto source = R"(
        struct Inspector {
            public static i32 check(...args) {
                object first = args[0];
                if (first is f64) {
                    return 1;
                }
                return 0;
            }
        }

        i32 main() {
            return Inspector.check(3.14);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(IsExpression, ObjectIsStr)
{
    const auto source = R"(
        struct Inspector {
            public static i32 check(...args) {
                object first = args[0];
                if (first is str) {
                    return 1;
                }
                return 0;
            }
        }

        i32 main() {
            return Inspector.check("hello");
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

// ============================================================================
// Negation
// ============================================================================

TEST(IsExpression, NegatedIsExpression)
{
    const auto source = R"(
        struct Inspector {
            public static i32 check(...args) {
                object first = args[0];
                if (!(first is i32)) {
                    return 1;
                }
                return 0;
            }
        }

        i32 main() {
            return Inspector.check(3.14);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

// ============================================================================
// Error: 'is' on non-object type
// ============================================================================

TEST(IsExpression, ErrorOnNonObjectOperand)
{
    const auto source = R"(
        i32 main() {
            i32 x = 42;
            if (x is i32) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_GT(result.diagnostics.size(), 0);
}

// ============================================================================
// Multiple type checks
// ============================================================================

TEST(IsExpression, MultipleTypeChecks)
{
    const auto source = R"(
        struct Inspector {
            public static i32 type_id(...args) {
                object first = args[0];
                if (first is i32) {
                    return 1;
                }
                if (first is f64) {
                    return 2;
                }
                if (first is str) {
                    return 3;
                }
                return 0;
            }
        }

        i32 main() {
            return Inspector.type_id("test");
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(IsExpression, IRContainsIsCheck)
{
    const auto source = R"(
        struct Inspector {
            public static i32 check(...args) {
                object first = args[0];
                if (first is i32) {
                    return 1;
                }
                return 0;
            }
        }

        i32 main() {
            return Inspector.check(42);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("is_check"), std::string::npos) << "IR should contain is_check comparison";
}

// ============================================================================
// Variable binding: obj is Type varName
// ============================================================================

TEST(IsExpression, BindingExtractsI32Value)
{
    const auto source = R"(
        struct Inspector {
            public static i32 check(...args) {
                object first = args[0];
                if (first is i32 value) {
                    return value;
                }
                return -1;
            }
        }

        i32 main() {
            return Inspector.check(42);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(IsExpression, BindingNotUsedWhenTypeMismatch)
{
    const auto source = R"(
        struct Inspector {
            public static i32 check(...args) {
                object first = args[0];
                if (first is i32 value) {
                    return value;
                }
                return -1;
            }
        }

        i32 main() {
            return Inspector.check(3.14);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, -1);
}

TEST(IsExpression, BindingWithDifferentTypes)
{
    const auto source = R"(
        struct Inspector {
            public static i32 identify(...args) {
                object first = args[0];
                if (first is i32 intVal) {
                    return intVal;
                }
                if (first is i64 longVal) {
                    return (i32)longVal;
                }
                return 0;
            }
        }

        i32 main() {
            return Inspector.identify(99);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}
