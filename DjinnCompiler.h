//
// Created by Luke on 29/12/2025.
//

#ifndef DJINN_DJINNCOMPILER_H
#define DJINN_DJINNCOMPILER_H
#include <string>

#include "lexer/Token.h"
#include "parser/AST.h"


struct CompilerOptions {
    bool optimize = true;
    std::string outputFileName = "output";
};

struct CompilerResult {
    int returnCode;
    std::vector<Token> tokens;
    std::unique_ptr<Program> program;
    std::string ir;
};

struct DjinnCompiler {
    static CompilerResult run(const std::string &source, const CompilerOptions &options = {});
};


#endif //DJINN_DJINNCOMPILER_H