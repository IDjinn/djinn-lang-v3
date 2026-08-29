//
// Generic Constraints (where clause) tests
//

#include <gtest/gtest.h>

#include "DjinnCompiler.h"

// ============================================================================
// Parser: Where clause parsing
// ============================================================================

TEST(GenericConstraints, ParseWhereClauseSingleConstraint)
{
    const auto source = R"(
        interface IHashable {
            i32 hash();
        }

        struct Set<K> where K : IHashable {
            K key;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->structs.size(), 1);

    const auto& structDecl = result.program->structs[0];
    ASSERT_EQ(structDecl->genericParams.size(), 1);
    EXPECT_EQ(structDecl->genericParams.params[0].constraints.size(), 1);
    EXPECT_EQ(structDecl->genericParams.params[0].constraints[0], "IHashable");
}

TEST(GenericConstraints, ParseWhereClauseMultipleConstraintsSameParam)
{
    const auto source = R"(
        interface IHashable {
            i32 hash();
        }

        interface IEquatable {
            i32 equals();
        }

        struct Set<K> where K : IHashable, IEquatable {
            K key;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);

    const auto& structDecl = result.program->structs[0];
    ASSERT_EQ(structDecl->genericParams.size(), 1);
    EXPECT_EQ(structDecl->genericParams.params[0].constraints.size(), 2);
    EXPECT_EQ(structDecl->genericParams.params[0].constraints[0], "IHashable");
    EXPECT_EQ(structDecl->genericParams.params[0].constraints[1], "IEquatable");
}

TEST(GenericConstraints, ParseWhereClauseMultipleParams)
{
    const auto source = R"(
        interface IHashable {
            i32 hash();
        }

        interface ISerializable {
            i32 serialize();
        }

        struct Map<K, V> where K : IHashable; where V : ISerializable {
            K key;
            V value;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);

    const auto& structDecl = result.program->structs[0];
    ASSERT_EQ(structDecl->genericParams.size(), 2);
    EXPECT_EQ(structDecl->genericParams.params[0].constraints.size(), 1);
    EXPECT_EQ(structDecl->genericParams.params[0].constraints[0], "IHashable");
    EXPECT_EQ(structDecl->genericParams.params[1].constraints.size(), 1);
    EXPECT_EQ(structDecl->genericParams.params[1].constraints[0], "ISerializable");
}

TEST(GenericConstraints, ParseWhereClauseOnEnum)
{
    const auto source = R"(
        interface ISerializable {
            i32 serialize();
        }

        enum Wrapper<T> where T : ISerializable {
            Some(T),
            None()
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->enums.size(), 1);

    const auto& enumDecl = result.program->enums[0];
    ASSERT_EQ(enumDecl->genericParams.size(), 1);
    EXPECT_EQ(enumDecl->genericParams.params[0].constraints.size(), 1);
    EXPECT_EQ(enumDecl->genericParams.params[0].constraints[0], "ISerializable");
}

TEST(GenericConstraints, ParseWhereClauseOnInterface)
{
    const auto source = R"(
        interface IEquatable {
            i32 equals();
        }

        interface ISortable<T> where T : IEquatable {
            void sort();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->interfaces.size(), 2);

    const auto& ifaceDecl = result.program->interfaces[1];
    ASSERT_EQ(ifaceDecl->genericParams.size(), 1);
    EXPECT_EQ(ifaceDecl->genericParams.params[0].constraints.size(), 1);
    EXPECT_EQ(ifaceDecl->genericParams.params[0].constraints[0], "IEquatable");
}

// ============================================================================
// Binder: Constraint validation (interface must exist)
// ============================================================================

