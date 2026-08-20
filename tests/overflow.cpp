//
// Integer overflow modes: w (wrapped), t (trapped), c (checked), s (saturating)
//

#include "../DjinnCompiler.h"
#include "gtest/gtest.h"

namespace
{
    CompilerResult compile(const std::string& source)
    {
        return DjinnCompiler::run(source, {.optimizationLevel = 0, .generateBinary = false});
    }
}

TEST(Overflow, SuffixDeclarationsCompile)
{
    const auto source = R"(
        i32 main() throws {
            i32 common = 123;
            i32w wrapped = 121231234w;
            i32t trapped = 232134324t;
            i32c checked = 232343c;
            i32s saturation = 23498s;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Overflow, WrappedIsPlainAdd)
{
    const auto source = R"(
        i32 main() {
            i32w a = 1000w;
            i32w b = 23w;
            i32w c = a + b;
            return c;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("add i32"), std::string::npos);
    EXPECT_EQ(result.ir.find("with.overflow"), std::string::npos);
}

TEST(Overflow, TrappedEmitsOverflowCheck)
{
    const auto source = R"(
        i32 main() {
            i32t a = 2000000000t;
            i32t b = a + 1000000000;
            return b;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("llvm.sadd.with.overflow.i32"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_runtime_error"), std::string::npos);
}

TEST(Overflow, TrappedRuntimeOverflowAborts)
{
    const auto source = R"(
        i32 main() {
            i32t a = 2000000000t;
            i32t b = a + a;
            return b;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}

TEST(Overflow, CheckedRequiresThrows)
{
    const auto source = R"(
        i32 main() {
            i32c a = 1000c;
            i32c b = a + 32;
            return b;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
}

TEST(Overflow, CheckedWithThrowsSetsErrorFlag)
{
    const auto source = R"(
        i32 main() throws {
            i32c a = 1000c;
            i32c b = a + 32;
            return b;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("llvm.sadd.with.overflow.i32"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_error_flag"), std::string::npos);
}

TEST(Overflow, SaturatingUsesSatIntrinsic)
{
    const auto source = R"(
        i32 main() {
            i32s a = 1000s;
            i32s b = a + 32;
            return b;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("llvm.sadd.sat.i32"), std::string::npos);
}

TEST(Overflow, SaturatingRuntimeAddClampsToMax)
{
    const auto source = R"(
        i32 main() {
            i32s a = 2000000000s;
            i32s b = a + 2000000000;
            return b / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2); // i32.MAX_VALUE / 1e9
}

TEST(Overflow, SaturatingLiteralClampsAtCompileTime)
{
    const auto source = R"(
        i32 main() {
            i32s a = 3000000000s;
            return a / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2); // clamped to 2147483647
}

TEST(Overflow, TrappedLiteralOutOfRangeIsCompileError)
{
    const auto source = R"(
        i32 main() {
            i32t a = 3000000000t;
            return a;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
}

TEST(Overflow, WrappedLiteralOutOfRangeStillCompiles)
{
    const auto source = R"(
        i32 main() {
            i32w a = 3000000000w;
            return a;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Overflow, ConflictingModesAreRejected)
{
    const auto source = R"(
        i32 main() throws {
            i32w a = 1000w;
            i32t b = 2000t;
            i32 c = a + b;
            return c;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
}

TEST(Overflow, PostfixIncrementRespectsMode)
{
    const auto source = R"(
        i32 main() {
            mut i32s a = 2147483646s;
            a++;
            return a / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("llvm.sadd.sat.i32"), std::string::npos);
    EXPECT_EQ(result.returnCode, 2); // saturates at MAX_VALUE
}

TEST(Overflow, FloatTypeSuffixIsRejected)
{
    const auto source = R"(
        i32 main() {
            f32w a = 1.5;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
}

TEST(Overflow, WrappedLiteralKeepsCStyleValue)
{
    // 3000000000 wrapped into i32 is -1294967296
    const auto source = R"(
        i32 main() {
            i32w a = 3000000000w;
            return 0 - a;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1294967296);
}

TEST(Overflow, HexLiteralSuffixCompiles)
{
    // 0xFFFFFFFFw must parse as the wrapped hex value, not "0xFFFFFFFFw"
    const auto source = R"(
        i32 main() {
            i32w flags = 0xFFFFFFFFw;
            return 0 - flags;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(Overflow, HexDigitCIsNotASuffix)
{
    // 'c' is a hex digit: 0x1c == 28, never mode suffix 'c'
    const auto source = R"(
        i32 main() {
            i32 c = 0x1c;
            return c;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 28);
}

TEST(Overflow, BinaryLiteralSuffix)
{
    const auto source = R"(
        i32 main() {
            i32s b = 0b1010s;
            return b;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(Overflow, CompoundAssignmentSaturates)
{
    const auto source = R"(
        i32 main() {
            mut i32s a = 2000000000s;
            a += 1000000000;
            return a / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2); // saturates at i32.MAX_VALUE
}

TEST(Overflow, CompoundAssignmentTrappedAborts)
{
    const auto source = R"(
        i32 main() {
            mut i32t a = 2000000000t;
            a += a;
            return a;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}

TEST(Overflow, UnaryNegateSaturatesMinToMax)
{
    // -(-2147483648) overflows positively: saturates at MAX_VALUE
    const auto source = R"(
        i32 main() {
            i32s m = -2147483648s;
            i32s n = -m;
            return n / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(Overflow, UnaryNegateTrappedAborts)
{
    const auto source = R"(
        i32 main() {
            i32t m = -2147483648t;
            i32t n = -m;
            return n;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}

TEST(Overflow, DivIntMinMinusOneTrappedAborts)
{
    // INT_MIN / -1 is the only signed division overflow
    const auto source = R"(
        i32 main() {
            i32t m = -2147483648t;
            i32t q = m / -1t;
            return q;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}

TEST(Overflow, SaturatingDivIntMinClampsToMax)
{
    const auto source = R"(
        i32 main() {
            i32s m = -2147483648s;
            i32s q = m / -1s;
            return q / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(Overflow, SaturatingRemIntMinIsZero)
{
    // INT_MIN % -1 == 0 mathematically
    const auto source = R"(
        i32 main() {
            i32s m = -2147483648s;
            i32s r = m % -1s;
            return r + 5;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(Overflow, SaturatingSubClampsToMin)
{
    const auto source = R"(
        i32 main() {
            i32s a = -2000000000s;
            i32s b = a - 2000000000;
            return 0 - (b / 1000000000);
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2); // clamps at MIN_VALUE: -2147483648 / 1e9 = -2
}

TEST(Overflow, SaturatingMulClampsToMax)
{
    const auto source = R"(
        i32 main() {
            i32s a = 2000000000s;
            i32s b = a * 2;
            return b / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(Overflow, UnsignedSaturatingSubClampsToZero)
{
    const auto source = R"(
        i32 main() {
            mut u32s a = 0u;
            u32s b = a - 1u;
            return b + 7;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 7); // 0 - 1 saturates to 0
}

TEST(Overflow, UnsignedTrappedAddAborts)
{
    const auto source = R"(
        i32 main() {
            mut u32t a = 4294967295u;
            u32t b = a + 1u;
            return b;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}

TEST(Overflow, ConstexprSaturatingAdd)
{
    const auto source = R"(
        constexpr i32s a = 2000000000s + 2000000000s;

        i32 main() {
            return a / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2); // compile-time saturating add
}

TEST(Overflow, ConstevalTrappedOverflowIsError)
{
    const auto source = R"(
        consteval i32t x = 2000000000t + 1000000000t;

        i32 main() {
            return x;
        }
    )";

    const auto result = compile(source);
    bool hasError = false;
    for (const auto& d : result.diagnostics)
    {
        if (d.severity == Severity::Error)
        {
            hasError = true;
            break;
        }
    }
    EXPECT_TRUE(hasError);
}

TEST(Overflow, NestedModePropagationSaturates)
{
    const auto source = R"(
        i32 main() {
            i32s a = 1000000000s;
            i32s b = (a + a) + a;
            return b / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2); // 3e9 saturates at MAX_VALUE
}

TEST(Overflow, CheckedOverflowCaughtByTryFallback)
{
    const auto source = R"(
        i32 safeAdd(i32c a, i32c b) throws {
            return a + b;
        }

        i32 main() {
            i32 result = try safeAdd(2147483647c, 1c) ?: -1;
            return 0 - result;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1); // overflow -> fallback -1 -> 0 - (-1)
}

TEST(Overflow, CheckedNoOverflowReturnsValue)
{
    const auto source = R"(
        i32 safeAdd(i32c a, i32c b) throws {
            return a + b;
        }

        i32 main() {
            i32 result = try safeAdd(1000c, 32c) ?: -1;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1032);
}

TEST(Overflow, ModeIsNotTypeIdentity)
{
    // modes annotate behavior, not identity: assignments between them are free
    const auto source = R"(
        i32 main() {
            i32s a = 100s;
            i32 b = a;
            i32w c = b;
            return c;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 100);
}

TEST(Overflow, StructFieldWithMode)
{
    const auto source = R"(
        struct Packet {
            i32t checksum;
            i32s size;
        }

        i32 main() {
            Packet p = Packet { .checksum = 1000t, .size = 42s };
            return p.size;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}
