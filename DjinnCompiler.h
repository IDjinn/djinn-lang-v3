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

struct RuntimeProperties
{
    std::string loggerLevel = "INFO";
    std::string loggerFormat;

    void serialize(std::ostream& out) const
    {
        out << "logger.level=" << loggerLevel << "\n";
        if (!loggerFormat.empty())
            out << "logger.format=" << loggerFormat << "\n";
    }
};

struct CompilerOptions
{
    bool print_ast = false;
    bool print_ir = false;
    bool print_macro_expansion = false;
    bool dump_macro_expansion = false;
    int optimizationLevel = 3;
    bool skipCoroPasses = false;
    bool generateBinary = false;
    bool includeStd = true;
    bool stdDeclOnly = false;
    bool silentMode = false;
    bool libraryMode = false;
    bool bundleModules = false;
    bool useTempDirectory = true; // Use system temp directory for output files
    bool runAfterCompile = true; // Run the compiled binary and capture exit code
    bool jitExecution = true; // Execute in-process via ORC JIT instead of spawning clang (run() only)
    bool noCache = false;
    bool debugMode = true;
    bool releaseMode = false;
    bool outputDjLib = false;
    std::string reflectionMode = "none"; // none | annotated | all
    std::string outputFileName{};
    std::string outputDirectory{"build"};
    std::filesystem::path stdLibPath{"./std"};
    std::vector<std::filesystem::path> linkLibraries{};
    std::vector<std::filesystem::path> djlibPaths{};
    RuntimeProperties runtimeProperties{};
};

struct CompilerResult
{
    int returnCode;
    bool verifiedIr = true;
    std::vector<Token> tokens{};
    std::shared_ptr<Program> program;
    std::string ir;
    std::vector<Diagnostic> diagnostics{};
    std::string expandedSource;
    // Full rendered runtime error report (source snippet, caret, operand
    // values, stack trace) when the program trapped; JIT path only.
    std::string runtimeErrorReport;
};


struct DjinnCompiler
{
    static CompilerResult compileFromDirectory(const std::filesystem::path& path, const CompilerOptions& options = {});

    static CompilerResult run(const std::string& source, const CompilerOptions& options = {});
};


#endif //DJINN_DJINNCOMPILER_H