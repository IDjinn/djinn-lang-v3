//
// Created by Claude on 11/01/2026.
//

#include <gtest/gtest.h>
#include "TestHelpers.h"

#include "DjinnCompiler.h"

// ============================================================================
// Basic Enum Tests
// ============================================================================

TEST(Enum, SimpleDefinition)
{
    const auto source = R"(
        enum Color {
            Red(),
            Green(),
            Blue()
        }

        i32 main() {
            Color c;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Enum, VariantConstruction)
{
    const auto source = R"(
        enum Status {
            Active(),
            Inactive()
        }

        i32 main() {
            Status s = Status::Active();
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Enum, VariantWithPayload)
{
    const auto source = R"(
        enum Result {
            Ok(i32),
            Error(i32)
        }

        i32 main() {
            Result r = Result::Ok(42);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Enum, MultiplePayloadTypes)
{
    const auto source = R"(
        enum Message {
            Empty(),
            Text(i8*),
            Number(i64),
            Pair(i32, i32)
        }

        i32 main() {
            Message m1 = Message::Empty();
            Message m2 = Message::Number(100);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Enum, VariantWithMultipleFields)
{
    const auto source = R"(
        enum Event {
            Click(i32, i32),
            KeyPress(i32),
            Resize(i32, i32, i32, i32)
        }

        i32 main() {
            Event click = Event::Click(100, 200);
            Event resize = Event::Resize(0, 0, 800, 600);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

// ============================================================================
// Generic Enum Tests
// ============================================================================

TEST(Enum, GenericOptional)
{
    const auto source = R"(
        enum optional<T> {
            Empty(),
            Value(T)
        }

        i32 main() {
            optional<i32> maybe_int = optional<i32>::Value(42);
            optional<i32> nothing = optional<i32>::Empty();
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Enum, GenericResult)
{
    const auto source = R"(
        enum result<T, E> {
            Ok(T),
            Error(E)
        }

        i32 main() {
            result<i32, i8*> success = result<i32, i8*>::Ok(100);
            result<i32, i8*> failure = result<i32, i8*>::Error("error");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Enum, GenericWithPointerType)
{
    const auto source = R"(
        enum opt<T> {
            Empty(),
            Value(T)
        }

        i32 main() {
            opt<str> maybe_string = opt<str>::Value("hello");
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Enum, GenericWithFloatType)
{
    const auto source = R"(
        enum optional<T> {
            Empty(),
            Value(T)
        }

        i32 main() {
            optional<f64> maybe_float = optional<f64>::Value(3.14159);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Enum, GenericMultipleInstantiations)
{
    const auto source = R"(
        enum optional<T> {
            Empty(),
            Value(T)
        }

        i32 main() {
            optional<i32> a = optional<i32>::Value(1);
            optional<i64> b = optional<i64>::Value(2);
            optional<f32> c = optional<f32>::Value(3.0);
            optional<f64> d = optional<f64>::Value(4.0);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

// ============================================================================
// Pattern Matching (Switch Expression) Tests
// ============================================================================

TEST(Enum, PatternMatchBasic)
{
    const auto source = R"(
        enum optional<T> {
            Empty(),
            Value(T)
        }

        i32 main() {
            auto opt = optional<i32>::Value(69);

            i32 result = switch opt {
                Value val -> val,
                Empty -> -1
            };

            return result;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 69);
}

TEST(Enum, PatternMatchEmpty)
{
    const auto source = R"(
        enum optional<T> {
            Empty(),
            Value(T)
        }

        i32 main() {
            auto opt = optional<i32>::Empty();

            i32 result = switch opt {
                Value val -> val,
                Empty -> -1
            };

            return result;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(-1));
}

TEST(Enum, PatternMatchResult)
{
    const auto source = R"(
        enum result<T, E> {
            Ok(T),
            Error(E)
        }

        i32 main() {
            auto r = result<i32, i32>::Ok(42);

            i32 val = switch r {
                Ok v -> v,
                Error e -> e
            };

            return val;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Enum, PatternMatchResultError)
{
    const auto source = R"(
        enum result<T, E> {
            Ok(T),
            Error(E)
        }

        i32 main() {
            auto r = result<i32, i32>::Error(7);

            i32 val = switch r {
                Ok v -> v,
                Error e -> e
            };

            return val;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 7);
}

TEST(Enum, PatternMatchWithComputation)
{
    const auto source = R"(
        enum optional<T> {
            Empty(),
            Value(T)
        }

        i32 main() {
            auto opt = optional<i32>::Value(10);

            i32 result = switch opt {
                Value val -> val * 2 + 1,
                Empty -> 0
            };

            return result;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 21);
}

TEST(Enum, PatternMatchNoPayload)
{
    const auto source = R"(
        enum Color {
            Red(),
            Green(),
            Blue()
        }

        i32 main() {
            Color c = Color::Green();

            i32 result = switch c {
                Red -> 1,
                Green -> 2,
                Blue -> 3
            };

            return result;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(Enum, PatternMatchInFunction)
{
    const auto source = R"(
        enum optional<T> {
            Empty(),
            Value(T)
        }

        i32 unwrap_or(optional<i32> opt, i32 default_val) {
            return switch opt {
                Value val -> val,
                Empty -> default_val
            };
        }

        i32 main() {
            auto a = optional<i32>::Value(50);
            auto b = optional<i32>::Empty();

            i32 x = unwrap_or(a, 0);
            i32 y = unwrap_or(b, 99);

            return x + y;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 149);
}

TEST(Enum, PatternMatchWildcardArm)
{
    // _ matches any variant without binding a payload
    const auto source = R"(
        enum optional<T> {
            Empty(),
            Value(T)
        }

        i32 main() {
            auto opt = optional<i32>::Empty();

            i32 result = switch opt {
                Value val -> val,
                _ -> -1
            };

            return result;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, DJINN_EXIT(-1));
}
