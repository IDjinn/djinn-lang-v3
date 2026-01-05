#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

// ==================== IF STATEMENT TESTS ====================

TEST(ControlFlow, IfStatementTrueBranch) {
    const auto source = R"(
        i32 main() {
            i32 x = 10;
            if (x > 5) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(ControlFlow, IfStatementFalseBranch) {
    const auto source = R"(
        i32 main() {
            i32 x = 3;
            if (x > 5) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(ControlFlow, IfElseStatementTrueBranch) {
    const auto source = R"(
        i32 main() {
            i32 x = 10;
            if (x > 5) {
                return 1;
            } else {
                return 2;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(ControlFlow, IfElseStatementFalseBranch) {
    const auto source = R"(
        i32 main() {
            i32 x = 3;
            if (x > 5) {
                return 1;
            } else {
                return 2;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(ControlFlow, IfElseIfElseStatement) {
    const auto source = R"(
        i32 main() {
            i32 x = 5;
            if (x > 10) {
                return 1;
            } else if (x > 3) {
                return 2;
            } else {
                return 3;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(ControlFlow, NestedIfStatements) {
    const auto source = R"(
        i32 main() {
            i32 x = 10;
            i32 y = 5;
            if (x > 5) {
                if (y > 3) {
                    return 1;
                }
                return 2;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(ControlFlow, IfWithEquality) {
    const auto source = R"(
        i32 main() {
            i32 x = 5;
            if (x == 5) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(ControlFlow, IfWithNotEqual) {
    const auto source = R"(
        i32 main() {
            i32 x = 5;
            if (x != 10) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(ControlFlow, IfWithLogicalAnd) {
    const auto source = R"(
        i32 main() {
            i32 x = 10;
            i32 y = 5;
            if (x > 5 && y > 3) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(ControlFlow, IfWithLogicalOr) {
    const auto source = R"(
        i32 main() {
            i32 x = 3;
            i32 y = 10;
            if (x > 5 || y > 5) {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

// ==================== WHILE STATEMENT TESTS ====================

TEST(ControlFlow, WhileLoopSimple) {
    const auto source = R"(
        i32 main() {
            i32 mut x = 0;
            while (x < 5) {
                x = x + 1;
            }
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(ControlFlow, WhileLoopSum) {
    const auto source = R"(
        i32 main() {
            i32 mut sum = 0;
            i32 mut i = 1;
            while (i <= 5) {
                sum = sum + i;
                i = i + 1;
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 15); // 1 + 2 + 3 + 4 + 5 = 15
}

TEST(ControlFlow, WhileLoopWithBreak) {
    const auto source = R"(
        i32 main() {
            i32 mut x = 0;
            while (x < 10) {
                if (x == 5) {
                    break;
                }
                x = x + 1;
            }
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(ControlFlow, WhileLoopWithContinue) {
    const auto source = R"(
        i32 main() {
            i32 mut sum = 0;
            i32 mut i = 0;
            while (i < 10) {
                i = i + 1;
                if (i == 5) {
                    continue;
                }
                sum = sum + 1;
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 9); // 10 iterations - 1 skipped = 9
}

TEST(ControlFlow, WhileLoopNeverExecutes) {
    const auto source = R"(
        i32 main() {
            i32 x = 10;
            while (x < 5) {
                x = x + 1;
            }
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

// ==================== DO-WHILE STATEMENT TESTS ====================

TEST(ControlFlow, DoWhileLoopSimple) {
    const auto source = R"(
        i32 main() {
            i32 mut x = 0;
            do {
                x = x + 1;
            } while (x < 5);
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(ControlFlow, DoWhileExecutesAtLeastOnce) {
    const auto source = R"(
        i32 main() {
            i32 mut x = 10;
            do {
                x = x + 1;
            } while (x < 5);
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 11); // Executes once even though condition is false
}

TEST(ControlFlow, DoWhileWithBreak) {
    const auto source = R"(
        i32 main() {
            i32 mut x = 0;
            do {
                if (x == 3) {
                    break;
                }
                x = x + 1;
            } while (x < 10);
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 3);
}

// ==================== FOR STATEMENT TESTS ====================

TEST(ControlFlow, ForLoopSimple) {
    const auto source = R"(
        i32 main() {
            i32 mut sum = 0;
            for (i32 mut i = 0; i < 5; i = i + 1) {
                sum = sum + 1;
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(ControlFlow, ForLoopSum) {
    const auto source = R"(
        i32 main() {
            i32 mut sum = 0;
            for (i32 mut i = 1; i <= 10; i = i + 1) {
                sum = sum + i;
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 55); // 1 + 2 + ... + 10 = 55
}

TEST(ControlFlow, ForLoopWithBreak) {
    const auto source = R"(
        i32 main() {
            i32 mut sum = 0;
            for (i32 mut i = 0; i < 10; i = i + 1) {
                if (i == 5) {
                    break;
                }
                sum = sum + 1;
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}

TEST(ControlFlow, ForLoopWithContinue) {
    const auto source = R"(
        i32 main() {
            i32 mut sum = 0;
            for (i32 mut i = 0; i < 10; i = i + 1) {
                if (i == 5) {
                    continue;
                }
                sum = sum + 1;
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 9); // 10 iterations - 1 skipped = 9
}

TEST(ControlFlow, ForLoopNested) {
    const auto source = R"(
        i32 main() {
            i32 mut sum = 0;
            for (i32 mut i = 0; i < 3; i = i + 1) {
                for (i32 mut j = 0; j < 3; j = j + 1) {
                    sum = sum + 1;
                }
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 9); // 3 * 3 = 9
}

// ==================== SWITCH STATEMENT TESTS ====================

TEST(ControlFlow, SwitchStatementCase1) {
    const auto source = R"(
        i32 main() {
            i32 x = 1;
            switch (x) {
                case 1:
                    return 10;
                case 2:
                    return 20;
                default:
                    return 0;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(ControlFlow, SwitchStatementCase2) {
    const auto source = R"(
        i32 main() {
            i32 x = 2;
            switch (x) {
                case 1:
                    return 10;
                case 2:
                    return 20;
                default:
                    return 0;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 20);
}

TEST(ControlFlow, SwitchStatementDefault) {
    const auto source = R"(
        i32 main() {
            i32 x = 99;
            switch (x) {
                case 1:
                    return 10;
                case 2:
                    return 20;
                default:
                    return 0;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(ControlFlow, SwitchStatementWithBreak) {
    const auto source = R"(
        i32 main() {
            i32 x = 1;
            i32 mut result = 0;
            switch (x) {
                case 1:
                    result = 10;
                    break;
                case 2:
                    result = 20;
                    break;
                default:
                    result = 0;
            }
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(ControlFlow, SwitchStatementWithoutDefault) {
    const auto source = R"(
        i32 main() {
            i32 x = 1;
            i32 mut result = 0;
            switch (x) {
                case 1:
                    result = 10;
                    break;
                case 2:
                    result = 20;
                    break;
            }
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

// ==================== COMPLEX CONTROL FLOW TESTS ====================

TEST(ControlFlow, FibonacciIterative) {
    const auto source = R"(
        i32 main() {
            i32 n = 10;
            i32 mut a = 0;
            i32 mut b = 1;
            for (i32 mut i = 0; i < n; i = i + 1) {
                i32 temp = a + b;
                a = b;
                b = temp;
            }
            return a;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 55); // Fib(10) = 55
}

TEST(ControlFlow, FactorialIterative) {
    const auto source = R"(
        i32 main() {
            i32 n = 5;
            i32 mut result = 1;
            for (i32 mut i = 1; i <= n; i = i + 1) {
                result = result * i;
            }
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 120); // 5! = 120
}

TEST(ControlFlow, NestedLoopsWithBreak) {
    const auto source = R"(
        i32 main() {
            i32 mut count = 0;
            for (i32 mut i = 0; i < 5; i = i + 1) {
                for (i32 mut j = 0; j < 5; j = j + 1) {
                    if (j == 3) {
                        break;
                    }
                    count = count + 1;
                }
            }
            return count;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 15); // 5 outer iterations * 3 inner iterations = 15
}

TEST(ControlFlow, WhileWithMultipleConditions) {
    const auto source = R"(
        i32 main() {
            i32 mut x = 0;
            i32 mut y = 10;
            while (x < 5 && y > 5) {
                x = x + 1;
                y = y - 1;
            }
            return x + y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10); // x = 5, y = 5, sum = 10
}
