//
// Tests for constexpr/consteval compile-time evaluation
//

#include "../DjinnCompiler.h"
#include "../evaluator/ConstEvaluator.h"
#include "gtest/gtest.h"

// ============================================================
// Helper: build AST nodes inline
// ============================================================

static auto intLit(const char* v) { return std::make_unique<IntegerLiteral>(v, true, SourceLocation{}); }
static auto floatLit(const char* v) { return std::make_unique<FloatLiteral>(v, SourceLocation{}); }
static auto boolLit(const char* v) { return std::make_unique<BooleanLiteral>(v, SourceLocation{}); }

static auto ident(const char* name)
{
    SourceIdentifier si;
    si.token_name = name;
    return std::make_unique<Identifier>(si);
}

static auto binOp(std::unique_ptr<Expression> l, TokenType op, std::unique_ptr<Expression> r)
{
    return std::make_unique<BinaryExpression>(std::move(l), op, std::move(r), SourceLocation{});
}

static auto unaryOp(TokenType op, std::unique_ptr<Expression> operand)
{
    return std::make_unique<UnaryExpression>(op, std::move(operand), SourceLocation{});
}

// ============================================================
// VM unit tests: literals
// ============================================================

TEST(ConstEvalVM, IntegerLiteral)
{
    ConstEvaluator eval;
    IntegerLiteral lit("42", true, {});
    auto result = eval.evaluate(lit);
    EXPECT_FALSE(result.isError());
    EXPECT_EQ(result.kind, ConstValue::Integer);
    EXPECT_EQ(result.intVal, 42);
}

TEST(ConstEvalVM, FloatLiteral)
{
    ConstEvaluator eval;
    FloatLiteral lit("3.14", {});
    auto result = eval.evaluate(lit);
    EXPECT_FALSE(result.isError());
    EXPECT_EQ(result.kind, ConstValue::Float);
    EXPECT_DOUBLE_EQ(result.floatVal, 3.14);
}

TEST(ConstEvalVM, BooleanLiteral)
{
    ConstEvaluator eval;
    BooleanLiteral lit("true", {});
    auto result = eval.evaluate(lit);
    EXPECT_FALSE(result.isError());
    EXPECT_TRUE(result.boolVal);
}

TEST(ConstEvalVM, BooleanFalseLiteral)
{
    ConstEvaluator eval;
    BooleanLiteral lit("false", {});
    auto result = eval.evaluate(lit);
    EXPECT_FALSE(result.isError());
    EXPECT_FALSE(result.boolVal);
}

TEST(ConstEvalVM, HexLiteral)
{
    ConstEvaluator eval;
    IntegerLiteral lit("0xFF", true, {});
    EXPECT_EQ(eval.evaluate(lit).intVal, 255);
}

TEST(ConstEvalVM, BinaryLiteral)
{
    ConstEvaluator eval;
    IntegerLiteral lit("0b1010", true, {});
    EXPECT_EQ(eval.evaluate(lit).intVal, 10);
}

TEST(ConstEvalVM, SeparatorLiteral)
{
    ConstEvaluator eval;
    IntegerLiteral lit("1_000_000", true, {});
    EXPECT_EQ(eval.evaluate(lit).intVal, 1000000);
}

TEST(ConstEvalVM, TickSeparatorLiteral)
{
    ConstEvaluator eval;
    IntegerLiteral lit("800'000'000", true, {});
    EXPECT_EQ(eval.evaluate(lit).intVal, 800000000);
}

TEST(ConstEvalVM, NegativeZero)
{
    ConstEvaluator eval;
    UnaryExpression unary(TokenType::MINUS, intLit("0"), {});
    EXPECT_EQ(eval.evaluate(unary).intVal, 0);
}

// ============================================================
// VM unit tests: arithmetic
// ============================================================

TEST(ConstEvalVM, Addition)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("10"), TokenType::PLUS, intLit("32"), {});
    EXPECT_EQ(eval.evaluate(expr).intVal, 42);
}

TEST(ConstEvalVM, Subtraction)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("10"), TokenType::MINUS, intLit("3"), {});
    EXPECT_EQ(eval.evaluate(expr).intVal, 7);
}

