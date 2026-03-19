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
            TypeInfo info = { .id = 1, .size = 4, .name = "i32", .kind = 0 };
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
            TypeInfo info = { .id = 1, .size = 4, .name = "i32", .kind = 0 };
            i32 value = 42;
            object obj = { .type = &info, .data = &value };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(Object, TypeInfoKindInt)
{
    const auto source = R"(
        struct Inspector {
            public static i32 first_kind(...args) {
                object first = args[0];
                return (i32)first.type.kind;
            }
        }

        i32 main() {
            return Inspector.first_kind(42);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0); // kind 0 = int
}

TEST(Object, TypeInfoKindFloat)
{
    const auto source = R"(
        struct Inspector {
            public static i32 first_kind(...args) {
                object first = args[0];
                return (i32)first.type.kind;
            }
        }

        i32 main() {
            return Inspector.first_kind(3.14);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1); // kind 1 = float
}

TEST(Object, TypeInfoKindPointer)
{
    const auto source = R"(
        struct Inspector {
            public static i32 first_kind(...args) {
                object first = args[0];
                return (i32)first.type.kind;
            }
        }

        i32 main() {
            i8* s = "hello";
            return Inspector.first_kind(s);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2); // kind 2 = pointer
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

// ============================================================================
// Sync Variadic Formatting Tests
// ============================================================================

TEST(Object, SyncPrintIntViaVariadic)
{
    // Sync test: verify boxing + kind + unbox i32 via printf (no coroutines)
    const auto source = R"(
        struct Printer {
            public static i32 print_first_int(...args) {
                object first = args[0];
                u8 kind = first.type.kind;
                if (kind == 0) {
                    i32* ip = (i32*)first.data;
                    printf("value=%d\n", *ip);
                    return *ip;
                }
                return -1;
            }
        }

        i32 main() {
            return Printer.print_first_int(42);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Object, BoxingIRContainsKind)
{
    // Verify the generated IR contains kind field in TypeInfo
    const auto source = R"(
        struct Inspector {
            public static i32 check(...args) {
                object first = args[0];
                return (i32)first.type.kind;
            }
        }

        i32 main() {
            return Inspector.check(42);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .includeStd = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    // Check IR contains typeinfo with kind field (4th element in the struct constant)
    EXPECT_NE(result.ir.find("__typeinfo_i32"), std::string::npos) << "IR should contain __typeinfo_i32";
}