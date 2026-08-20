//
// Native-width types: nint (pointer-sized integer), nfloat (C float), ndouble (C double)
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

TEST(NativeTypes, DeclarationsCompile)
{
    const auto source = R"(
        i32 main() {
            nint count = 42;
            nfloat ratio = 1.5;
            ndouble precise = 2.5;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(NativeTypes, NintIsPointerSized)
{
    const auto source = R"(
        i32 main() {
            nint count = 42;
            return sizeof(count);
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, sizeof(void*)); // 8 on 64-bit targets
}

TEST(NativeTypes, NfloatIsF32)
{
    const auto source = R"(
        i32 main() {
            nfloat a = 1.5;
            f32 b = a + 2.5;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("float"), std::string::npos);
}

TEST(NativeTypes, NdoubleIsF64)
{
    const auto source = R"(
        i32 main() {
            ndouble a = 1.5;
            f64 b = a + 2.5;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("double"), std::string::npos);
}

TEST(NativeTypes, NintMixesWithI64)
{
    const auto source = R"(
        i32 main() {
            nint a = 100;
            i64 b = 9000000000;
            nint c = a + 1;
            i64 d = b + a;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(NativeTypes, TypeofShowsNativeName)
{
    const auto source = R"(
        i32 main() {
            nint count = 42;
            str t = typeof(count);
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("nint"), std::string::npos);
}

TEST(NativeTypes, NintMaxValueConstant)
{
    const auto source = R"(
        i32 main() {
            nint m = nint.MAX_VALUE;
            return m / 1000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 9); // 9223372036854775807 / 1e9
}

TEST(NativeTypes, NintTakesOverflowSuffixes)
{
    const auto source = R"(
        i32 main() {
            nintt a = 100t;
            nints b = a + 200;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("llvm.sadd.with.overflow.i64"), std::string::npos);
}

TEST(NativeTypes, NfloatTypeofShowsNativeName)
{
    const auto source = R"(
        i32 main() {
            nfloat ratio = 1.5;
            str t = typeof(ratio);
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("nfloat"), std::string::npos);
}

TEST(NativeTypes, NdoubleTypeofShowsNativeName)
{
    const auto source = R"(
        i32 main() {
            ndouble precise = 2.5;
            str t = typeof(precise);
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("ndouble"), std::string::npos);
}

TEST(NativeTypes, NintSaturatingArithmetic)
{
    const auto source = R"(
        i32 main() {
            nints a = 9223372036854775800s;
            nints b = a + a;
            return b / 1000000000000000000;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 9); // saturates at nint.MAX_VALUE (i64)
}

TEST(NativeTypes, NintNegation)
{
    const auto source = R"(
        i32 main() {
            nint x = 5;
            nint y = -x;
            return 0 - y;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(NativeTypes, NintAsFunctionSignature)
{
    const auto source = R"(
        nint offset(nint base, nint delta) {
            return base + delta;
        }

        i32 main() {
            nint r = offset(40, 2);
            return r;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(NativeTypes, NfloatNdoubleArithmetic)
{
    const auto source = R"(
        i32 main() {
            nfloat a = 1.5;
            nfloat b = a + 2.5;
            ndouble c = 1.5;
            ndouble d = c + b;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}
