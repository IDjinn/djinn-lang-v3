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

struct CompilerOptions {
    bool print_ast = false;
    bool print_ir = false;
    bool optimize = true;
    bool executeAfterCompile = true;
    bool includeStd = true;
    bool stdDeclOnly = false;
    bool silentMode = false;
    bool libraryMode = false;
    bool bundleModules = false;
    std::string outputFileName{};
    std::string outputDirectory{"build"};
    std::filesystem::path stdLibPath{"std"};
    std::vector<std::filesystem::path> linkLibraries{};
};

struct CompilerResult {
    int returnCode;
    std::vector<Token> tokens{};
    std::unique_ptr<Program> program;
    std::string ir;
    std::vector<Diagnostic> diagnostics{};
};


struct DjinnCompiler {
    static CompilerResult compileFromDirectory(const std::filesystem::path &path, const CompilerOptions &options = {});
};


#endif //DJINN_DJINNCOMPILER_H