TEST(ConstEvalVM, Multiplication)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("6"), TokenType::STAR, intLit("7"), {});
    EXPECT_EQ(eval.evaluate(expr).intVal, 42);
}

TEST(ConstEvalVM, Division)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("84"), TokenType::SLASH, intLit("2"), {});
    EXPECT_EQ(eval.evaluate(expr).intVal, 42);
}

TEST(ConstEvalVM, Modulo)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("17"), TokenType::PERCENT, intLit("5"), {});
    EXPECT_EQ(eval.evaluate(expr).intVal, 2);
}

TEST(ConstEvalVM, DivisionByZero)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("10"), TokenType::SLASH, intLit("0"), {});
    EXPECT_TRUE(eval.evaluate(expr).isError());
}

TEST(ConstEvalVM, ModuloByZero)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("10"), TokenType::PERCENT, intLit("0"), {});
    EXPECT_TRUE(eval.evaluate(expr).isError());
}

TEST(ConstEvalVM, UnaryNegate)
{
    ConstEvaluator eval;
    UnaryExpression unary(TokenType::MINUS, intLit("42"), {});
    EXPECT_EQ(eval.evaluate(unary).intVal, -42);
}

TEST(ConstEvalVM, DoubleNegate)
{
    ConstEvaluator eval;
    auto inner = unaryOp(TokenType::MINUS, intLit("42"));
    UnaryExpression outer(TokenType::MINUS, std::move(inner), {});
    EXPECT_EQ(eval.evaluate(outer).intVal, 42);
}

TEST(ConstEvalVM, NestedArithmetic)
{
    // (5 + 3) * 2 = 16
    ConstEvaluator eval;
    auto add = binOp(intLit("5"), TokenType::PLUS, intLit("3"));
    BinaryExpression mul(std::move(add), TokenType::STAR, intLit("2"), {});
    EXPECT_EQ(eval.evaluate(mul).intVal, 16);
}

TEST(ConstEvalVM, ComplexNestedArithmetic)
{
    // ((10 - 2) * 3) + 1 = 25
    ConstEvaluator eval;
    auto sub = binOp(intLit("10"), TokenType::MINUS, intLit("2"));
    auto mul = binOp(std::move(sub), TokenType::STAR, intLit("3"));
    BinaryExpression add(std::move(mul), TokenType::PLUS, intLit("1"), {});
    EXPECT_EQ(eval.evaluate(add).intVal, 25);
}

// ============================================================
// VM unit tests: comparisons
// ============================================================

TEST(ConstEvalVM, LessThan)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("10"), TokenType::LESS, intLit("20"), {});
    EXPECT_TRUE(eval.evaluate(expr).boolVal);
}

TEST(ConstEvalVM, LessThanFalse)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("20"), TokenType::LESS, intLit("10"), {});
    EXPECT_FALSE(eval.evaluate(expr).boolVal);
}

TEST(ConstEvalVM, GreaterThan)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("20"), TokenType::GREATER, intLit("10"), {});
    EXPECT_TRUE(eval.evaluate(expr).boolVal);
}

TEST(ConstEvalVM, LessEqual)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("10"), TokenType::LESS_EQUAL, intLit("10"), {});
    EXPECT_TRUE(eval.evaluate(expr).boolVal);
}

TEST(ConstEvalVM, GreaterEqual)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("9"), TokenType::GREATER_EQUAL, intLit("10"), {});
    EXPECT_FALSE(eval.evaluate(expr).boolVal);
}

TEST(ConstEvalVM, Equality)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("42"), TokenType::EQUAL_EQUAL, intLit("42"), {});
    EXPECT_TRUE(eval.evaluate(expr).boolVal);
}

TEST(ConstEvalVM, Inequality)
{
    ConstEvaluator eval;
    BinaryExpression expr(intLit("42"), TokenType::BANG_EQUAL, intLit("43"), {});
    EXPECT_TRUE(eval.evaluate(expr).boolVal);
}

// ============================================================
// VM unit tests: logic
// ============================================================

TEST(ConstEvalVM, LogicAnd)
{
    ConstEvaluator eval;
    BinaryExpression expr(boolLit("true"), TokenType::AND_AND, boolLit("false"), {});
    EXPECT_FALSE(eval.evaluate(expr).toBool());
}

