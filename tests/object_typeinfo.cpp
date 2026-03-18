//
// Tests for object, TypeInfo, and variadic auto-boxing
//

#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

// ============================================================================
// TypeInfo and object Struct Definition Tests
// ============================================================================

TEST(Object, TypeInfoStructDefinition)
{
    const auto source = R"(
        i32 main() {
            TypeInfo info = { .id = 1, .size = 4, .name = "i32" };
            return info.id;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(Object, ObjectStructDefinition)
{
    const auto source = R"(
        i32 main() {
            TypeInfo info = { .id = 1, .size = 4, .name = "i32" };
            i32 value = 42;
            object obj = { .data = &value, .type = &info };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

// ============================================================================
// Variadic Auto-Boxing Tests
// ============================================================================

TEST(Object, VariadicMethodCompiles)
{
    const auto source = R"(
        struct Logger {
            public static void log(...args) {
            }
        }

        i32 main() {
            Logger.log(42, 3);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Object, VariadicEmptyArgs)
{
    const auto source = R"(
        struct Logger {
            public static void log(...args) {
            }
        }

        i32 main() {
            Logger.log();
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Object, VariadicArgsLength)
{
    const auto source = R"(
        struct Util {
            public static i32 count(...args) {
                return (i32)args.length;
            }
        }

        i32 main() {
            return Util.count(1, 2, 3);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

TEST(Object, VariadicWithNormalParams)
{
    const auto source = R"(
        struct Formatter {
            public static i32 format(i8* fmt, ...args) {
                return (i32)args.length;
            }
        }

        i32 main() {
            return Formatter.format("hello {} {}", 42, 99);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(Object, VariadicAccessTypeInfo)
{
    const auto source = R"(
        struct Inspector {
            public static i32 first_type_size(...args) {
                object first = args[0];
                return first.type.size;
            }
        }

        i32 main() {
            return Inspector.first_type_size(42);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 4); // sizeof(i32) = 4
}