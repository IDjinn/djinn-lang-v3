//
// Non-zero integer types: the 'n' suffix (i32n, u32n) rejects the value 0.
// Non-zero parameters behave as implicit require(p != 0) contracts: checked
// at entry (ContractViolation) and at call sites with constant arguments,
// which lets division-by-zero checks be elided for proven non-zero divisors.
//

#include "DjinnCompiler.h"
#include "TestHelpers.h"
#include "gtest/gtest.h"

namespace
{
    CompilerResult compile(const std::string& source)
    {
        return DjinnCompiler::run(source, {.optimizationLevel = 0, .generateBinary = false});
    }

    // LLVM IR of a single function (the module includes the whole std lib, so
    // assertions must be scoped to the function body)
    std::string functionIr(const std::string& ir, const std::string& funcName)
    {
        const auto begin = ir.find("define @" + funcName + "(");
        if (begin == std::string::npos) return "";
        const auto end = ir.find('}', begin);
        return ir.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    }
}

TEST(NonZero, DeclarationsCompile)
{
    const auto source = R"(
        i32 main() {
            i32n signedNonZero = 5;
            i32n negative = -5;
            u32n unsignedNonZero = 10;
            i64n wide = 3000000000;
            i32 back = signedNonZero;
            u32 backUnsigned = unsignedNonZero;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(NonZero, SameRepresentationAsBaseType)
{
    const auto source = R"(
        i32 main() {
            i32n a = 5;
            i32 b = a + 1;
            return b;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("add i32"), std::string::npos);
}

TEST(NonZero, RejectsZeroLiteral)
{
    const auto source = R"(
        i32 main() {
            i32n a = 0;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_NE(result.diagnostics[0].message.find("non-zero"), std::string::npos);
}

TEST(NonZero, RejectsZeroLiteralVariants)
{
    const std::string variants[] = {
        "i32n a = 0;",
        "u32n a = 0x0;",
        "u32n a = 0b0;",
        "u32n a = 0_0;",
        "i32n a = -0;",
    };

    for (const auto& variant : variants)
    {
        const auto source = "i32 main() { " + variant + " return 0; }";
        const auto result = compile(source);
        ASSERT_EQ(result.diagnostics.size(), 1) << "variant: " << variant;
        EXPECT_NE(result.diagnostics[0].message.find("non-zero"), std::string::npos) << "variant: " << variant;
    }
}

TEST(NonZero, RejectsImplicitConversionFromPlainType)
{
    const auto source = R"(
        i32 main() {
            i32 plain = 1;
            i32n a = plain;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_NE(result.diagnostics[0].message.find("explicit cast"), std::string::npos);
}

TEST(NonZero, ExplicitCastIsTrusted)
{
    const auto source = R"(
        i32 main() {
            i32 plain = 1;
            i32n trusted = (i32n)plain;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(NonZero, RejectsCastOfZeroLiteral)
{
    const auto source = R"(
        i32 main() {
            i32n a = (i32n)0;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_NE(result.diagnostics[0].message.find("non-zero"), std::string::npos);
}

TEST(NonZero, ArithmeticResultLosesGuarantee)
{
    const auto source = R"(
        i32 main() {
            i32n a = 5;
            i32n b = a + a;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_NE(result.diagnostics[0].message.find("non-zero"), std::string::npos);
}

TEST(NonZero, RejectsIncrementAndDecrement)
{
    const auto source = R"(
        i32 main() {
            mut i32n a = 5;
            a++;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_NE(result.diagnostics[0].message.find("non-zero"), std::string::npos);
}

TEST(NonZero, RejectsDeclarationWithoutInitializer)
{
    const auto source = R"(
        i32 main() {
            i32n a;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_NE(result.diagnostics[0].message.find("must be initialized"), std::string::npos);
}

TEST(NonZero, CombinesWithOverflowSuffix)
{
    const auto source = R"(
        i32 main() {
            i32nw a = 5;
            i32wn b = 6;
            u32nt c = 7;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(NonZero, RejectsSuffixOnFloatType)
{
    const auto source = R"(
        i32 main() {
            f32n a = 1.5;
            return 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
    EXPECT_NE(result.diagnostics[0].message.find("only valid on integer types"), std::string::npos);
}

TEST(NonZero, DivisionByNonZeroRuns)
{
    const auto source = R"(
        i32 main() {
            i32n divisor = 10;
            return 100 / divisor;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(NonZero, DivisionByConstantSkipsZeroCheck)
{
    const auto source = R"(
        i32 work() {
            return 100 / 10;
        }

        i32 main() {
            return work();
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    const auto ir = functionIr(result.ir, "work");
    EXPECT_NE(ir.find("sdiv"), std::string::npos);
    EXPECT_EQ(ir.find("div_zero"), std::string::npos);
}

TEST(NonZero, DivisionByUnknownStillChecked)
{
    const auto source = R"(
        i32 work(i32 b) {
            return 100 / b;
        }

        i32 main() {
            return work(7);
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    const auto ir = functionIr(result.ir, "work");
    EXPECT_NE(ir.find("div_zero"), std::string::npos);
}

TEST(NonZero, DivisionByNonZeroParamSkipsCheckAndChecksEntry)
{
    const auto source = R"(
        i32 work(i32n b) {
            return 100 / b;
        }

        i32 main() {
            return work(10);
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    const auto ir = functionIr(result.ir, "work");
    EXPECT_NE(ir.find("sdiv"), std::string::npos);
    EXPECT_EQ(ir.find("div_zero"), std::string::npos);
    EXPECT_NE(ir.find("nz_zero"), std::string::npos);
}

TEST(NonZero, RequireNonZeroDedupsEntryCheck)
{
    const auto source = R"(
        i32 work(i32 b)
            require(b != 0)
        {
            return 100 / b;
        }

        i32 main() {
            return try work(10) ?: 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    const auto ir = functionIr(result.ir, "work");
    EXPECT_EQ(ir.find("div_zero"), std::string::npos);
    EXPECT_NE(ir.find("nz_zero"), std::string::npos);
    EXPECT_EQ(ir.find("req_cond"), std::string::npos); // folded into the non-zero entry check
}

TEST(NonZero, RequireProvesParamNonZeroForAssignments)
{
    const auto source = R"(
        i32 work(i32 b)
            require(b != 0)
        {
            i32n proven = b;
            return 100 / b;
        }

        i32 main() {
            return try work(10) ?: 0;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(NonZero, CompileTimeViolationForNonZeroParam)
{
    const auto source = R"(
        i32 work(i32n b) {
            return b;
        }

        i32 main() {
            return work(0);
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(errorCount(result), 1);
    EXPECT_TRUE(hasErrorCode(result, 9007));
}

TEST(NonZero, CompileTimeViolationForRequireStillWorks)
{
    const auto source = R"(
        i32 work(i32 b)
            require(b != 0)
        {
            return 100 / b;
        }

        i32 main() {
            return work(0);
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(errorCount(result), 1);
    EXPECT_TRUE(hasErrorCode(result, 9007));
}

TEST(NonZero, NonZeroParamCallWithNonZeroConstantCompiles)
{
    const auto source = R"(
        i32 work(i32n b) {
            return 100 / b;
        }

        i32 main() {
            return work(10);
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(NonZero, EnsureProvesNonZeroReturn)
{
    const auto source = R"(
        i32 five()
            ensure(return != 0)
        {
            return 5;
        }

        i32 main() {
            i32n x = try five() ?: 1;
            return x;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(NonZero, EnsureWorksInAnyDeclarationOrder)
{
    const auto source = R"(
        i32 main() {
            i32n x = try five() ?: 1;
            return x;
        }

        i32 five()
            ensure(return != 0)
        {
            return 5;
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(NonZero, EntryCheckTrapsAtRuntime)
{
    const auto source = R"(
        i32 work(i32n b) {
            return 100 / b;
        }

        i32 main() {
            i32 zero = 0;
            i32n d = (i32n)zero;
            return work(d);
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}
