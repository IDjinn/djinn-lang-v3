//
// Created by Luke on 29/12/2025.
//

#ifndef DJINN_DJINNCOMPILER_H
#define DJINN_DJINNCOMPILER_H
#include <string>
#include <vector>
#include <set>
#include <map>
#include <filesystem>

#include "diagnostics/Diagnostic.h"
#include "lexer/Token.h"
#include "parser/AST.h"

struct CompilerOptions
{
    bool print_ast = true;
    bool print_ir = true;
    bool optimize = true;
    bool generateBinary = true;
    bool includeStd = true;
    bool stdDeclOnly = false;
    bool silentMode = false;
    bool libraryMode = false;
    bool bundleModules = false;
    bool useTempDirectory = true; // Use system temp directory for output files
    bool runAfterCompile = true; // Run the compiled binary and capture exit code
    std::string outputFileName{};
    std::string outputDirectory{"build"};
    std::filesystem::path stdLibPath{"./std"};
    std::vector<std::filesystem::path> linkLibraries{};
};

struct CompilerResult
{
    int returnCode;
    std::vector<Token> tokens{};
    std::shared_ptr<Program> program;
    std::string ir;
    std::vector<Diagnostic> diagnostics{};
};


struct DjinnCompiler
{
    static CompilerResult compileFromDirectory(const std::filesystem::path& path, const CompilerOptions& options = {});

    static CompilerResult run(const std::string& source, const CompilerOptions& options = {});
};


#endif //DJINN_DJINNCOMPILER_H