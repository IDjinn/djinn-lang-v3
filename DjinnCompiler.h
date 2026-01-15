//
// Created by Luke on 29/12/2025.
//

#ifndef DJINN_DJINNCOMPILER_H
#define DJINN_DJINNCOMPILER_H
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

#include "diagnostics/Diagnostic.h"
#include "lexer/Token.h"
#include "parser/AST.h"

struct CompilerOptions {
    bool print_ast = false;
    bool print_ir = false;
    bool optimize = true;
    bool executeAfterCompile = true;
    bool includeStd = true; // Include standard library by default
    bool stdDeclOnly = false; // Only include std declarations (no code gen), use with -l std.ll
    bool silentMode = false; // Don't print to stderr (for LSP mode)
    bool libraryMode = false; // Compile as library (no main required, emit all symbols)
    bool bundleModules = false; // Bundle all files into single .ll (default: per namespace)
    std::string outputFileName{};
    std::string outputDirectory{"build"};
    std::filesystem::path stdLibPath{"std"}; // Path to standard library
    std::vector<std::filesystem::path> linkLibraries{}; // Pre-compiled .ll files to link
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

    // Generate separate .ll per namespace (for library compilation)
    static CompilerResult runPerNamespace(const std::vector<std::filesystem::path> &filePaths,
                                          CompilerOptions options = {});

    static CompilerResult runFromProgram(std::unique_ptr<Program> program, CompilerOptions options = {},
                                         std::unordered_map<std::string, std::string> sources = {});

private:
    static void mergePrograms(Program &target, std::unique_ptr<Program> source);

    static std::unique_ptr<Program> loadStdLibrary(const std::filesystem::path &stdLibPath);

    static std::vector<std::filesystem::path> collectStdFiles(const std::filesystem::path &stdLibPath);
};


#endif //DJINN_DJINNCOMPILER_H