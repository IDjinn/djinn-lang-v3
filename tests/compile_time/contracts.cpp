#include <gtest/gtest.h>

#include "DjinnCompiler.h"

//
// Contracts: require/ensure clauses. A violated contract throws the builtin
// ContractViolation error, so call sites must use `try`.
//

TEST(Contracts, RequirePasses)
{
    const auto source = R"(
        i32 my_abs(i32 value)
            require(value > -100000)
        {
            return value < 0 ? -value : value;
        }

        i32 main() {
            i32 result = try my_abs(-5) ?: 0;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Contracts, RequireViolationThrowsContractViolation)
{
    const auto source = R"(
        i32 my_abs(i32 value)
            require(value > 0)
        {
            return value < 0 ? -value : value;
        }

        i32 main() {
            i32 result = try my_abs(-5) ?: 99;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Contracts, EnsurePasses)
{
    const auto source = R"(
        i32 double_it(i32 value)
            ensure(return == value * 2)
        {
            return value * 2;
        }

        i32 main() {
            i32 result = try double_it(21) ?: 0;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Contracts, EnsureViolationThrowsContractViolation)
{
    const auto source = R"(
        i32 broken(i32 value)
            ensure(return == 12345)
        {
            return value;
        }

        i32 main() {
            i32 result = try broken(1) ?: 77;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 77);
}

TEST(Contracts, MultipleClauses)
{
    const auto source = R"(
        i32 clamp(i32 value, i32 lo, i32 hi)
            require(lo <= hi)
            require(value >= lo)
            ensure(return >= lo)
            ensure(return <= hi)
        {
            return value;
        }

        i32 main() {
            i32 ok = try clamp(5, 1, 10) ?: 0;
            i32 violated = try clamp(0, 1, 10) ?: -2;
            return ok + violated;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(Contracts, RequireBlockForm)
{
    const auto source = R"(
        i32 safe_div(i32 a, i32 b)
            require { return b != 0; }
        {
            return a / b;
        }

        i32 main() {
            i32 ok = try safe_div(10, 2) ?: 0;
            i32 violated = try safe_div(1, 0) ?: -4;
            return ok + violated;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(Contracts, ContractCallSiteRequiresTry)
{
    // Contracts make the function throwing: calling without try is an error
    const auto source = R"(
        i32 my_abs(i32 value)
            require(value != 0)
        {
            return value < 0 ? -value : value;
        }

        i32 main() {
            i32 result = my_abs(5);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {});
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(Contracts, ContractViolationPropagatesThroughThrows)
{
    // caller declares throws and lets the ContractViolation propagate
    const auto source = R"(
        i32 checked(i32 value)
            require(value < 100)
        {
            return value;
        }

        i32 caller(i32 value) throws {
            i32 v = try checked(value);
            return v;
        }

        i32 main() {
            i32 violated = try caller(500) ?: 31;
            return violated;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 31);
}
