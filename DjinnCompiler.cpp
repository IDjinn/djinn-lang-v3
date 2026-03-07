//
// Created by Luke on 29/12/2025.
//

#include "DjinnCompiler.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stacktrace>

#include "binder/Binder.h"
#include "generator/Generator.h"
#include "lexer/Lexer.h"
#include "parser/parser.h"
#include "utils/Logger.h"
#include "utils/StopWatch.h"

#ifndef DJINN_CLANG_PATH
#define DJINN_CLANG_PATH "clang"
#endif

// NOTE: -fsanitize=address is incompatible with LLVM coroutines (false positives
// on coro frame access after resume). Use __djinn_malloc/__djinn_free runtime
// tracing instead for memory debugging.
#define CLANG_ARGS "-O2 "

const std::string preludes[] = {
    // DO NOT TOUCH IT! ORDERING MATTERS
    "sys/debug.djinn",
    "sys/console.djinn",
    "types/types.djinn",
    "builtin/coro.djinn",
};


// Resolve std library path: try the given path first, then try relative to this source file
static std::filesystem::path resolve_std_path(const std::filesystem::path& given)
{
    namespace fs = std::filesystem;
    if (fs::exists(given)) return given;

    // Fallback: resolve relative to this source file's directory (project root)
    const fs::path thisFile(__FILE__);
    const auto projectRoot = thisFile.parent_path();
    const auto candidate = projectRoot / "std";
    if (fs::exists(candidate)) return candidate;

    return given;
}

// Resolve runtime C file path
static std::filesystem::path resolve_runtime_path()
{
    namespace fs = std::filesystem;

    // Try relative to current directory
    if (fs::exists("runtime/djinn_runtime.c")) return "runtime/djinn_runtime.c";

    // Try relative to this source file
    const fs::path thisFile(__FILE__);
    const auto projectRoot = thisFile.parent_path();
    const auto candidate = projectRoot / "runtime" / "djinn_runtime.c";
    if (fs::exists(candidate)) return candidate;

    return "";
}

