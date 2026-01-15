#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

TEST(Interface, BasicDefinition) {
    const auto source = R"(
        interface ICounter {
            i32 getCount();
            void increment();
        }

        void main() {
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->interfaces.size(), 1);
}

TEST(Interface, GenericInterface) {
    const auto source = R"(
        interface IComparable<T> {
            i32 compareTo(T other);
        }

        void main() {
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->interfaces.size(), 1);
}

TEST(Interface, StructImplementsInterface) {
    const auto source = R"(
        interface IValue {
            i32 getValue();
        }

        struct Counter : interface IValue {
            i32 count;

            i32 getValue() {
                return this.count;
            }
        }

        void main() {
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->interfaces.size(), 1);
    EXPECT_EQ(result.program->structs.size(), 1);
    EXPECT_EQ(result.program->structs[0]->implements.size(), 1);
    EXPECT_EQ(result.program->structs[0]->implements[0], "IValue");
}

TEST(Interface, StructImplementsMultipleInterfaces) {
    const auto source = R"(
        interface IReadable {
            i32 read();
        }

        interface IWritable {
            void write(i32 value);
        }

        struct Buffer : interface IReadable, interface IWritable {
            i32 data;

            i32 read() {
                return this.data;
            }

            void write(i32 value) {
                this.data = value;
            }
        }

        void main() {
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.program->interfaces.size(), 2);
    EXPECT_EQ(result.program->structs[0]->implements.size(), 2);
}