//
// Polished runtime error reports: source location + caret, operand values
// and the shadow-stack trace for trapped overflow and division by zero.
//

#include "DjinnCompiler.h"
#include "gtest/gtest.h"

namespace
{
    CompilerResult run(const std::string& source)
    {
        return DjinnCompiler::run(source, {.optimizationLevel = 0, .generateBinary = true});
    }

    CompilerResult compile(const std::string& source)
    {
        return DjinnCompiler::run(source, {.optimizationLevel = 0, .generateBinary = false});
    }
}

TEST(RuntimeErrors, TrappedOverflowReportHasLocationValuesAndTrace)
{
    const auto source = R"(
        i32 compute(i32t v) {
            i32t r = v + v;
            return r;
        }
        i32 main() {
            i32t x = 2000000000t;
            return compute(x);
        }
    )";

    const auto result = run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);

    const auto& report = result.runtimeErrorReport;
    EXPECT_NE(report.find("djinn runtime error: integer overflow"), std::string::npos);
    EXPECT_NE(report.find("--> main:"), std::string::npos);
    EXPECT_NE(report.find("v + v"), std::string::npos);       // snippet line
    EXPECT_NE(report.find("^^^"), std::string::npos);         // caret underline
    EXPECT_NE(report.find("2000000000 + 2000000000 = 4000000000 exceeds i32 max (2147483647)"),
              std::string::npos);
    EXPECT_NE(report.find("stack trace"), std::string::npos);
    EXPECT_NE(report.find("at compute"), std::string::npos);
    EXPECT_NE(report.find("at main"), std::string::npos);
}

TEST(RuntimeErrors, DivisionByZeroReportShowsOperands)
{
    const auto source = R"(
        i32 main() {
            i32 d = 0;
            i32 x = 10 / d;
            return x;
        }
    )";

    const auto result = run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);

    const auto& report = result.runtimeErrorReport;
    EXPECT_NE(report.find("djinn runtime error: division by zero"), std::string::npos);
    EXPECT_NE(report.find("--> main:"), std::string::npos);
    EXPECT_NE(report.find("10 / 0"), std::string::npos);
    EXPECT_NE(report.find("^^^"), std::string::npos);
}

TEST(RuntimeErrors, NegateOverflowReport)
{
    const auto source = R"(
        i32 main() {
            i32t m = -2147483647t;
            m = m - 1t;
            i32t r = -m;
            return r;
        }
    )";

    const auto result = run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);

    const auto& report = result.runtimeErrorReport;
    EXPECT_NE(report.find("integer overflow"), std::string::npos);
    EXPECT_NE(report.find("negate -2147483648 exceeds i32 max (2147483647)"), std::string::npos);
}

TEST(RuntimeErrors, StackTraceShowsIntermediateFrames)
{
    const auto source = R"(
        i32 inner(i32t v) {
            i32t r = v * v;
            return r;
        }
        i32 outer(i32t v) {
            return inner(v);
        }
        i32 main() {
            i32t x = 1000000000t;
            return outer(x);
        }
    )";

    const auto result = run(source);
    EXPECT_NE(result.returnCode, 0);

    const auto& report = result.runtimeErrorReport;
    EXPECT_NE(report.find("1000000000 * 1000000000 = 1000000000000000000 exceeds i32 max (2147483647)"),
              std::string::npos);
    // Innermost first, with call-site lines
    const auto atInner = report.find("at inner");
    const auto atOuter = report.find("at outer");
    const auto atMain = report.find("at main");
    ASSERT_NE(atInner, std::string::npos);
    ASSERT_NE(atOuter, std::string::npos);
    ASSERT_NE(atMain, std::string::npos);
    EXPECT_TRUE(atInner < atOuter && atOuter < atMain);
}

TEST(RuntimeErrors, ShadowStackCallsAppearInIr)
{
    const auto source = R"(
        i32 helper(i32t v) {
            return v;
        }
        i32 main() {
            return helper(1t);
        }
    )";

    const auto result = compile(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("__djinn_frame_push"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_frame_pop"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_frame_set_line"), std::string::npos);
}

TEST(RuntimeErrors, NormalRunHasEmptyReport)
{
    const auto source = R"(
        i32 main() {
            i32t ok = 100t + 23t;
            return ok;
        }
    )";

    const auto result = run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 123);
    EXPECT_TRUE(result.runtimeErrorReport.empty());
}