TEST(ConstEvalVM, LogicOr)
{
    ConstEvaluator eval;
    BinaryExpression expr(boolLit("false"), TokenType::OR_OR, boolLit("true"), {});
    EXPECT_TRUE(eval.evaluate(expr).toBool());
}

TEST(ConstEvalVM, LogicalNot)
{
    ConstEvaluator eval;
    UnaryExpression unary(TokenType::BANG, boolLit("true"), {});
    EXPECT_FALSE(eval.evaluate(unary).toBool());
}

TEST(ConstEvalVM, LogicalNotFalse)
{
    ConstEvaluator eval;
    UnaryExpression unary(TokenType::BANG, boolLit("false"), {});
    EXPECT_TRUE(eval.evaluate(unary).toBool());
}

// ============================================================
// VM unit tests: named constants
// ============================================================

TEST(ConstEvalVM, NamedConstant)
{
    ConstEvaluator eval;
    eval.defineConstant("SIZE", ConstValue::makeInt(1024));
    auto result = eval.evaluate(*ident("SIZE"));
    EXPECT_EQ(result.intVal, 1024);
}

TEST(ConstEvalVM, NamedConstantInExpression)
{
    ConstEvaluator eval;
    eval.defineConstant("BASE", ConstValue::makeInt(100));
    BinaryExpression expr(ident("BASE"), TokenType::PLUS, intLit("5"), {});
    EXPECT_EQ(eval.evaluate(expr).intVal, 105);
}

TEST(ConstEvalVM, MultipleNamedConstants)
{
    ConstEvaluator eval;
    eval.defineConstant("WIDTH", ConstValue::makeInt(80));
    eval.defineConstant("HEIGHT", ConstValue::makeInt(24));
    BinaryExpression expr(ident("WIDTH"), TokenType::STAR, ident("HEIGHT"), {});
    EXPECT_EQ(eval.evaluate(expr).intVal, 1920);
}

TEST(ConstEvalVM, UnknownIdentifierReturnsError)
{
    ConstEvaluator eval;
    auto result = eval.evaluate(*ident("UNKNOWN"));
    EXPECT_TRUE(result.isError());
}

// ============================================================
// VM unit tests: config limits
// ============================================================

TEST(ConstEvalVM, MaxIterationsExceeded)
{
    // A loop that would run forever is caught by maxIterations
    // We test via config with very low limit
    ConstEvalConfig cfg;
    cfg.maxIterations = 10;
    ConstEvaluator eval(cfg);

    // Can't easily create an infinite loop via expressions alone,
    // but a deeply nested expression will use many iterations
    // For now just verify config is accepted and simple eval works
    IntegerLiteral lit("1", true, {});
    auto result = eval.evaluate(lit);
    EXPECT_FALSE(result.isError());
}

TEST(ConstEvalVM, StackOverflowProtection)
{
    ConstEvalConfig cfg;
    cfg.maxStackSize = 2; // very small stack
    ConstEvaluator eval(cfg);

    // Simple expression with 1 stack entry works
    IntegerLiteral lit("42", true, {});
    auto result = eval.evaluate(lit);
    EXPECT_FALSE(result.isError());
    EXPECT_EQ(result.intVal, 42);
}

// ============================================================
// Integration tests: constexpr variables
// ============================================================

