#include <gtest/gtest.h>

#include "../DjinnCompiler.h"

// ========================
// Async/Await: basic
// ========================

TEST(Async, SimpleReturnValue)
{
    const auto source = R"(
        async i32 compute() {
            return 42;
        }

        i32 main() {
            i32 result = await compute();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Async, WithParameter)
{
    const auto source = R"(
        async i32 double_val(i32 x) {
            return x * 2;
        }

        i32 main() {
            i32 result = await double_val(21);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Async, MultipleParameters)
{
    const auto source = R"(
        async i32 add(i32 a, i32 b) {
            return a + b;
        }

        i32 main() {
            i32 result = await add(30, 12);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Async, ExpressionComputation)
{
    const auto source = R"(
        async i32 compute(i32 x) {
            return x * 2 + 1;
        }

        i32 main() {
            i32 result = await compute(10);
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 21);
}

// ========================
// Async/Await: async main
// ========================

TEST(Async, AsyncMain)
{
    const auto source = R"(
        async i32 get_value() {
            return 99;
        }

        async i32 main() {
            i32 val = await get_value();
            return val;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Async, AsyncMainWithComputation)
{
    const auto source = R"(
        async i32 square(i32 x) {
            return x * x;
        }

        async i32 main() {
            i32 a = await square(3);
            i32 b = await square(4);
            return a + b;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 25); // 9 + 16
}

TEST(Async, DiagnosticsForNonWaitableAsync)
{
    const auto source = R"(
        async i32 get_value() {
            return 99;
        }

        async i32 main() {
            i32 val = get_value();
            return val;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_GE(result.diagnostics.size(), 1);
    EXPECT_TRUE(
        result.diagnostics.at(0).message.contains("cannot assign result of async function 'get_value' without 'await'"
        ));
    EXPECT_NE(result.returnCode, 99);
}

// ========================
// Async/Await: multiple awaits
// ========================

TEST(Async, MultipleAwaitsSequential)
{
    const auto source = R"(
        async i32 get_a() {
            return 10;
        }

        async i32 get_b() {
            return 20;
        }

        i32 main() {
            i32 a = await get_a();
            i32 b = await get_b();
            return a + b;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

TEST(Async, ThreeAwaitsAccumulate)
{
    const auto source = R"(
        async i32 val(i32 x) {
            return x;
        }

        i32 main() {
            i32 a = await val(10);
            i32 b = await val(20);
            i32 c = await val(30);
            return a + b + c;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 60);
}

// ========================
// Async/Await: chained (async calling async)
// ========================

TEST(Async, ChainedAwait)
{
    const auto source = R"(
        async i32 inner() {
            return 10;
        }

        async i32 outer() {
            i32 val = await inner();
            return val + 5;
        }

        i32 main() {
            i32 result = await outer();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 15);
}

TEST(Async, DeepChaining)
{
    const auto source = R"(
        async i32 level3() {
            return 5;
        }

        async i32 level2() {
            i32 val = await level3();
            return val * 2;
        }

        async i32 level1() {
            i32 val = await level2();
            return val + 3;
        }

        i32 main() {
            i32 result = await level1();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 13); // (5 * 2) + 3
}

TEST(Async, ChainedAsyncMain)
{
    const auto source = R"(
        async i32 add(i32 a, i32 b) {
            return a + b;
        }

        async i32 compute() {
            i32 x = await add(10, 20);
            i32 y = await add(x, 5);
            return y;
        }

        async i32 main() {
            i32 result = await compute();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 35);
}

// ========================
// Async/Await: await result used in further computation
// ========================

TEST(Async, AwaitResultAsArgument)
{
    const auto source = R"(
        async i32 double_it(i32 x) {
            return x * 2;
        }

        i32 main() {
            i32 a = await double_it(5);
            i32 b = await double_it(a);
            return b;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 20); // 5 * 2 = 10, 10 * 2 = 20
}

TEST(Async, AwaitWithLocalComputation)
{
    const auto source = R"(
        async i32 fetch(i32 x) {
            return x + 1;
        }

        i32 main() {
            i32 val = await fetch(10);
            i32 local = val * 3;
            return local;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 33); // (10 + 1) * 3
}

// ========================
// Yield: intermediate suspend
// ========================

TEST(Async, Yield_BasicYield)
{
    const auto source = R"(
        async i32 compute() {
            yield;
            return 42;
        }

        i32 main() {
            i32 result = await compute();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Async, Yield_MultipleYields)
{
    const auto source = R"(
        async i32 compute() {
            yield;
            yield;
            yield;
            return 99;
        }

        i32 main() {
            i32 result = await compute();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(Async, Yield_WithComputation)
{
    const auto source = R"(
        async i32 counter() {
            mut i32 x = 10;
            yield;
            x = x + 20;
            yield;
            return x;
        }

        i32 main() {
            i32 result = await counter();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30); // 10 + 20
}

// ========================
// Spawn: basic fire-and-forget
// ========================

TEST(Spawn, BasicSpawnParse)
{
    // Verify spawn keyword parses correctly (IR generation only, no binary)
    const auto source = R"(
        async i32 compute(i32 x) {
            return x * 2;
        }

        async i32 main() {
            spawn compute(10);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .generateBinary = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    // Verify spawn and event loop declarations appear in IR
    EXPECT_NE(result.ir.find("__djinn_spawn"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_event_loop"), std::string::npos);
}

// ========================
// Event Loop: async main generates runtime calls
// ========================

TEST(EventLoop, AsyncMainGeneratesRuntimeCalls)
{
    const auto source = R"(
        async i32 get_value() {
            return 42;
        }

        async i32 main() {
            i32 val = await get_value();
            return val;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .generateBinary = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    // Verify runtime init/shutdown are called in the IR
    EXPECT_NE(result.ir.find("__djinn_runtime_init"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_runtime_shutdown"), std::string::npos);
}

TEST(EventLoop, CoroWrappersGenerated)
{
    const auto source = R"(
        async i32 compute() {
            return 1;
        }

        async i32 main() {
            i32 r = await compute();
            return r;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .generateBinary = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    // Verify coro wrapper functions are generated
    EXPECT_NE(result.ir.find("__djinn_coro_resume"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_coro_done"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_coro_destroy"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_coro_promise"), std::string::npos);
}

// ========================
// Backward Compatibility: non-async main with await still uses busy-loop
// ========================

TEST(Async, NonAsyncMainStillWorks)
{
    const auto source = R"(
        async i32 compute() {
            return 77;
        }

        i32 main() {
            i32 result = await compute();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 77);
}

TEST(Async, NonAsyncMainWithRuntime)
{
    const auto source = R"(
        async i32 compute() {
            return 1;
        }

        i32 main() {
            i32 result = await compute();
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .generateBinary = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
    // Sync main with async functions needs runtime for nested await support
    EXPECT_NE(result.ir.find("__djinn_runtime_init"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_event_loop_run"), std::string::npos);
    EXPECT_NE(result.ir.find("__djinn_sync_main"), std::string::npos);
}

// ========================
// Spawn: multiple spawns parse
// ========================

TEST(Spawn, MultipleSpawnsParse)
{
    const auto source = R"(
        async i32 work(i32 x) {
            return x;
        }

        async i32 main() {
            spawn work(1);
            spawn work(2);
            spawn work(3);
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .generateBinary = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}

// ========================
// Spawn with Yield
// ========================

TEST(Spawn, WithYieldParse)
{
    const auto source = R"(
        async i32 worker() {
            yield;
            yield;
            return 0;
        }

        async i32 main() {
            spawn worker();
            yield;
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source, {.optimize = false, .generateBinary = false});
    EXPECT_EQ(result.diagnostics.size(), 0);
}
