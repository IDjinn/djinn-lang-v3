#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

// ============================================================================
// Basic Ownership Tests - Copy Types
// ============================================================================

TEST(Ownership, CopyTypesDoNotMove)
{
    // Primitive types like i32 are copy types and should not be moved
    const auto source = R"(
        i32 main() {
            i32 x = 42;
            i32 y = x;  // Copy, not move
            return x + y;  // Both can be used
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 84);
}

TEST(Ownership, FloatCopyTypes)
{
    const auto source = R"(
        i32 main() {
            f64 x = 3.14;
            f64 y = x;
            i32 res = 1;
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(Ownership, PointerCopyTypes)
{
    const auto source = R"(
        i32 main() {
            i32 value = 10;
            i32* p1 = &value;
            i32* p2 = p1;  // Copy pointer, not move
            return *p1 + *p2;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 20);
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

TEST(Ownership, StructMoveSemantics)
{
    // Non-copy types (structs) should be moved
    const auto source = R"(
        struct Data {
            i32 value;
        }

        i32 main() {
            Data d1 = { .value = 100 };
            Data d2 = d1;  // Move d1 to d2
            return d2.value;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 100);
}

TEST(Ownership, UseAfterMove)
{
    // Using a moved value should produce an error
    const auto source = R"(
        struct Data {
            i32 value;
        }

        i32 main() {
            Data d1 = { .value = 42 };
            Data d2 = d1;  // Move d1 to d2
            return d1.value;  // Error: use after move
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(Ownership, DoubleMoveError)
{
    // Moving a variable twice should produce an error
    const auto source = R"(
        struct Data {
            i32 value;
        }

        i32 main() {
            Data d1 = { .value = 10 };
            Data d2 = d1;  // First move
            Data d3 = d1;  // Error: d1 already moved
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_GE(result.diagnostics.size(), 1);
}

// ============================================================================
// Reinitialize After Move
// ============================================================================

TEST(Ownership, ReinitializeAfterMove)
{
    // Assigning to a moved variable should reinitialize it
    const auto source = R"(
        struct Data {
            i32 value;
        }

        i32 main() {
            Data mut d1 = { .value = 10 };
            Data d2 = d1;  // Move d1 to d2
            d1 = { .value = 20 };  // Reinitialize d1
            return d1.value + d2.value;  // Both valid
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

// ============================================================================
// Scope Tests
// ============================================================================

TEST(Ownership, ScopeOwnership)
{
    const auto source = R"(
        i32 main() {
            i32 mut res = 0;
            {
                i32 x = 10;
                res = x;
            }
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(Ownership, NestedScopeOwnership)
{
    const auto source = R"(
        i32 main() {
            i32 mut res = 0;
            {
                i32 a = 1;
                {
                    i32 b = 2;
                    res = a + b;
                }
            }
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

// ============================================================================
// Function Parameter Tests
// ============================================================================

TEST(Ownership, FunctionParameterCopyType)
{
    const auto source = R"(
        i32 increment(i32 x) {
            return x + 1;
        }

        i32 main() {
            i32 val = 10;
            i32 res = increment(val);
            return val + res;  // val is still valid (copy type)
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 21);
}

// ============================================================================
// Control Flow Tests
// ============================================================================

TEST(Ownership, IfStatementOwnership)
{
    const auto source = R"(
        i32 main() {
            i32 x = 10;
            i32 mut res = 0;
            if (x > 5) {
                res = x;
            }
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(Ownership, WhileLoopOwnership)
{
    const auto source = R"(
        i32 main() {
            i32 mut counter = 0;
            i32 mut sum = 0;
            while (counter < 5) {
                sum = sum + counter;
                counter = counter + 1;
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10); // 0+1+2+3+4
}

TEST(Ownership, ForLoopOwnership)
{
    const auto source = R"(
        i32 main() {
            i32 mut sum = 0;
            for (i32 mut i = 0; i < 5; i = i + 1) {
                sum = sum + i;
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}