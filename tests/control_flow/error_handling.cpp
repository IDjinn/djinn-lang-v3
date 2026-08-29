#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "DjinnCompiler.h"

//
// Error handling: throw / throws / try ?: fallback, builtin error types,
// error inheritance/alias and compiler enforcement (diagnostics 9xxx).
//

TEST(ErrorHandling, ThrowWithTryFallback)
{
    const auto source = R"(
        struct DivisionByZeroException : Argument;

        i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
            if (divisor == 0) {
                throw DivisionByZeroException("Division by zero is not allowed");
            }
            return value / divisor;
        }

        i32 main() {
            i32 result = try division(1, 0) ?: -1;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(-1));
}

TEST(ErrorHandling, TryReturnsValueOnSuccess)
{
    const auto source = R"(
        struct DivisionByZeroException : Argument;

        i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
            if (divisor == 0) {
                throw DivisionByZeroException("Division by zero is not allowed");
            }
            return value / divisor;
        }

        i32 main() {
            i32 result = try division(10, 2) ?: -1;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(ErrorHandling, BuiltinExceptionAvailableWithoutStd)
{
    // No import: builtin errors (Exception, DivisionByZero, ...) are core types
    const auto source = R"(
        i32 fail() throws(DivisionByZero) {
            throw DivisionByZero("boom");
        }

        i32 main() {
            i32 result = try fail() ?: 42;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(ErrorHandling, RethrowsPropagation)
{
    // inner() re-throws to main by declaring throws; main catches with try
    const auto source = R"(
        i32 inner() throws(Generic) {
            throw Generic("inner failure");
        }

        i32 middle() throws {
            // try without fallback in a throws function: re-throw to main
            i32 value = try inner();
            return value;
        }

        i32 main() {
            i32 result = try middle() ?: 7;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 7);
}

TEST(ErrorHandling, UncheckedThrowingCallInThrowingCallerPropagates)
{
    // middle() calls inner() WITHOUT try — the error must propagate to main
    const auto source = R"(
        i32 inner() throws(Generic) {
            throw Generic("inner failure");
        }

        i32 middle() throws(Generic) {
            i32 value = inner();
            return value;
        }

        i32 main() {
            i32 result = try middle() ?: 9;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 9);
}

TEST(ErrorHandling, DerivedErrorAcceptedByBaseThrowsClause)
{
    // throws(Exception) must accept any derived error (inheritance)
    const auto source = R"(
        struct MyCustomError : Exception;

        i32 fail() throws(Exception) {
            throw MyCustomError("custom failure");
        }

        i32 main() {
            i32 result = try fail() ?: 11;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 11);
}

TEST(ErrorHandling, DerivedErrorAcceptedByIntermediateBase)
{
    // MyError derives from Argument; throws(Argument) accepts it
    const auto source = R"(
        struct MyError : Argument;

        i32 fail() throws(Argument) {
            throw MyError("bad argument");
        }

        i32 main() {
            i32 result = try fail() ?: 13;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 13);
}

TEST(ErrorHandling, ErrorAliasChain)
{
    // Alias: AliasError derives from the user error MyError (chain of 2)
    const auto source = R"(
        struct MyError : Exception;
        struct AliasError : MyError;

        i32 fail() throws(MyError) {
            throw AliasError("aliased");
        }

        i32 main() {
            i32 result = try fail() ?: 21;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 21);
}

TEST(ErrorHandling, DivisionByZeroThrowsInThrowingFunction)
{
    // No explicit throw: checked division raises DivisionByZero automatically
    const auto source = R"(
        i32 division(i32 value, i32 divisor) throws(DivisionByZero) {
            return value / divisor;
        }

        i32 main() {
            i32 result = try division(1, 0) ?: -3;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(-3));
}

TEST(ErrorHandling, InterpolatedErrorMessage)
{
    // Interpolated message: parser desugars "{value}" into ("{0}", value);
    // the message must be formatted at runtime via Console.format
    const auto source = R"(
        i32 division(i32 value, i32 divisor) throws(DivisionByZero) {
            if (divisor == 0) {
                throw DivisionByZero("Division {value}/0 is not allowed");
            }
            return value / divisor;
        }

        i32 main() {
            i32 result = try division(1, 0) ?: -1;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(-1));
}

TEST(ErrorHandling, ConsoleFormatBuildsString)
{
    const auto source = R"(
        import std::sys;

        i32 main() {
            str formatted = Console.format("v={0}", 7);
            str expected = "v=7";
            if (formatted == expected) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(ErrorHandling, MissingTryDiagnostic)
{
    const auto source = R"(
        i32 fail() throws(Generic) {
            throw Generic("boom");
        }

        i32 main() {
            i32 result = fail();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {});
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(ErrorHandling, ThrowOutsideThrowsDiagnostic)
{
    const auto source = R"(
        i32 main() {
            throw Generic("boom");
        }
    )";

    const auto result = DjinnCompiler::run(source, {});
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(ErrorHandling, ThrowsTypeMismatchDiagnostic)
{
    // OutOfBounds does not derive from Argument — must be rejected
    const auto source = R"(
        i32 fail() throws(Argument) {
            throw OutOfBounds("not allowed here");
        }

        i32 main() {
            i32 result = try fail() ?: 0;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {});
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(ErrorHandling, ThrowNonErrorTypeDiagnostic)
{
    const auto source = R"(
        struct NotAnError {
            i32 code;
        }

        i32 fail() throws {
            throw NotAnError();
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {});
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(ErrorHandling, TryOnNonThrowingDiagnostic)
{
    const auto source = R"(
        i32 compute() {
            return 1;
        }

        i32 main() {
            i32 result = try compute() ?: 0;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {});
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(ErrorHandling, TryWithoutFallbackOutsideThrowsDiagnostic)
{
    const auto source = R"(
        i32 fail() throws(Generic) {
            throw Generic("boom");
        }

        i32 main() {
            i32 result = try fail();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {});
    EXPECT_GE(result.diagnostics.size(), 1);
}

//
// Compile-time enforcement: a constexpr call with constant arguments that
// provably throws is a compile error (9006) when unhandled, and a warning
// (9008) inside `try ... ?:` because the fallback is always taken.
//

TEST(ErrorHandling, ConstexprCallThatAlwaysThrowsIsCompileError)
{
    // The compiler evaluates division(1, 0) at compile time: divisor == 0 is
    // decided, so the throw is unavoidable — even though main declares throws
    const auto source = R"(
        struct DivisionByZeroException : Exception;

        constexpr i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
            if (unlikely(divisor == 0)) {
                throw DivisionByZeroException("Division {value}/0 is not allowed");
            }
            return value / divisor;
        }

        i32 main() throws {
            i32 test = division(1, 0);
            return test;
        }
    )";

    const auto result = DjinnCompiler::run(source, {});
    EXPECT_EQ(errorCount(result), 1);
    EXPECT_TRUE(hasErrorCode(result, 9006));
    for (const auto& diagnostic : result.diagnostics)
    {
        if (diagnostic.code == 9006)
            EXPECT_NE(diagnostic.message.find("DivisionByZeroException"), std::string::npos);
    }
}

TEST(ErrorHandling, ConstexprCallInsideTryWarnsAndTakesFallback)
{
    const auto source = R"(
        struct DivisionByZeroException : Exception;

        constexpr i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
            if (divisor == 0) {
                throw DivisionByZeroException("Division {value}/0 is not allowed");
            }
            return value / divisor;
        }

        i32 main() {
            i32 result = try division(1, 0) ?: 7;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(errorCount(result), 0);
    EXPECT_EQ(warningCount(result), 1);
    EXPECT_TRUE(hasErrorCode(result, 9008));
    EXPECT_EQ(result.returnCode, 7);
}

TEST(ErrorHandling, ConstexprCallWithRuntimeArgumentsStaysRuntime)
{
    // A non-constant divisor is not analyzable at compile time: no
    // diagnostics, the runtime fallback decides
    const auto source = R"(
        struct DivisionByZeroException : Exception;

        constexpr i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
            if (divisor == 0) {
                throw DivisionByZeroException("Division {value}/0 is not allowed");
            }
            return value / divisor;
        }

        static mut i32 runtime_divisor = 2;

        i32 main() {
            i32 result = try division(10, runtime_divisor) ?: -1;
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(ErrorHandling, ConstexprCallThatSucceedsHasNoDiagnostics)
{
    // Constant arguments that do NOT throw: the evaluation succeeds and
    // nothing is reported
    const auto source = R"(
        struct DivisionByZeroException : Exception;

        constexpr i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
            if (divisor == 0) {
                throw DivisionByZeroException("Division {value}/0 is not allowed");
            }
            return value / divisor;
        }

        i32 main() throws {
            i32 test = division(10, 2);
            return test;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}
