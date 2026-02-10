#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

// ========================
// Stack constructors: Type(args)
// ========================

TEST(Constructor, StackBasicFieldAccess) {
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;

            Point(i32 x, i32 y) {
                this.x = x;
                this.y = y;
            }
        }

        i32 main() {
            Point p = Point(10, 20);
            return p.x;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(Constructor, StackSecondField) {
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;

            Point(i32 x, i32 y) {
                this.x = x;
                this.y = y;
            }
        }

        i32 main() {
            Point p = Point(10, 20);
            return p.y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 20);
}

TEST(Constructor, StackFieldExpression) {
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;

            Point(i32 x, i32 y) {
                this.x = x;
                this.y = y;
            }
        }

        i32 main() {
            Point p = Point(30, 12);
            return p.x + p.y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Constructor, StackThreeFields) {
    const auto source = R"(
        struct Vec3 {
            i32 x;
            i32 y;
            i32 z;

            Vec3(i32 x, i32 y, i32 z) {
                this.x = x;
                this.y = y;
                this.z = z;
            }
        }

        i32 main() {
            Vec3 v = Vec3(1, 2, 3);
            return v.x + v.y + v.z;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 6);
}

TEST(Constructor, StackPassedToFunction) {
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;

            Point(i32 x, i32 y) {
                this.x = x;
                this.y = y;
            }
        }

        i32 getX(Point p) {
            return p.x;
        }

        i32 main() {
            Point p = Point(99, 1);
            return getX(p);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

// ========================
// Heap constructors: new Type(args)
// ========================

TEST(Constructor, HeapBasicFieldAccess) {
    const auto source = R"(
        struct User {
            i32 id;
            i32 age;

            User(i32 id, i32 age) {
                this.id = id;
                this.age = age;
            }
        }

        i32 main() {
            User me = new User(1, 25);
            return me.id;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}

TEST(Constructor, HeapSecondField) {
    const auto source = R"(
        struct User {
            i32 id;
            i32 age;

            User(i32 id, i32 age) {
                this.id = id;
                this.age = age;
            }
        }

        i32 main() {
            User me = new User(1, 69);
            return me.age;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 69);
}

TEST(Constructor, HeapFieldExpression) {
    const auto source = R"(
        struct Point {
            i32 x;
            i32 y;

            Point(i32 x, i32 y) {
                this.x = x;
                this.y = y;
            }
        }

        i32 main() {
            Point p = new Point(30, 12);
            return p.x + p.y;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Constructor, HeapThreeFields) {
    const auto source = R"(
        struct Vec3 {
            i32 x;
            i32 y;
            i32 z;

            Vec3(i32 x, i32 y, i32 z) {
                this.x = x;
                this.y = y;
                this.z = z;
            }
        }

        i32 main() {
            Vec3 v = new Vec3(10, 20, 30);
            return v.x + v.y + v.z;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 60);
}

TEST(Constructor, HeapFieldAssignment) {
    const auto source = R"(
        struct Counter {
            i32 value;

            Counter(i32 v) {
                this.value = v;
            }
        }

        i32 main() {
            Counter c = new Counter(10);
            c.value = 77;
            return c.value;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 77);
}

// ========================
// Stack vs Heap: same struct, both paths
// ========================

TEST(Constructor, StackAndHeapSameStruct) {
    const auto source = R"(
        struct Pair {
            i32 a;
            i32 b;

            Pair(i32 a, i32 b) {
                this.a = a;
                this.b = b;
            }
        }

        i32 main() {
            Pair stack = Pair(10, 20);
            Pair heap = new Pair(30, 40);
            return stack.a + heap.b;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 50);
}
