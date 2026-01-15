#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Math, IntegerSumFunction) {
    const auto source = R"(
        i32 sum(i32 a, i32 b) {
            return a + b;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 0);
    EXPECT_EQ(result.program->functions.size(), 1);
    EXPECT_EQ(result.tokens.size(), 17);
    EXPECT_EQ(result.ir, R"(; ModuleID = 'djinn'
source_filename = "djinn"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define i32 @sum(i32 %a, i32 %b) local_unnamed_addr #0 {
entry:
  %addtmp = add i32 %b, %a
  ret i32 %addtmp
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef i32 @main() local_unnamed_addr #0 {
entry:
  ret i32 0
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) }
)");
}

TEST(Math, IntegerSum) {
    const auto source = R"(
        i32 sum(i32 a, i32 b) {
            return a + b;
        }

        i32 main() {
            return sum(2, 3);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.returnCode, 5);
    EXPECT_EQ(result.program->functions.size(), 2);
    EXPECT_EQ(result.tokens.size(), 31);
    EXPECT_EQ(result.ir, R"(; ModuleID = 'djinn'
source_filename = "djinn"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define i32 @sum(i32 %a, i32 %b) local_unnamed_addr #0 {
entry:
  %addtmp = add i32 %b, %a
  ret i32 %addtmp
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef i32 @main() local_unnamed_addr #0 {
entry:
  ret i32 5
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) }
)");
}