CompilerResult DjinnCompiler::compileFromDirectory(const std::filesystem::path& path, const CompilerOptions& options)
{
    utils::StopWatch global_watch("build time");
    namespace fs = std::filesystem;
    assert(!options.outputDirectory.empty() && "You need give output directory!");

    fs::create_directories(options.outputDirectory);
    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program>> programs;

    // Prelude type names (from std::types) available to all parsers
    std::vector<std::string> preludeTypeNames;
    std::vector<std::string> stdTypeNames;

    const auto parseFile = [&](const fs::path& filePath, const bool registerPrelude, const bool isUserCode)
    {
        std::ifstream file(filePath);
        if (!file) return;

        const auto source = std::string(
            std::istreambuf_iterator(file),
            std::istreambuf_iterator<char>()
        );

        auto file_name = filePath.string();
        diagnostics.registerSource(file_name, source);

        Lexer lexer(source, file_name);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens, diagnostics);

        // Register prelude types for std lib and user code parsers
        if (registerPrelude)
        {
            for (const auto& name : preludeTypeNames)
                parser.registerKnownType(name);
        }
        // Register all std types for user code parsers
        if (isUserCode)
        {
            for (const auto& name : stdTypeNames)
                parser.registerKnownType(name);
        }

        auto program = parser.parse(file_name);

        if (options.print_ast && isUserCode)
        {
            std::ostringstream oss;
            oss << "=====AST [" << file_name << "]=====\n";
            program->print(oss);
            oss << "=====";
            LOG_DEBUG("%s", oss.str().c_str());
        }

        programs.emplace_back(std::move(program));
    };

    // Resolve std path once, canonicalize for comparison
    const auto stdLibPath = resolve_std_path(options.stdLibPath);
    const auto stdCanonical = fs::exists(stdLibPath) ? fs::canonical(stdLibPath) : fs::path{};

    // Track parsed files to avoid duplicates
    std::set<fs::path> parsedFiles;

    try
    {
        // ── Phase 1: Prelude (std/types/types.djinn) ──
        if (options.includeStd && !stdCanonical.empty())
        {
            for (const auto& prelude : preludes)
            {
                const auto preludePath = stdLibPath / prelude;
                if (!fs::exists(preludePath))
                {
                    LOG_ERROR("Prelude file %s was not found!", preludePath.c_str());
                    continue;
                }

                try
                {
                    parsedFiles.insert(fs::canonical(preludePath));
                    parseFile(preludePath, false, false);
                    if (!programs.empty())
                    {
                        const auto& preludeProgram = programs.back();
                        for (const auto& s : preludeProgram->structs) preludeTypeNames.push_back(s->name.token_name);
                        for (const auto& e : preludeProgram->enums) preludeTypeNames.push_back(e->name.token_name);
                    }
                }
                catch (const CompileError& compile_error)
                {
                    LOG_ERROR("Error parsing prelude: %s", compile_error.message().c_str());
                }
            }

            // ── Phase 2: Remaining std library files ──
            for (const auto& entry : fs::recursive_directory_iterator(stdLibPath))
            {
                try
                {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".djinn") continue;

                    auto canonical = fs::canonical(entry.path());
                    if (parsedFiles.contains(canonical)) continue;
                    parsedFiles.insert(canonical);

                    parseFile(entry.path(), true, false);
                }
                catch (const CompileError& compile_error)
                {
                    LOG_ERROR("Error parsing %s: %s", entry.path().string().c_str(), compile_error.message().c_str());
                }
            }
        }

        // Collect all std library type names for user code parsers
        for (const auto& prog : programs)
        {
            for (const auto& s : prog->structs) stdTypeNames.push_back(s->name.token_name);
            for (const auto& e : prog->enums) stdTypeNames.push_back(e->name.token_name);
        }

        // ── Phase 3: User code (skip anything already parsed or inside std) ──
        if (fs::exists(path))
        {
            for (const auto& entry : fs::recursive_directory_iterator(path))
            {
                try
                {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".djinn") continue;

                    auto canonical = fs::canonical(entry.path());
                    if (parsedFiles.contains(canonical)) continue;

                    // Skip files inside std library directory
                    if (!stdCanonical.empty())
                    {
                        auto rel = canonical.lexically_relative(stdCanonical);
                        if (!rel.empty() && !rel.string().starts_with("..")) continue;
                    }

                    parsedFiles.insert(canonical);
                    parseFile(entry.path(), true, true);
                }
                catch (const CompileError& compile_error)
                {
                    LOG_ERROR("Error parsing %s: %s", entry.path().string().c_str(), compile_error.message().c_str());
                }
            }
        }

        if (programs.empty())
        {
            std::cerr << diagnostics.render();
            LOG_ERROR("No .djinn files found in %s", path.string().c_str());
            return {.returnCode = 1};
        }

        if (diagnostics.hasErrors())
        {
            std::cerr << diagnostics.render();
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        Binder binder(diagnostics);
        const auto bindResult = binder.bindAll(programs);
        if (!bindResult.success)
        {
            std::cerr << diagnostics.render();
            LOG_ERROR("Bind result return failed.");
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        auto generator = Generator(diagnostics, bindResult.globalScope);
        generator.generate();
        if (options.print_ir)
        {
            LOG_INFO("RESULT\n\n%s", generator.print().c_str());
        }

        const auto out_file_path = options.outputDirectory + "\\" + options.outputFileName;
        const std::string llPath = out_file_path + ".ll";

        // Run coroutine lowering + optional optimization in a single pass
        {
            utils::StopWatch pass_stop_watch("run passes");
            generator.run_passes(options.optimize);
        }

        std::string generatedIr = generator.print();
        std::ofstream outFile(llPath);
        outFile << generatedIr;
        outFile.close();

        if (options.generateBinary)
        {
            // Always link runtime (async by default — runtime needed for memory tracing and async infra)
            std::string runtimeArg;
            const auto runtimePath = resolve_runtime_path();
            if (!runtimePath.empty())
            {
                runtimeArg = " " + runtimePath.string();
            }

            const auto cmdString = "\"" DJINN_CLANG_PATH "\" " CLANG_ARGS " " + llPath + runtimeArg + " -o " +
                out_file_path +
                ".exe";
            LOG_DEBUG("Executing compilation command: %s", cmdString.c_str());
            system(cmdString.c_str());
        }


        return {.returnCode = 0, .diagnostics = diagnostics.get_diagnostics()};
    }
    catch (const CompileError& e)
    {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        diagnostics.printToStderr(std::stacktrace::current());
        return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
    }
}

