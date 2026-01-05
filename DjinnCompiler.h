//
// Created by Luke on 29/12/2025.
//

#ifndef DJINN_DJINNCOMPILER_H
#define DJINN_DJINNCOMPILER_H
#include <string>

#include "diagnostics/Diagnostic.h"
#include "lexer/Token.h"
#include "parser/AST.h"


struct CompilerOptions {
    bool optimize = true;
    std::string outputFileName{};
};

struct CompilerResult {
    int returnCode;
    std::vector<Token> tokens{};
    std::unique_ptr<Program> program;
    std::string ir;
    std::vector<Diagnostic> diagnostics{};
};

struct DjinnCompiler {
    static CompilerResult run(const std::string &source, CompilerOptions options = {});
};


#endif //DJINN_DJINNCOMPILER_H