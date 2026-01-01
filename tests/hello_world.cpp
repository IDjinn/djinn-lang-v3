#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Hello, World) {
    const auto source = R"(
        void main() {
            printf("hello world!");
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 12);
    EXPECT_EQ(result.program->functions.size(), 1);
    EXPECT_EQ(result.tokens.size(), 12);
    EXPECT_EQ(result.ir, R"(; ModuleID = 'djinn'
source_filename = "djinn"

@0 = private unnamed_addr constant [13 x i8] c"hello world!\00", align 1

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr noundef readonly captures(none), ...) local_unnamed_addr #0

; Function Attrs: nofree nounwind
define void @main() local_unnamed_addr #0 {
entry:
  %0 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @0)
  ret void
}

attributes #0 = { nofree nounwind }
)");
}