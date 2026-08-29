#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "DjinnCompiler.h"

//
// Contracts: require/ensure clauses. A violated contract throws the builtin
// ContractViolation error, so call sites must use `try`.
//
// Compile-time enforcement (default ErrorEnforcement::CompileTime): a call
// whose require clauses are decided by constant arguments is checked by the
// compiler — an unhandled provable violation is a compile error (9007), a
// handled one (inside `try ... ?:`) is a warning (9008) because the fallback
// is always taken.
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
    EXPECT_EQ(errorCount(result), 0);
    // Provable violation with a constant argument, but handled by `try`:
    // warn that the fallback is always taken
    EXPECT_EQ(warningCount(result), 1);
    EXPECT_TRUE(hasErrorCode(result, 9008));
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
    // ensure stays a runtime check (evaluating it would require running the body)
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
    EXPECT_EQ(errorCount(result), 0);
    EXPECT_EQ(warningCount(result), 1); // clamp(0, 1, 10) violates require(value >= lo)
    EXPECT_TRUE(hasErrorCode(result, 9008));
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
    EXPECT_EQ(errorCount(result), 0);
    EXPECT_EQ(warningCount(result), 1); // safe_div(1, 0) violates the block-form require
    EXPECT_TRUE(hasErrorCode(result, 9008));
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
    EXPECT_TRUE(hasErrorCode(result, 9001));
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

    // Non-constant argument (caller parameter): no compile-time analysis
    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 31);
}

TEST(Contracts, RequireConstantViolationIsCompileError)
{
    // Unchecked call in a throwing caller: propagation would be allowed, but
    // the require clause is provably violated by the constant argument
    const auto source = R"(
        i32 checked(i32 value)
            require(value < 100)
        {
            return value;
        }

        i32 main() throws {
            i32 v = checked(500);
            return v;
        }
    )";

    const auto result = DjinnCompiler::run(source, {});
    EXPECT_EQ(errorCount(result), 1);
    EXPECT_TRUE(hasErrorCode(result, 9007));
}

TEST(Contracts, RuntimeArgumentsSkipCompileTimeAnalysis)
{
    // Same require, but the argument is a runtime value: the check must stay
    // at runtime and no warning may be emitted
    const auto source = R"(
        static mut i32 runtime_input = 500;

        i32 checked(i32 value)
            require(value < 100)
        {
            return value;
        }

        i32 main() throws {
            i32 v = try checked(runtime_input) ?: -6;
            return v;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(-6));
}

TEST(Contracts, EnforcementRuntimeKeepsOldBehavior)
{
    // Runtime level: no compile-time analysis; propagation is allowed
    const auto source = R"(
        i32 checked(i32 value)
            require(value < 100)
        {
            return value;
        }

        i32 main() throws {
            i32 v = checked(500);
            return v;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.errorEnforcement = ErrorEnforcement::Runtime});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Contracts, EnforcementStrictRequiresTryEverywhere)
{
    // Strict level: even a throwing caller must be explicit about error flow
    const auto source = R"(
        i32 checked(i32 value)
            require(value < 100)
        {
            return value;
        }

        i32 middle(i32 value) throws {
            i32 v = checked(value);
            return v;
        }

        i32 main() {
            i32 result = try middle(5) ?: 0;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.errorEnforcement = ErrorEnforcement::Strict});
    EXPECT_GE(errorCount(result), 1);
    EXPECT_TRUE(hasErrorCode(result, 9001));
}

TEST(Contracts, EnforcementStrictAcceptsBareTryPropagation)
{
    const auto source = R"(
        i32 checked(i32 value)
            require(value < 100)
        {
            return value;
        }

        i32 middle(i32 value) throws {
            i32 v = try checked(value);
            return v;
        }

        i32 main() {
            i32 result = try middle(5) ?: 0;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.generateBinary = true, .errorEnforcement = ErrorEnforcement::Strict});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Contracts, EnforcementOffDisablesAllChecks)
{
    const auto source = R"(
        i32 checked(i32 value)
            require(value < 100)
        {
            return value;
        }

        i32 main() {
            i32 v = checked(500);
            return v;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.errorEnforcement = ErrorEnforcement::Off});
    EXPECT_EQ(result.diagnostics.size(), 0);
}
