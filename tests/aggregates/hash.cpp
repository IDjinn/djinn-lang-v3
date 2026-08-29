//
// Tests for Hashable impl on primitive integer types
//

#include "DjinnCompiler.h"
#include "gtest/gtest.h"

TEST(Hash, I32HashIsIdentity)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            i32 x = 42;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Hash, U8HashIsIdentity)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            u8 x = 20u;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 20);
}

TEST(Hash, I16HashIsIdentity)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            i16 x = 99;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Hash, U16HashIsIdentity)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            u16 x = 150;
            i32 h = x.hash();
            return h;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 150);
}

TEST(Hash, I8HashIsIdentity)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            i8 x = 77;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 77);
}

TEST(Hash, U32HashIsIdentity)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            u32 x = 55;
            i32 h = x.hash();
            return h;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 55);
}

TEST(Hash, I64HashProducesNonZero)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            i64 x = 42;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}

TEST(Hash, U64HashProducesNonZero)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            u64 x = 100;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}

TEST(Hash, 128HashProducesNonZero)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            i128 x = 121321300;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}

TEST(Hash, U128HashProducesNonZero)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            u128 x = 121321300;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_NE(result.returnCode, 0);
}

TEST(Hash, HexLiteralHash)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            i32 x = 0xFF;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 255);
}

TEST(Hash, BinaryLiteralHash)
{
    const auto source = R"(
        import std::types;
        i32 main() {
            i32 x = 0b101010;
            return x.hash();
        }
    )";

    const auto result = DjinnCompiler::run(source, {.generateBinary = true});
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}
