//
// Polished runtime error reports: source location + caret, operand values
// (digit-grouped), variable assignment history and the native stack trace
// (captured at throw/trap sites, symbolized lazily) for trapped overflow and
// division by zero.
//

#include "DjinnCompiler.h"
#include "jit/JitRunner.h"
#include "gtest/gtest.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
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

TEST(RuntimeErrors, DebugInfoAndVarTrackingAppearInIr)
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
    EXPECT_NE(result.ir.find("!DISubprogram"), std::string::npos);
    EXPECT_NE(result.ir.find("!DILocation"), std::string::npos);
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
    // throw site (line 6); the throw site lives in the trace instead. JIT
    // traces symbolize names only (no debug objects in memory); AOT debug
    // builds add file:line via dbghelp/llvm-symbolizer.
    EXPECT_NE(report.find("--> main:12"), std::string::npos);
    EXPECT_NE(report.find("stack trace"), std::string::npos);
    EXPECT_NE(report.find("at division"), std::string::npos);
    EXPECT_NE(report.find("at main"), std::string::npos);
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
// drop the instrumentation that bloats binaries — debug metadata, variable
// tracking, throw-site trace captures and source-line snippets.
//

TEST(RuntimeErrors, ReleaseModeOmitsDebugInfoAndVarTracking)
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
    EXPECT_EQ(result.ir.find("!DISubprogram"), std::string::npos);
    EXPECT_EQ(result.ir.find("!DILocation"), std::string::npos);
    EXPECT_EQ(result.ir.find("__djinn_capture_backtrace"), std::string::npos);
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

namespace
{
    std::string read_file(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) return "";
        std::ostringstream buf;
        buf << file.rdbuf();
        return buf.str();
    }
}

// Debug builds split the human-facing dump from the symbol payload: main.ll
// is readable IR without !dbg metadata, main.debug.ll carries the metadata
// and is what clang compiles (-g -> CodeView/PDB).
TEST(RuntimeErrors, DebugBuildSplitsDebugInfoOutOfReadableIr)
{
    const auto source = R"(
        i32 main() {
            return 0;
        }
    )";

    std::filesystem::create_directories("build/test_ir");
    std::remove("build/test_ir/split_debug.ll");
    std::remove("build/test_ir/split_debug.debug.ll");

    const auto result = DjinnCompiler::run(source, {
                                               .optimizationLevel = 0, .generateBinary = false,
                                               .useTempDirectory = false,
                                               .outputDirectory = "build/test_ir",
                                               .outputFileName = "split_debug"
                                           });
    EXPECT_EQ(result.diagnostics.size(), 0);

    // result.ir keeps the full module for IR-level inspection
    EXPECT_NE(result.ir.find("!DISubprogram"), std::string::npos);

    const auto clean = read_file("build/test_ir/split_debug.ll");
    ASSERT_FALSE(clean.empty());
    EXPECT_NE(clean.find("define i32 @main"), std::string::npos);
    EXPECT_EQ(clean.find("!DISubprogram"), std::string::npos);
    EXPECT_EQ(clean.find("!DILocation"), std::string::npos);

    const auto debug = read_file("build/test_ir/split_debug.debug.ll");
    ASSERT_FALSE(debug.empty());
    EXPECT_NE(debug.find("!DISubprogram"), std::string::npos);
    EXPECT_NE(debug.find("define i32 @main"), std::string::npos);
}

// --embed-debug-info (splitDebugInfo = false): single .ll keeps the metadata,
// as before the split existed.
TEST(RuntimeErrors, EmbedDebugInfoKeepsSingleIrFile)
{
    const auto source = R"(
        i32 main() {
            return 0;
        }
    )";

    std::filesystem::create_directories("build/test_ir");

    const auto result = DjinnCompiler::run(source, {
                                               .optimizationLevel = 0, .generateBinary = false,
                                               .useTempDirectory = false,
                                               .outputDirectory = "build/test_ir",
                                               .outputFileName = "embed_debug",
                                               .splitDebugInfo = false
                                           });
    EXPECT_EQ(result.diagnostics.size(), 0);

    const auto ir = read_file("build/test_ir/embed_debug.ll");
    ASSERT_FALSE(ir.empty());
    EXPECT_NE(ir.find("define i32 @main"), std::string::npos);
    EXPECT_NE(ir.find("!DISubprogram"), std::string::npos);
}
