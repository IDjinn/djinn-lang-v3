#include <gtest/gtest.h>
#include "../diagnostics/Diagnostic.h"

TEST(Diagnostics, BasicError) {
    const std::string source = R"(i32 main() {
    return x;
})";

    DiagnosticEngine engine(source);

    Diagnostic diag(Severity::Error, DiagnosticCode::UNDEFINED_VARIABLE,
                    "variavel nao definida: `x`", SourceLocation(2, 12, 1));
    diag.withLabel("nao encontrada neste escopo");

    engine.emit(diag);

    EXPECT_TRUE(engine.hasErrors());
    EXPECT_EQ(engine.errorCount(), 1);

    std::string output = engine.render();
    EXPECT_TRUE(output.find("E3001") != std::string::npos);
    EXPECT_TRUE(output.find("variavel nao definida") != std::string::npos);
}

TEST(Diagnostics, ErrorWithHelp) {
    const std::string source = R"(struct Point {
    i32 x;
    i32 y;
}

i32 main() {
    Point p = { .z = 10 };
    return 0;
})";

    DiagnosticEngine engine(source);

    Diagnostic diag(Severity::Error, DiagnosticCode::UNDEFINED_FIELD,
                    "campo nao encontrado: `z`", SourceLocation(7, 17, 1));
    diag.withLabel("campo desconhecido")
            .withHelp("campos disponiveis: `x`, `y`");

    engine.emit(diag);

    EXPECT_TRUE(engine.hasErrors());
    const std::string output = engine.render();
    EXPECT_TRUE(output.find("campos disponiveis") != std::string::npos);
}

TEST(Diagnostics, MultipleErrors) {
    const std::string source = "i32 x = ;";

    DiagnosticEngine engine(source);

    engine.error(DiagnosticCode::EXPECTED_EXPRESSION, "expressao esperada", SourceLocation(1, 9, 1));
    engine.warning(DiagnosticCode::UNEXPECTED_TOKEN, "token inesperado", SourceLocation(1, 9, 1));

    EXPECT_EQ(engine.errorCount(), 1);
    EXPECT_EQ(engine.warningCount(), 1);
}

TEST(Diagnostics, CodeFormatting) {
    const std::string source = "test";
    DiagnosticEngine engine(source);

    // Empty engine should not have errors
    EXPECT_FALSE(engine.hasErrors());

    engine.error(DiagnosticCode::UNEXPECTED_CHARACTER, "caractere inesperado", SourceLocation(1, 1, 1));

    const std::string output = engine.render();
    // Code should be E1001 (UNEXPECTED_CHARACTER = 1001)
    EXPECT_TRUE(output.find("E1001") != std::string::npos);
}