CompilerResult DjinnCompiler::run(const std::string& source, const CompilerOptions& options)
{
    namespace fs = std::filesystem;

    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program>> programs;
    std::shared_ptr<Program> userProgram;
    std::string generatedIr;

    auto makeResult = [&](int returnCode, const std::stacktrace& trace)
    {
        if (!options.silentMode && !diagnostics.get_diagnostics().empty())
        {
            diagnostics.printToStderr(trace);
        }
        return CompilerResult{
            .returnCode = returnCode,
            .program = userProgram,
            .ir = generatedIr,
            .diagnostics = diagnostics.get_diagnostics()
        };
    };

    try
    {
        std::set<fs::path> parsedFiles;
        std::vector<std::string> preludeTypeNames;

        auto stdLibPath = resolve_std_path(options.stdLibPath);
        if (options.includeStd && fs::exists(stdLibPath))
        {
            for (const auto& prelude : preludes)
            {
                const auto preludePath = fs::path(stdLibPath) / prelude;
                if (!fs::exists(preludePath))
                    continue;

                try
                {
                    std::ifstream file(preludePath);
                    if (!file)
                    {
                        LOG_ERROR("Prelude file %s was not found!", preludePath.c_str());
                        continue;
                    }

                    parsedFiles.insert(fs::canonical(preludePath));
                    const auto stdSource = std::string(
                        std::istreambuf_iterator(file),
                        std::istreambuf_iterator<char>()
                    );

                    auto file_id = preludePath.string();
                    diagnostics.registerSource(file_id, stdSource);

                    Lexer lexer(stdSource);
                    const auto tokens = lexer.tokenize();

                    Parser parser(tokens, diagnostics);
                    auto program = parser.parse(file_id);

                    // Collect prelude type names for subsequent parsers
                    for (const auto& s : program->structs) preludeTypeNames.push_back(s->name.token_name);
                    for (const auto& e : program->enums) preludeTypeNames.push_back(e->name.token_name);

                    programs.emplace_back(std::move(program));
                }
                catch (const CompileError& compile_error)
                {
                    if (!options.silentMode)
                    {
                        LOG_ERROR("Error parsing prelude: %s", compile_error.message().c_str());
                    }
                }
            }

            // Phase 2: Parse remaining std library files (with prelude types registered)
            for (const auto& entry : fs::recursive_directory_iterator(stdLibPath))
            {
                try
                {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".djinn") continue;

                    auto canonical = fs::canonical(entry.path());
                    if (parsedFiles.contains(canonical)) continue;

                    // Skip prelude — already parsed
                    // if (fs::equivalent(entry.path(), preludePath)) continue;

                    std::ifstream file(entry.path());
                    if (!file) continue;

                    const auto stdSource = std::string(
                        std::istreambuf_iterator(file),
                        std::istreambuf_iterator<char>()
                    );

                    auto file_id = entry.path().string();
                    diagnostics.registerSource(file_id, stdSource);

                    Lexer lexer(stdSource);
                    const auto tokens = lexer.tokenize();

                    Parser parser(tokens, diagnostics);
                    // Register prelude types so other std files can use them
                    for (const auto& name : preludeTypeNames)
                        parser.registerKnownType(name);

                    auto program = parser.parse(file_id);

                    programs.emplace_back(std::move(program));
                }
                catch (const CompileError& compile_error)
                {
                    if (!options.silentMode)
                    {
                        LOG_ERROR("Error parsing std: %s: %s", entry.path().string().c_str(),
                                  compile_error.message().c_str());
                    }
                }
            }
        }

        // Parse user source
        diagnostics.registerSource("main", source);

        Lexer lexer(source);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens, diagnostics);

        // Pre-register prelude + std library types so parser recognizes them
        for (const auto& name : preludeTypeNames)
            parser.registerKnownType(name);
        for (const auto& prog : programs)
        {
            for (const auto& s : prog->structs) parser.registerKnownType(s->name.token_name);
            for (const auto& e : prog->enums) parser.registerKnownType(e->name.token_name);
        }

        auto program = parser.parse("main");

        if (options.print_ast)
        {
            std::ostringstream oss;
            oss << "=====AST=====\n";
            program->print(oss);
            oss << "=====";
            LOG_DEBUG("%s", oss.str().c_str());
        }

        userProgram = std::shared_ptr<Program>(std::move(program));
        programs.emplace_back(userProgram);

        if (diagnostics.hasErrors())
        {
            return makeResult(1, std::stacktrace::current());
        }

        Binder binder(diagnostics);
        const auto bindResult = binder.bindAll(programs);
        if (!bindResult.success)
        {
            return makeResult(1, std::stacktrace::current());
        }

        auto generator = Generator(diagnostics, bindResult.globalScope);
        generator.generate();

        if (options.print_ir)
        {
            LOG_INFO("RESULT\n\n%s", generator.print().c_str());
        }

        generatedIr = generator.print();

        std::string outputDir = options.outputDirectory;
        std::string outputFileName = options.outputFileName.empty() ? "main" : options.outputFileName;
        if (options.useTempDirectory)
        {
            outputDir = (fs::temp_directory_path() / "djinn_build" / std::to_string(rand())).string();
            fs::create_directories(outputDir);
        }

        const std::string llPath = outputDir + "\\" + outputFileName + ".ll";
        const std::string exePath = outputDir + "\\" + outputFileName + ".exe";

        // Run coroutine lowering + optional optimization in a single pass
        {
            utils::StopWatch pass_stop_watch("run passes");
            generator.run_passes(options.optimize);
        }

        generatedIr = generator.print();
        std::ofstream optOutput(llPath);
        optOutput << generatedIr;
        optOutput.close();

        if (!options.generateBinary)
        {
            return makeResult(0, std::stacktrace::current());
        }

        // Always link runtime (async by default — runtime needed for memory tracing and async infra)
        std::string runtimeArg;
        const auto runtimePath = resolve_runtime_path();
        if (!runtimePath.empty())
        {
            runtimeArg = " " + runtimePath.string();
        }

        const auto cmdString = "\"" DJINN_CLANG_PATH "\" " CLANG_ARGS " " + llPath + runtimeArg + " -o " + exePath;
        LOG_DEBUG("Executing compilation command: %s", cmdString.c_str());
        int clangResult = system(cmdString.c_str());
        if (clangResult != 0)
        {
            return makeResult(clangResult, std::stacktrace::current());
        }

        int programReturnCode = 0;
        if (options.runAfterCompile)
        {
            programReturnCode = system(exePath.c_str());
#ifndef _WIN32
            // On Unix, need to extract exit code with WEXITSTATUS
            if (WIFEXITED(programReturnCode))
            {
                programReturnCode = WEXITSTATUS(programReturnCode);
            }
#endif
        }

        return makeResult(programReturnCode, std::stacktrace::current());
    }
    catch (const CompileError& e)
    {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        return makeResult(1, std::stacktrace::current());
    }
}
