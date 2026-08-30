//
// Polished runtime error reports: source location + caret, operand values
// (digit-grouped), variable assignment history and the shadow-stack trace
// for trapped overflow and division by zero.
//

#include "DjinnCompiler.h"
#include "jit/JitRunner.h"
#include "gtest/gtest.h"
#include <cstdlib>

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

    CompilerResult runRelease(const std::string& source)
    {
        return DjinnCompiler::run(source, {.optimizationLevel = 0, .generateBinary = true,
                                           .debugMode = false, .releaseMode = true});
    }

    CompilerResult compileRelease(const std::string& source)
    {
        return DjinnCompiler::run(source, {.optimizationLevel = 0, .generateBinary = false,
                                           .debugMode = false, .releaseMode = true});
    }

    // The report is captured by the in-process JIT runner; ASan builds disable
    // the JIT and fall back to spawning clang, where no capture is available.
    bool reportCaptureAvailable()
    {
        return djinn::jitRuntimeAvailable() && std::getenv("DJINN_DISABLE_JIT") == nullptr;
    }
}

TEST(RuntimeErrors, TrappedOverflowReportHasLocationValuesAndTrace)
{
    if (!reportCaptureAvailable()) GTEST_SKIP() << "JIT report capture disabled in this build";

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
    EXPECT_NE(report.find("2.000.000.000 + 2.000.000.000 = 4.000.000.000 exceeds i32 max (2.147.483.647)"),
              std::string::npos);
    EXPECT_NE(report.find("stack trace"), std::string::npos);
    EXPECT_NE(report.find("at compute"), std::string::npos);
    EXPECT_NE(report.find("at main"), std::string::npos);
}

TEST(RuntimeErrors, DivisionByZeroReportShowsOperands)
{
    if (!reportCaptureAvailable()) GTEST_SKIP() << "JIT report capture disabled in this build";

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
    if (!reportCaptureAvailable()) GTEST_SKIP() << "JIT report capture disabled in this build";

    const auto source = R"(
        i32 main() {
            mut i32t m = -2147483647t;
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
    EXPECT_NE(report.find("negate -2.147.483.648 exceeds i32 max (2.147.483.647)"), std::string::npos);
}

TEST(RuntimeErrors, VariableHistoryShowsLastAssignments)
{
    if (!reportCaptureAvailable()) GTEST_SKIP() << "JIT report capture disabled in this build";

    const auto source = R"(
        i32 main() {
            mut i32t a = 2t;
            a = 2000000000t;
            i32t b = a + 1000000000;
            return b;
        }
    )";

    const auto result = run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);

    const auto& report = result.runtimeErrorReport;
    EXPECT_NE(report.find("history of 'a'"), std::string::npos);
    EXPECT_NE(report.find("i32t a = 2t;"), std::string::npos);
    EXPECT_NE(report.find("a = 2000000000t;"), std::string::npos);
    EXPECT_NE(report.find("2.000.000.000 + 1.000.000.000 = 3.000.000.000 exceeds i32 max (2.147.483.647)"),
              std::string::npos);
}

TEST(RuntimeErrors, StackTraceShowsIntermediateFrames)
{
    if (!reportCaptureAvailable()) GTEST_SKIP() << "JIT report capture disabled in this build";

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
    EXPECT_NE(report.find(
                  "1.000.000.000 * 1.000.000.000 = 1.000.000.000.000.000.000 exceeds i32 max (2.147.483.647)"),
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
    EXPECT_NE(result.ir.find("__djinn_var_track"), std::string::npos);
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

//
// Uncaught djinn exception escaping main() throws: the generated main wrapper
// reports and aborts instead of exiting silently with the default value.
//

TEST(RuntimeErrors, UncaughtExceptionEscapingMainReportsAndAborts)
{
    if (!reportCaptureAvailable()) GTEST_SKIP() << "JIT report capture disabled in this build";

    const auto source = R"(
        struct DivisionByZeroException : Exception;

        i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
            if (divisor == 0) {
                throw DivisionByZeroException("Division {value}/0 is not allowed");
            }
            return value / divisor;
        }

        i32 main() throws {
            i32 test = division(1, 0);
            return test;
        }
    )";

    const auto result = run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);

    const auto& report = result.runtimeErrorReport;
    EXPECT_NE(report.find("uncaught exception escaped 'main'"), std::string::npos);
    EXPECT_NE(report.find("DivisionByZeroException: Division 1/0 is not allowed"), std::string::npos);
    // The origin rises to the outermost unhandled call (line 12), not the
    // throw site (line 6); the throw site lives in the trace instead
    EXPECT_NE(report.find("--> main:12"), std::string::npos);
    EXPECT_NE(report.find("stack trace"), std::string::npos);
    EXPECT_NE(report.find("at division (main:6)"), std::string::npos);
    EXPECT_NE(report.find("at main (main:12)"), std::string::npos);
    const auto atDivision = report.find("at division");
    const auto atMain = report.find("at main");
    ASSERT_NE(atDivision, std::string::npos);
    ASSERT_NE(atMain, std::string::npos);
    EXPECT_TRUE(atDivision < atMain);
}

