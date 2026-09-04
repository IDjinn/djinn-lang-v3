//
// Native exceptions mode (--exceptions): block-form try/catch/finally,
// invoke-based propagation and the shim entry points. These tests compile
// in-process and inspect the IR — actually running requires the AOT path
// (the JIT falls back to it in exceptions mode).
//

#include "DjinnCompiler.h"
#include "gtest/gtest.h"

namespace
{
    CompilerResult compileWith(const std::string& source, const bool exceptions)
    {
        return DjinnCompiler::run(source, {
                                      .optimizationLevel = 0, .generateBinary = false,
                                      .exceptions = exceptions
                                  });
    }

    bool hasDiagnostic(const CompilerResult& result, const uint32_t code)
    {
        for (const auto& diag : result.diagnostics)
        {
            if (diag.code == code) return true;
        }
        return false;
    }
}

TEST(NativeExceptions, TryCatchRequiresExceptionsMode)
{
    const auto source = R"(
        struct MyError : Exception;

        i32 main() throws {
            try {
                risky();
            } catch (MyError e) {
                return 1;
            }
            return 0;
        }

        i32 risky() throws(MyError) {
            throw MyError("boom");
        }
    )";

    const auto result = compileWith(source, false);
    EXPECT_TRUE(hasDiagnostic(result, 9009));
}

TEST(NativeExceptions, CatchArmMustBeErrorType)
{
    const auto source = R"(
        struct NotAnError;

        i32 main() throws {
            try {
                risky();
            } catch (NotAnError e) {
                return 1;
            }
            return 0;
        }

        i32 risky() throws {
            throw Generic("boom");
        }
    )";

    const auto result = compileWith(source, true);
    EXPECT_TRUE(hasDiagnostic(result, 9010));
}

TEST(NativeExceptions, TryWithoutCatchOrFinallyIsRejected)
{
    const auto source = R"(
        i32 main() throws {
            try {
                risky();
            }
            return 0;
        }

        i32 risky() throws {
            throw Generic("boom");
        }
    )";

    const auto result = compileWith(source, true);
    EXPECT_FALSE(result.diagnostics.empty());
}

TEST(NativeExceptions, IrUsesInvokeAndPersonality)
{
    const auto source = R"(
        struct MyError : Exception;

        i32 risky() throws(MyError) {
            throw MyError("boom");
        }

        i32 main() throws {
            try {
                risky();
            } catch (MyError e) {
                return 1;
            } catch (Error e) {
                return 2;
            } finally {
                Console.write("done");
            }
        }
    )";

    const auto result = compileWith(source, true);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("invoke"), std::string::npos);
    EXPECT_NE(result.ir.find("catchpad"), std::string::npos);
    EXPECT_NE(result.ir.find("catchswitch"), std::string::npos);
    EXPECT_NE(result.ir.find("__CxxFrameHandler3"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_throw"), std::string::npos);
    // The throwing function itself carries the personality (only extern
    // declarations keep nounwind in this mode)
    EXPECT_NE(result.ir.find("personality ptr @__CxxFrameHandler3"), std::string::npos);
}

TEST(NativeExceptions, TryExpressionLoweredToLanding)
{
    const auto source = R"(
        i32 risky() throws {
            throw Generic("boom");
        }

        i32 main() {
            i32 v = try risky() ?: -1;
            return v;
        }
    )";

    const auto result = compileWith(source, true);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("invoke"), std::string::npos);
    EXPECT_NE(result.ir.find("catchret"), std::string::npos);
}

TEST(NativeExceptions, UncaughtSyncMainIsWrapped)
{
    const auto source = R"(
        i32 inner() throws {
            throw Generic("boom");
        }

        i32 main() throws {
            inner();
            return 0;
        }
    )";

    const auto result = compileWith(source, true);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("__djinn_uncaught_error"), std::string::npos);
}

TEST(NativeExceptions, NonThrowingFunctionsStayNounwindFreeOfEh)
{
    const auto source = R"(
        i32 add(i32 a, i32 b) {
            return a + b;
        }

        i32 main() {
            return add(1, 2);
        }
    )";

    const auto result = compileWith(source, true);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.ir.find("invoke"), std::string::npos);
    EXPECT_EQ(result.ir.find("__djinn_throw"), std::string::npos);
    EXPECT_EQ(result.ir.find("catchswitch"), std::string::npos);
}
