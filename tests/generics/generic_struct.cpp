//
// Created by Claude on 11/01/2026.
//

#include <gtest/gtest.h>

#include "DjinnCompiler.h"

// ============================================================================
// Multi-Parameter Generic Struct Tests
// ============================================================================

TEST(GenericStruct, TwoParameters)
{
    const auto source = R"(
        struct Pair<K, V> {
            K key;
            V value;
        }

        i32 main() {
            Pair<i32, i64> p;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericStruct, ThreeParameters)
{
    const auto source = R"(
        struct Triple<A, B, C> {
            A first;
            B second;
            C third;
        }

        i32 main() {
            Triple<i32, i64, f64> t;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericStruct, FourParameters)
{
    const auto source = R"(
        struct Quad<A, B, C, D> {
            A a;
            B b;
            C c;
            D d;
        }

        i32 main() {
            Quad<i8, i16, i32, i64> q;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericStruct, MapEntry)
{
    const auto source = R"(
        struct MapEntry<K, V> {
            K key;
            V value;
            i32 hash;
        }

        i32 main() {
            MapEntry<i8*, i32> entry;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericStruct, TwoParametersWithInit)
{
    const auto source = R"(
        struct Pair<K, V> {
            K key;
            V value;
        }

        i32 main() {
            Pair<i32, i32> p = { 10, 20 };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericStruct, TwoParametersFieldAccess)
{
    const auto source = R"(
        struct Pair<K, V> {
            K key;
            V value;
        }

        i32 main() {
            Pair<i32, i32> p = { .key = 42, .value = 100 };
            return p.key;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(GenericStruct, TwoParametersSecondFieldAccess)
{
    const auto source = R"(
        struct Pair<K, V> {
            K key;
            V value;
        }

        i32 main() {
            Pair<i32, i32> p = { .key = 10, .value = 99 };
            return p.value;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(GenericStruct, MixedTypes)
{
    const auto source = R"(
        struct Mixed<A, B> {
            A a;
            B b;
        }

        i32 main() {
            Mixed<i32, f64> m = { .a = 50, .b = 3.14 };
            return m.a;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 50);
}

TEST(GenericStruct, PointerTypes)
{
    const auto source = R"(
        struct Container<T, U> {
            T* first;
            U* second;
        }

        i32 main() {
            Container<i32, i64> c;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericStruct, MultipleInstantiations)
{
    const auto source = R"(
        struct Pair<K, V> {
            K key;
            V value;
        }

        i32 main() {
            Pair<i32, i32> p1;
            Pair<i32, i64> p2;
            Pair<i64, i32> p3;
            Pair<f32, f64> p4;
            Pair<i8*, i32> p5;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericStruct, NestedGenericStruct)
{
    const auto source = R"(
        struct Pair<K, V> {
            K key;
            V value;
        }

        struct Wrapper<T> {
            T data;
            i32 id;
        }

        i32 main() {
            Wrapper<Pair<i32, i32>> w;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericStruct, FunctionReturningGenericStruct)
{
    const auto source = R"(
        struct Pair<K, V> {
            K key;
            V value;
        }

        Pair<i32, i32> makePair(i32 k, i32 v) {
            return { .key = k, .value = v };
        }

        i32 main() {
            auto p = makePair(5, 10);
            return p.key + p.value;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 15);
}

TEST(GenericStruct, GenericStructAsParameter)
{
    const auto source = R"(
        struct Pair<K, V> {
            K key;
            V value;
        }

        i32 getKey(Pair<i32, i32> p) {
            return p.key;
        }

        i32 main() {
            Pair<i32, i32> p = { .key = 77, .value = 88 };
            return getKey(p);
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 77);
}

TEST(GenericStruct, ThreeParametersFieldSum)
{
    const auto source = R"(
        struct Triple<A, B, C> {
            A first;
            B second;
            C third;
        }

        i32 main() {
            Triple<i32, i32, i32> t = { .first = 10, .second = 20, .third = 30 };
            return t.first + t.second + t.third;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 60);
}
