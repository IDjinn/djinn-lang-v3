//
// Tests for the attribute system — parsing, validation, and LLVM IR generation
//

#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

// ============================================================================
// Attribute parsing
// ============================================================================

TEST(Attributes, ForceInlineGeneratesAlwaysInline)
{
    const auto source = R"(
        struct Math {
            [force-inline]
            public static i32 add(i32 a, i32 b) {
                return a + b;
            }
        }

        i32 main() {
            return Math.add(1, 2);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("alwaysinline"), std::string::npos)
        << "IR should contain alwaysinline attribute";
}

TEST(Attributes, NoInlineGeneratesNoInline)
{
    const auto source = R"(
        struct Math {
            [no-inline]
            public static i32 add(i32 a, i32 b) {
                return a + b;
            }
        }

        i32 main() {
            return Math.add(1, 2);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("noinline"), std::string::npos)
        << "IR should contain noinline attribute";
}

TEST(Attributes, NoReturnAttribute)
{
    const auto source = R"(
        struct Util {
            [noreturn]
            public static void abort() {
                while (true) {}
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.runAfterCompile = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("noreturn"), std::string::npos)
        << "IR should contain noreturn attribute";
}

TEST(Attributes, ColdAttribute)
{
    const auto source = R"(
        struct Util {
            [cold]
            public static i32 rare_path() {
                return -1;
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("cold"), std::string::npos)
        << "IR should contain cold attribute";
}

// ============================================================================
// Parameterized attributes
// ============================================================================

TEST(Attributes, ParameterizedAttributeParses)
{
    const auto source = R"(
        [align(16)]
        struct Vec4 {
            f32 x;
            f32 y;
            f32 z;
            f32 w;
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Attributes, NamedParameterAttributeParses)
{
    const auto source = R"(
        [deprecated(message = "use v2")]
        struct OldApi {
            public static i32 call() {
                return 0;
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

// ============================================================================
// Multiple attributes
// ============================================================================

TEST(Attributes, MultipleAttributesOnMethod)
{
    const auto source = R"(
        struct Math {
            [force-inline]
            [hot]
            public static i32 add(i32 a, i32 b) {
                return a + b;
            }
        }

        i32 main() {
            return Math.add(1, 2);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("alwaysinline"), std::string::npos);
    EXPECT_NE(result.ir.find("hot"), std::string::npos);
}

// ============================================================================
// Implicit attributes
// ============================================================================

TEST(Attributes, ImplicitNounwindOnAllFunctions)
{
    const auto source = R"(
        struct Math {
            public static i32 add(i32 a, i32 b) {
                return a + b;
            }
        }

        i32 main() {
            return Math.add(1, 2);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("nounwind"), std::string::npos)
        << "All Djinn functions should have nounwind";
}

// ============================================================================
// [llvm(...)] escape hatch
// ============================================================================

TEST(Attributes, LlvmEscapeHatch)
{
    const auto source = R"(
        struct Util {
            [llvm("mustprogress")]
            public static i32 compute(i32 x) {
                return x * 2;
            }
        }

        i32 main() {
            return Util.compute(21);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.ir.find("mustprogress"), std::string::npos)
        << "IR should contain raw LLVM attribute from [llvm()] escape hatch";
}

// ============================================================================
// Unknown attribute error
// ============================================================================

TEST(Attributes, ErrorOnUnknownAttribute)
{
    const auto source = R"(
        struct Util {
            [InvalidAttribute]
            public static i32 rare_path() {
                return -1;
            }
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_GT(result.diagnostics.size(), 0);
}

TEST(Attributes, ErrorOnUnknownStructAttribute)
{
    const auto source = R"(
        [NotARealAttribute]
        struct Foo {
            i32 x;
        }

        i32 main() {
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_GT(result.diagnostics.size(), 0);
}
