#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Struct, Definition) {
    const auto source = R"(
        struct Result {
            i32 value;
        }

        void main() {
            Result result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->functions.size(), 1);
    EXPECT_EQ(result.tokens.size(), 17);
    EXPECT_EQ(result.ir, R"(; ModuleID = 'djinn'
source_filename = "djinn"

%Result = type { i32 }

declare i32 @printf(ptr, ...)

define void @main() {
entry:
  %result = alloca %Result, align 8
  store %Result zeroinitializer, ptr %result, align 4
  ret void
}
)");
}

TEST(Struct, MethodReturnDefinition) {
    const auto source = R"(
        struct {
            i32 value;
        } sum(i32 a, i32 b) {
            return { .value = a + b };
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->functions.size(), 1);
    EXPECT_EQ(result.tokens.size(), 17);
    EXPECT_EQ(result.ir, R"(; ModuleID = 'djinn'
source_filename = "djinn"

%Result = type { i32 }

declare i32 @printf(ptr, ...)

define void @main() {
entry:
  %result = alloca %Result, align 8
  store %Result zeroinitializer, ptr %result, align 4
  ret void
}
)");
}