TEST(GenericConstraints, UndefinedConstraintInterfaceError)
{
    const auto source = R"(
        struct Set<K> where K : INonExistent {
            K key;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_GE(result.diagnostics.size(), 1);
}

// ============================================================================
// Generator: Constraint satisfaction check
// ============================================================================

TEST(GenericConstraints, ConstraintSatisfied)
{
    const auto source = R"(
        interface IValue {
            i32 getValue();
        }

        struct Container<T> where T : IValue {
            T item;
        }

        struct MyItem : IValue {
            i32 data;

            i32 getValue() {
                return this.data;
            }
        }

        i32 main() {
            MyItem item = { .data = 42 };
            Container<MyItem> c = { .item = item };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericConstraints, ConstraintViolated)
{
    const auto source = R"(
        interface IValue {
            i32 getValue();
        }

        struct Container<T> where T : IValue {
            T item;
        }

        struct PlainStruct {
            i32 data;
        }

        i32 main() {
            PlainStruct item = { .data = 10 };
            Container<PlainStruct> c = { .item = item };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_GE(result.diagnostics.size(), 1);
}

TEST(GenericConstraints, NoConstraintNoValidation)
{
    const auto source = R"(
        struct Container<T> {
            T item;
        }

        struct Anything {
            i32 x;
        }

        i32 main() {
            Anything a = { .x = 55 };
            Container<Anything> c = { .item = a };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericConstraints, MultipleConstraintsSatisfied)
{
    const auto source = R"(
        interface IReadable {
            i32 read();
        }

        interface IWritable {
            void write(i32 val);
        }

        struct Store<T> where T : IReadable, IWritable {
            T backend;
        }

        struct MemBackend : IReadable, IWritable {
            i32 value;

            i32 read() {
                return this.value;
            }

            void write(i32 val) {
                this.value = val;
            }
        }

        i32 main() {
            MemBackend b = { .value = 99 };
            Store<MemBackend> s = { .backend = b };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 0);
}

TEST(GenericConstraints, MultipleConstraintsPartiallyViolated)
{
    const auto source = R"(
        interface IReadable {
            i32 read();
        }

        interface IWritable {
            void write(i32 val);
        }

        struct Store<T> where T : IReadable, IWritable {
            T backend;
        }

        struct ReadOnly : IReadable {
            i32 value;

            i32 read() {
                return this.value;
            }
        }

        i32 main() {
            ReadOnly r = { .value = 10 };
            Store<ReadOnly> s = { .backend = r };
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_GE(result.diagnostics.size(), 1);
}

// ============================================================================
// Where clause on enum with constraint validation
// ============================================================================

TEST(GenericConstraints, EnumConstraintSatisfied)
{
    const auto source = R"(
        interface IValue {
            i32 getValue();
        }

        enum Wrapper<T> where T : IValue {
            Some(T),
            None()
        }

        struct Val : IValue {
            i32 x;

            i32 getValue() {
                return this.x;
            }
        }

        i32 main() {
            Val v = { .x = 7 };
            Wrapper<Val> w = Wrapper<Val>::Some(v);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(GenericConstraints, EnumConstraintViolated)
{
    const auto source = R"(
        interface IValue {
            i32 getValue();
        }

        enum Wrapper<T> where T : IValue {
            Some(T),
            None()
        }

        struct Plain {
            i32 x;
        }

        i32 main() {
            Plain p = { .x = 1 };
            Wrapper<Plain> w = Wrapper<Plain>::Some(p);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimizationLevel = 0, .includeStd = false});
    EXPECT_GE(result.diagnostics.size(), 1);
}

// ============================================================================
// Struct with implements list AND where clause (no conflict)
// ============================================================================

TEST(GenericConstraints, WhereClauseWithImplements)
{
    const auto source = R"(
        interface IContainer {
            i32 size();
        }

        interface IValue {
            i32 getValue();
        }

        struct TypedBox<T> where T : IValue : IContainer {
            T item;
            i32 count;

            i32 size() {
                return this.count;
            }
        }

        struct MyVal : IValue {
            i32 data;

            i32 getValue() {
                return this.data;
            }
        }

        i32 main() {
            MyVal v = { .data = 33 };
            TypedBox<MyVal> box = { .item = v, .count = 1 };
            return box.count;
        }
    )";

    const auto result = DjinnCompiler::run(
        source, {.optimizationLevel = 0, .generateBinary = true, .includeStd = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 1);
}
