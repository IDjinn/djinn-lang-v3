#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(PointerCompat, VoidToTyped) {
    const auto source = R"(
        extern "C" {
            void* malloc(i64 size);
            void free(void* ptr);
        }

        i32 main() {
            i32* data = malloc(4);
            data[0] = 42;
            i32 val = data[0];
            free(data);
            return val;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(PointerCompat, TypedToVoid) {
    const auto source = R"(
        extern "C" {
            void free(void* ptr);
            void* malloc(i64 size);
        }

        i32 main() {
            i32* data = malloc(4);
            void* vp = data;
            free(vp);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(PointerCompat, IncompatibleTypes) {
    const auto source = R"(
        extern "C" {
            void* malloc(i64 size);
        }

        i32 main() {
            i64* data = malloc(8);
            i32* wrong = data;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(PointerCompat, SameType) {
    const auto source = R"(
        extern "C" {
            void* malloc(i64 size);
            void free(void* ptr);
        }

        i32 main() {
            i32* a = malloc(4);
            i32* b = a;
            free(b);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}
