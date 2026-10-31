// Language features the self-hosted compiler (djinnc/) relies on.
// A failure here means stage0 can no longer build stage1.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "DjinnCompiler.h"

TEST(BootstrapSubset, PointerParameterFieldReadWrite)
{
    const auto source = R"(
        struct Node {
            i32 value;
        }

        void set_value(Node* node, i32 v) {
            node.value = v;
        }

        i32 get_value(Node* node) {
            return node.value;
        }

        i32 main() {
            Node* n = malloc(sizeof(Node));
            set_value(n, 41);
            n.value = n.value + 1;
            i32 result = get_value(n);
            free(n);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(BootstrapSubset, PointerLocalMethodCall)
{
    const auto source = R"(
        struct Counter {
            i32 count;

            void bump() {
                this.count = this.count + 1;
            }
        }

        i32 main() {
            Counter* c = malloc(sizeof(Counter));
            c.count = 0;
            c.bump();
            c.bump();
            i32 result = c.count;
            free(c);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(BootstrapSubset, PointerParameterInMethod)
{
    const auto source = R"(
        struct Node {
            i32 value;
        }

        struct Sum {
            i32 total;

            void add(Node* node) {
                this.total = this.total + node.value;
            }
        }

        i32 main() {
            Node* n = malloc(sizeof(Node));
            n.value = 9;
            Sum* s = malloc(sizeof(Sum));
            s.total = 1;
            s.add(n);
            i32 result = s.total;
            free(n);
            free(s);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(BootstrapSubset, NullableLinkedListTraversal)
{
    const auto source = R"(
        struct Node {
            i32 value;
            Node*? next;
        }

        Node* make(i32 value, Node*? next) {
            Node* n = malloc(sizeof(Node));
            n.value = value;
            n.next = next;
            return n;
        }

        i32 main() {
            Node* head = make(1, make(2, make(3, null)));
            mut i32 sum = 0;
            mut Node*? cur = head;
            while (cur != null) {
                sum = sum + cur.value;
                cur = cur.next;
            }
            if (head.next == null) {
                return 100;
            }
            return sum;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 6);
}

TEST(BootstrapSubset, LogicalOperatorsShortCircuit)
{
    const auto source = R"(
        struct Node {
            i32 value;
        }

        i32 main() {
            Node*? missing = null;
            mut i32 result = 0;
            if (missing != null && missing.value == 1) {
                result = 1;
            }
            if (missing == null || missing.value == 1) {
                result = result + 2;
            }
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 2);
}

TEST(BootstrapSubset, PointerArrayOfStructPointers)
{
    const auto source = R"(
        struct Item {
            i32 weight;
        }

        i32 main() {
            void** items = malloc(8 * 3);
            for (mut i32 i = 0; i < 3; i = i + 1) {
                Item* item = malloc(sizeof(Item));
                item.weight = i + 1;
                items[i] = item;
            }
            mut i32 total = 0;
            for (mut i32 i = 0; i < 3; i = i + 1) {
                Item* item = items[i];
                total = total + item.weight;
            }
            return total;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 6);
}

TEST(BootstrapSubset, StructTypesVisibleAcrossFiles)
{
    namespace fs = std::filesystem;
    const auto projectDir = fs::temp_directory_path() / "djinn_bootstrap_cross_file";
    fs::remove_all(projectDir);
    fs::create_directories(projectDir);

    std::ofstream(projectDir / "shapes.djinn") << R"(
        namespace shapes;

        struct Box {
            i32 volume;
        }
    )";

    std::ofstream(projectDir / "main.djinn") << R"(
        import shapes;

        i32 main() {
            Box* box = malloc(sizeof(Box));
            box.volume = 5;
            i32 result = box.volume;
            free(box);
            return result;
        }
    )";

    CompilerOptions options;
    options.generateBinary = true;
    options.runAfterCompile = true;
    options.noCache = true;
    options.outputFileName = "cross_file";
    options.outputDirectory = (projectDir / "build").string();

    const auto result = DjinnCompiler::compileFromDirectory(projectDir, options);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 5);
}