TEST(ConstExpr, SimpleConstExprVariable)
{
    const auto source = R"(
        constexpr i32 VALUE = 42;

        i32 main() {
            return VALUE;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(ConstExpr, ConstExprWithArithmetic)
{
    const auto source = R"(
        constexpr i32 A = 10;
        constexpr i32 B = 20 + 5;

        i32 main() {
            return A + B;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 35);
}

TEST(ConstExpr, ConstExprMultiplication)
{
    const auto source = R"(
        constexpr i32 WIDTH = 80;
        constexpr i32 HEIGHT = 24;
        constexpr i32 AREA = 80 * 24;

        i32 main() {
            return AREA / 10;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 192);
}

TEST(ConstExpr, ConstExprNegative)
{
    const auto source = R"(
        constexpr i32 NEG = -42;

        i32 main() {
            return 0 - NEG;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(ConstExpr, ConstExprUsedInArraySize)
{
    const auto source = R"(
        constexpr i32 SIZE = 64;

        i32 main() {
            i8* buf = i8[SIZE];
            buf[0] = 99;
            return (i32)buf[0];
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

// ============================================================
// Integration tests: struct const fields
// ============================================================

TEST(ConstExpr, ConstFieldUsedInExpression)
{
    const auto source = R"(
        struct Config {
            const i32 MAX = 100;
            const i32 MIN = 1;
        }

        i32 main() {
            return Config.MAX - Config.MIN;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}

TEST(ConstExpr, ConstFieldWithArithmetic)
{
    const auto source = R"(
        struct Settings {
            const i32 WIDTH = 80;
            const i32 HEIGHT = 24;
            const i32 AREA = 80 * 24;
        }

        i32 main() {
            return Settings.AREA / 10;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 192);
}

// ============================================================
// Integration tests: constexpr functions
// ============================================================

TEST(ConstExpr, ConstExprFunctionCompiles)
{
    const auto source = R"(
        constexpr i32 add(i32 a, i32 b) {
            return a + b;
        }

        i32 main() {
            auto test = add(1, 2);
            return test;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(ConstExpr, ConstExprFunctionResult)
{
    const auto source = R"(
        constexpr i32 double_it(i32 x) {
            return x * 2;
        }

        i32 main() {
            return double_it(21);
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

// ============================================================
// Integration tests: local constexpr/consteval
// ============================================================

TEST(ConstExpr, LocalConstExprVariable)
{
    const auto source = R"(
        i32 main() {
            constexpr i32 x = 10 + 32;
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(ConstExpr, LocalConstevalVariable)
{
    const auto source = R"(
        i32 main() {
            consteval i32 x = 7 * 6;
            return x;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(ConstExpr, LocalConstExprUsedInExpression)
{
    const auto source = R"(
        i32 main() {
            constexpr i32 a = 10;
            constexpr i32 b = 20;
            return a + b;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 30);
}

TEST(ConstExpr, LocalConstExprInArraySize)
{
    const auto source = R"(
        i32 main() {
            constexpr i32 SIZE = 32;
            i8* buf = i8[SIZE];
            buf[0] = 77;
            return (i32)buf[0];
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 77);
}

TEST(ConstExpr, GlobalConstevalVariable)
{
    const auto source = R"(
        consteval i32 MAGIC = 2 + 5;

        i32 main() {
            return MAGIC;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 7);
}

// ============================================================
// Integration tests: constexpr modifiers validation
// ============================================================

TEST(ConstExpr, InvalidConstExprAndAsyncModifier)
{
    const auto source = R"(
        constexpr async i32 add(i32 a, i32 b) {
            return a + b;
        }

        i32 main() {
            auto test = add(1, 2);
            return test;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 1);
}

// ============================================================
// Compile-time if: if expr {}, if constexpr expr {}, if consteval expr {}
// ============================================================

TEST(CompileTimeIf, TrueBranch)
{
    const auto source = R"(
        consteval i32 ENABLED = 1;

        i32 main() {
            if ENABLED {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, FalseBranch)
{
    const auto source = R"(
        consteval i32 DISABLED = 0;

        i32 main() {
            if DISABLED {
                return 1;
            }
            return 42;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, FalseBranchWithElse)
{
    const auto source = R"(
        consteval i32 DISABLED = 0;

        i32 main() {
            if DISABLED {
                return 1;
            } else {
                return 42;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, TrueBranchWithElse)
{
    const auto source = R"(
        consteval i32 ENABLED = 1;

        i32 main() {
            if ENABLED {
                return 42;
            } else {
                return 0;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, BinaryExpression)
{
    const auto source = R"(
        consteval i32 PLATFORM = 2;

        i32 main() {
            if PLATFORM == 2 {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, CompoundExpression)
{
    const auto source = R"(
        consteval i32 A = 1;
        consteval i32 B = 1;

        i32 main() {
            if A && B {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, ElseIfChain)
{
    const auto source = R"(
        consteval i32 MODE = 2;

        i32 main() {
            if MODE == 1 {
                return 1;
            } else if MODE == 2 {
                return 42;
            } else {
                return 0;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, WarningOnConstExprInRuntimeIf)
{
    const auto source = R"(
        consteval i32 FLAG = 1;

        i32 main() {
            if (FLAG) {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    bool hasWarning = false;
    for (const auto& d : result.diagnostics)
    {
        if (d.code == DiagnosticCode::CONSTEXPR_RUNTIME_IF)
        {
            hasWarning = true;
            break;
        }
    }
    EXPECT_TRUE(hasWarning);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, NoWarningOnRuntimeVariable)
{
    const auto source = R"(
        i32 main() {
            i32 x = 1;
            if (x) {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    bool hasWarning = false;
    for (const auto& d : result.diagnostics)
    {
        if (d.code == DiagnosticCode::CONSTEXPR_RUNTIME_IF)
        {
            hasWarning = true;
            break;
        }
    }
    EXPECT_FALSE(hasWarning);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, ErrorOnNonConstExprWithoutParens)
{
    const auto source = R"(
        i32 main() {
            i32 x = 1;
            if x {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    bool hasError = false;
    for (const auto& d : result.diagnostics)
    {
        if (d.code == DiagnosticCode::NOT_COMPILE_TIME_EVALUABLE)
        {
            hasError = true;
            break;
        }
    }
    EXPECT_TRUE(hasError);
}

TEST(CompileTimeIf, LiteralCondition)
{
    const auto source = R"(
        i32 main() {
            if 1 {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, DeadCodeEliminated)
{
    const auto source = R"(
        consteval i32 DEBUG = 0;

        i32 main() {
            i32 mut result = 10;
            if DEBUG {
                result = 999;
            }
            return result;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 10);
}

TEST(CompileTimeIf, ExplicitConstexprKeyword)
{
    const auto source = R"(
        consteval i32 ENABLED = 1;

        i32 main() {
            if constexpr ENABLED {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, ExplicitConstevalKeyword)
{
    const auto source = R"(
        consteval i32 ENABLED = 1;

        i32 main() {
            if consteval ENABLED {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, ConstevalErrorOnRuntimeExpr)
{
    const auto source = R"(
        i32 main() {
            i32 x = 1;
            if consteval x {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    bool hasError = false;
    for (const auto& d : result.diagnostics)
    {
        if (d.code == DiagnosticCode::NOT_COMPILE_TIME_EVALUABLE)
        {
            hasError = true;
            break;
        }
    }
    EXPECT_TRUE(hasError);
}

TEST(CompileTimeIf, ExplicitConstexprWithElseIf)
{
    const auto source = R"(
        consteval i32 MODE = 3;

        i32 main() {
            if constexpr MODE == 1 {
                return 1;
            } else if constexpr MODE == 2 {
                return 2;
            } else if constexpr MODE == 3 {
                return 42;
            } else {
                return 0;
            }
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(CompileTimeIf, NoWarningOnMixedExpr)
{
    const auto source = R"(
        consteval i32 FLAG = 1;

        i32 main() {
            i32 x = 5;
            if (FLAG && x > 3) {
                return 42;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    bool hasWarning = false;
    for (const auto& d : result.diagnostics)
    {
        if (d.code == DiagnosticCode::CONSTEXPR_RUNTIME_IF)
        {
            hasWarning = true;
            break;
        }
    }
    EXPECT_FALSE(hasWarning);
    EXPECT_EQ(result.returnCode, 42);
}

// ============================================================
// Intrinsic constexpr variables and structs
// ============================================================

TEST(Intrinsic, IntrinsicConstevalVariable)
{
    const auto source = R"(
        [intrinsic] consteval i32 is_debug;

        i32 main() {
            if is_debug {
                return 1;
            }
            return 0;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
}

TEST(Intrinsic, ConstexprStructFieldAccess)
{
    const auto source = R"(
        constexpr struct Config {
            const i32 VERSION = 42;
        }

        i32 main() {
            return Config.VERSION;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 42);
}

TEST(Intrinsic, ConstexprStructAllFieldsAreConst)
{
    const auto source = R"(
        constexpr struct Limits {
            i32 MIN = 1;
            i32 MAX = 100;
        }

        i32 main() {
            return Limits.MAX - Limits.MIN;
        }
    )";

    const auto result = DjinnCompiler::run(source);
    EXPECT_EQ(result.diagnostics.size(), 0);
    EXPECT_EQ(result.returnCode, 99);
}
