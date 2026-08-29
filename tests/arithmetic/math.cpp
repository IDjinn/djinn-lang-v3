//
// Created by Claude on 11/01/2026.
//

#include <gtest/gtest.h>

#include "DjinnCompiler.h"

// ============================================================================
// Math Library Tests - Using libc math functions
// ============================================================================

TEST(Math, Sqrt)
{
    const auto source = R"(
        extern "C" {
            f64 sqrt(f64 x);
        }

        i32 main() {
            f64 res = sqrt(16.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4);
}

TEST(Math, SqrtFloat)
{
    const auto source = R"(
        extern "C" {
            f32 sqrtf(f32 x);
        }

        i32 main() {
            f32 res = sqrtf(25.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Math, Pow)
{
    const auto source = R"(
        extern "C" {
            f64 pow(f64 base, f64 exponent);
        }

        i32 main() {
            f64 res = pow(2.0, 3.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 8);
}

TEST(Math, Floor)
{
    const auto source = R"(
        extern "C" {
            f64 floor(f64 x);
        }

        i32 main() {
            f64 res = floor(3.7);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(Math, Ceil)
{
    const auto source = R"(
        extern "C" {
            f64 ceil(f64 x);
        }

        i32 main() {
            f64 res = ceil(3.2);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4);
}

TEST(Math, Round)
{
    const auto source = R"(
        extern "C" {
            f64 round(f64 x);
        }

        i32 main() {
            f64 res = round(3.5);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4);
}

TEST(Math, Fabs)
{
    const auto source = R"(
        extern "C" {
            f64 fabs(f64 x);
        }

        i32 main() {
            f64 res = fabs(-42.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Math, Fmax)
{
    const auto source = R"(
        extern "C" {
            f64 fmax(f64 x, f64 y);
        }

        i32 main() {
            f64 res = fmax(10.0, 20.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 20);
}

TEST(Math, Fmin)
{
    const auto source = R"(
        extern "C" {
            f64 fmin(f64 x, f64 y);
        }

        i32 main() {
            f64 res = fmin(10.0, 5.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Math, Cbrt)
{
    const auto source = R"(
        extern "C" {
            f64 cbrt(f64 x);
        }

        i32 main() {
            f64 res = cbrt(27.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(Math, Exp)
{
    const auto source = R"(
        extern "C" {
            f64 exp(f64 x);
            f64 floor(f64 x);
        }

        i32 main() {
            f64 e_squared = exp(2.0);
            return floor(e_squared);
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 7); // floor(e^2) = floor(7.389...) = 7
}

TEST(Math, Log)
{
    const auto source = R"(
        extern "C" {
            f64 log(f64 x);
            f64 exp(f64 x);
            f64 round(f64 x);
        }

        i32 main() {
            f64 e = exp(1.0);
            f64 ln_e = log(e);
            return round(ln_e);
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1); // ln(e) = 1
}

TEST(Math, Log10)
{
    const auto source = R"(
        extern "C" {
            f64 log10(f64 x);
        }

        i32 main() {
            f64 res = log10(1000.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3); // log10(1000) = 3
}

TEST(Math, Log2)
{
    const auto source = R"(
        extern "C" {
            f64 log2(f64 x);
        }

        i32 main() {
            f64 res = log2(8.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3); // log2(8) = 3
}

TEST(Math, Fmod)
{
    const auto source = R"(
        extern "C" {
            f64 fmod(f64 x, f64 y);
        }

        i32 main() {
            f64 res = fmod(10.0, 3.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1); // 10 mod 3 = 1
}

TEST(Math, Hypot)
{
    const auto source = R"(
        extern "C" {
            f64 hypot(f64 x, f64 y);
        }

        i32 main() {
            f64 res = hypot(3.0, 4.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5); // sqrt(3^2 + 4^2) = 5
}

TEST(Math, Trunc)
{
    const auto source = R"(
        extern "C" {
            f64 trunc(f64 x);
        }

        i32 main() {
            f64 res = trunc(7.9);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 7);
}

TEST(Math, Exp2)
{
    const auto source = R"(
        extern "C" {
            f64 exp2(f64 x);
        }

        i32 main() {
            f64 res = exp2(4.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 16); // 2^4 = 16
}

// ============================================================================
// Math Library Import Tests - Using std::math
// ============================================================================

TEST(Math, ImportMathSqrt)
{
    const auto source = R"(
        import std::math;

        i32 main() {
            f64 res = sqrt(49.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 7);
}

TEST(Math, ImportMathPow)
{
    const auto source = R"(
        import std::math;

        i32 main() {
            f64 res = pow(3.0, 2.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 9);
}

TEST(Math, ImportMathMultipleFunctions)
{
    const auto source = R"(
        import std::math;

        i32 main() {
            f64 a = sqrt(16.0);
            f64 b = pow(2.0, 2.0);
            f64 c = fabs(-2.0);
            return a + b + c;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10); // 4 + 4 + 2 = 10
}

TEST(Math, ImportMathFloatFunctions)
{
    const auto source = R"(
        import std::math;

        i32 main() {
            f32 res = sqrtf(36.0);
            return res;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 6);
}

TEST(Math, CombinedMathOperations)
{
    const auto source = R"(
        import std::math;

        i32 main() {
            f64 x = 3.0;
            f64 y = 4.0;

            // Calculate hypotenuse using sqrt(x^2 + y^2)
            f64 h = sqrt(pow(x, 2.0) + pow(y, 2.0));
            return h;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}
