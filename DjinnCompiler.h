//
// Created by Luke on 29/12/2025.
//

#ifndef DJINN_DJINNCOMPILER_H
#define DJINN_DJINNCOMPILER_H
#include <string>
#include <vector>
#include <filesystem>

#include "diagnostics/Diagnostic.h"
#include "lexer/Token.h"
#include "parser/AST.h"

struct CompilerOptions {
    bool optimize = true;
    bool executeAfterCompile = true;
    std::string outputFileName{};
    std::string outputDirectory{"build"};
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

    static CompilerResult runFromFile(const std::filesystem::path &filePath, CompilerOptions options = {});

    static CompilerResult runFromFiles(const std::vector<std::filesystem::path> &filePaths,
                                       CompilerOptions options = {});

    // Compila com um Program já parseado (usado quando múltiplos arquivos são parseados separadamente)
    static CompilerResult runFromProgram(std::unique_ptr<Program> program, CompilerOptions options = {});

private:
    // Mescla múltiplos Programs em um único, respeitando namespaces de cada arquivo
    static void mergePrograms(Program &target, std::unique_ptr<Program> source);
};


#endif //DJINN_DJINNCOMPILER_H