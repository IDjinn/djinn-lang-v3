#include <gtest/gtest.h>

#include "DjinnCompiler.h"

TEST(Nullable, NullLiteralAllowedOnNullableType)
{
    const std::string source = R"(
        struct User { i32 id; }

        i32 main() {
            User*? u = null;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Nullable, NullLiteralForbiddenOnNonNullableStruct)
{
    const std::string source = R"(
        struct User { i32 id; }

        i32 main() {
            User u = null;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_GT(result.diagnostics.size(), 0);
    EXPECT_TRUE(
        result.diagnostics.at(0).message.contains(
            "cannot assign 'null' to non-nullable type — use 'User?' to allow null"));
}

TEST(Nullable, NullLiteralForbiddenOnNonNullablePointer)
{
    const std::string source = R"(
        i32 main() {
            i8* p = null;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_GT(result.diagnostics.size(), 0);
}

TEST(Nullable, NullLiteralForbiddenOnInteger)
{
    const std::string source = R"(
        i32 main() {
            i32 n = null;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_GT(result.diagnostics.size(), 0);
}

TEST(Nullable, NullableArraySyntax)
{
    const std::string source = R"(
        i32 main() {
            i32[]? arr = null;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Nullable, ReassignNullToNullableMutVariable)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            mut Box*? b = null;
            b = null;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Nullable, ReassignNullToNonNullableErrors)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            mut Box* b = (Box*)0;
            b = null;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_GT(result.diagnostics.size(), 0);
}

TEST(Nullable, NullCoalescingParsesAndCompiles)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            Box*? a = null;
            Box* b = (Box*)1;
            Box* c = a ?? b;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Nullable, NullCoalescingAssignmentDesugars)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            mut Box*? a = null;
            Box* b = (Box*)1;
            a ??= b;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Nullable, NullConditionalFieldAccessParses)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            Box*? a = null;
            i32 x = a?.v;
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    // Just verify parse + bind path works (codegen safety is best-effort v1)
    EXPECT_EQ(result.program->functions.size(), 1);
}

TEST(Nullable, NullForgivingFieldAccessParses)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            Box*? a = null;
            i32 x = a!.v;
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    EXPECT_EQ(result.program->functions.size(), 1);
}

TEST(Nullable, CastNonNullableToNullableAllowed)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            Box* b = (Box*)1;
            Box*? n = (Box*?)b;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    int errors = 0;
    for (const auto& d : result.diagnostics) if (d.severity == Severity::Error) errors++;
    EXPECT_EQ(errors, 0);
}

TEST(Nullable, CastNullableToNonNullableEmitsWarning)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            Box*? a = null;
            Box* b = (Box*)a;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    int warnings = 0;
    for (const auto& d : result.diagnostics)
        if (d.severity == Severity::Warning && d.message.find("nullable") != std::string::npos) warnings++;
    EXPECT_GT(warnings, 0);
}

TEST(Nullable, CastNullableToNullableNoWarning)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            Box*? a = null;
            Box*? b = (Box*?)a;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    int warnings = 0;
    for (const auto& d : result.diagnostics) if (d.severity == Severity::Warning) warnings++;
    EXPECT_EQ(warnings, 0);
}

TEST(Nullable, CastIntegerToPointerNoWarning)
{
    const std::string source = R"(
        struct Box { i32 v; }

        i32 main() {
            Box* b = (Box*)0;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = false});
    int warnings = 0;
    for (const auto& d : result.diagnostics) if (d.severity == Severity::Warning) warnings++;
    EXPECT_EQ(warnings, 0);
}