TEST(RuntimeErrors, UncaughtExceptionReleaseReportHasNoStackTrace)
{
    if (!reportCaptureAvailable()) GTEST_SKIP() << "JIT report capture disabled in this build";

    const auto source = R"(
        struct DivisionByZeroException : Exception;

        i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
            if (divisor == 0) {
                throw DivisionByZeroException("Division {value}/0 is not allowed");
            }
            return value / divisor;
        }

        i32 main() throws {
            try division(1, 0);
            return 0;
        }
    )";

    const auto result = runRelease(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);

    const auto& report = result.runtimeErrorReport;
    EXPECT_NE(report.find("uncaught exception escaped 'main'"), std::string::npos);
    EXPECT_NE(report.find("DivisionByZeroException: Division 1/0 is not allowed"), std::string::npos);
    // Origin rises to the unhandled try call (line 12) even in release; the
    // trace section is debug-only
    EXPECT_NE(report.find("--> main:12"), std::string::npos);
    EXPECT_EQ(report.find("stack trace"), std::string::npos);
}

//
// Release (minimal) diagnostics: traps keep file:line + operand values but
// drop the instrumentation that bloats binaries — shadow-stack frames,
// variable tracking and source-line snippets.
//

TEST(RuntimeErrors, ReleaseModeOmitsShadowStackAndVarTracking)
{
    const auto source = R"(
        i32 helper(i32t v) {
            return v;
        }
        i32 main() {
            i32t x = 1t;
            x = 2t;
            return helper(x);
        }
    )";

    const auto result = compileRelease(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.ir.find("__djinn_frame_push"), std::string::npos);
    EXPECT_EQ(result.ir.find("__djinn_frame_pop"), std::string::npos);
    EXPECT_EQ(result.ir.find("__djinn_frame_set_line"), std::string::npos);
    EXPECT_EQ(result.ir.find("__djinn_var_track"), std::string::npos);
}

TEST(RuntimeErrors, ReleaseModeTrapUsesMinimalCall)
{
    const auto source = R"(
        i32 main() {
            i32 d = 0;
            i32 x = 10 / d;
            return x;
        }
    )";

    const auto result = compileRelease(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("__djinn_runtime_error_min"), std::string::npos);
    EXPECT_EQ(result.ir.find("@__djinn_runtime_error("), std::string::npos);
}

TEST(RuntimeErrors, ReleaseModeTrapReportKeepsLocationAndValues)
{
    if (!reportCaptureAvailable()) GTEST_SKIP() << "JIT report capture disabled in this build";

    const auto source = R"(
        i32 main() {
            i32 d = 0;
            i32 x = 10 / d;
            return x;
        }
    )";

    const auto result = runRelease(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);

    const auto& report = result.runtimeErrorReport;
    EXPECT_NE(report.find("djinn runtime error: division by zero"), std::string::npos);
    EXPECT_NE(report.find("--> main:"), std::string::npos);
    EXPECT_NE(report.find("division by zero: 10 / 0"), std::string::npos);
    EXPECT_EQ(report.find("^^^"), std::string::npos);             // no source snippet
    EXPECT_EQ(report.find("history of"), std::string::npos);      // no variable history
    EXPECT_EQ(report.find("stack trace"), std::string::npos);     // no shadow frames
}
