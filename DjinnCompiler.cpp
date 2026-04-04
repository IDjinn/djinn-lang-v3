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
#include "evaluator/ConstEvaluator.h"
#include "lexer/Lexer.h"
#include "parser/parser.h"
#include "utils/Logger.h"
#include "utils/StopWatch.h"

#ifndef DJINN_CLANG_PATH
#define DJINN_CLANG_PATH "clang"
#endif

#define CLANG_ARGS "-O3 -flto -fuse-ld=lld"

const std::string preludes[] = {
    // DO NOT TOUCH IT! ORDERING MATTERS
    "sys/intrinsics.djinn",
    "sys/debug.djinn",
    "sys/console.djinn",
    "types/types.djinn",
    "builtin/coro.djinn",
};

const std::filesystem::path runtimePaths[] = {
    "runtime/djinn_runtime.c",
    "runtime/logger.c",
};


static std::filesystem::path resolve_std_path(const std::filesystem::path& given)
{
    namespace fs = std::filesystem;
    if (fs::exists(given)) return given;

    const fs::path thisFile(__FILE__);
    const auto projectRoot = thisFile.parent_path();
    const auto candidate = projectRoot / "std";
    if (fs::exists(candidate)) return candidate;

    return given;
}


static size_t find_matching_paren(const std::string& s, size_t openPos)
{
    int depth = 1;
    auto i = openPos;
    while (i < s.size() && depth > 0)
    {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        i++;
    }
    return i;
}

static void collect_expansion_steps(std::string& body,
                                    const std::vector<Parser::MacroExpansionRecord>& expansions,
                                    size_t startIdx,
                                    std::vector<std::string>& steps)
{
    for (size_t j = startIdx; j < expansions.size(); j++)
    {
        const auto& inner = expansions[j];
        std::string pattern = inner.macroName + "(";
        auto pos = body.find(pattern);
        if (pos == std::string::npos) continue;

        auto end = find_matching_paren(body, pos + pattern.size());
        body.replace(pos, end - pos, inner.expandedText);

        collect_expansion_steps(body, expansions, j + 1, steps);

        std::ostringstream step;
        step << inner.macroName << "(" << inner.argsText << ") => " << inner.expandedText;
        steps.push_back(step.str());
    }
}

static std::string apply_macro_expansions(const std::string& source,
                                          const std::vector<Parser::MacroExpansionRecord>& expansions)
{
    auto result = source;
    for (size_t i = 0; i < expansions.size(); i++)
    {
        const auto& expansion = expansions[i];
        std::string callPattern = expansion.macroName + "(";
        size_t searchFrom = 0;
        while (true)
        {
            auto pos = result.find(callPattern, searchFrom);
            if (pos == std::string::npos) break;

            auto commentCheck = result.rfind("/*", pos);
            auto commentEnd = result.rfind("*/", pos);
            if (commentCheck != std::string::npos &&
                (commentEnd == std::string::npos || commentEnd < commentCheck))
            {
                searchFrom = pos + 1;
                continue;
            }

            auto end = find_matching_paren(result, pos + callPattern.size());

            auto body = expansion.expandedText;
            std::vector<std::string> steps;
            collect_expansion_steps(body, expansions, i + 1, steps);

            std::ostringstream oss;
            oss << "/*\n";
            for (const auto& step : steps)
                oss << "    " << step << "\n";
            oss << "    " << expansion.macroName << "(" << expansion.argsText << ") => " << body << "\n";
            oss << "*/ " << body;

            auto replacement = oss.str();
            result.replace(pos, end - pos, replacement);
            searchFrom = pos + replacement.size();
            break;
        }
    }
    return result;
}

static void merge_program_into(Program& target, Program& source)
{
    for (auto& i : source.imports) target.imports.push_back(std::move(i));
    for (auto& s : source.structs) target.structs.push_back(std::move(s));
    for (auto& f : source.functions) target.functions.push_back(std::move(f));
    for (auto& e : source.enums) target.enums.push_back(std::move(e));
    for (auto& n : source.namespaces) target.namespaces.push_back(std::move(n));
    for (auto& ef : source.externFunctions) target.externFunctions.push_back(std::move(ef));
    for (auto& iface : source.interfaces) target.interfaces.push_back(std::move(iface));
    for (auto& impl : source.impls) target.impls.push_back(std::move(impl));
    for (auto& ce : source.constExprs) target.constExprs.push_back(std::move(ce));
    for (auto& m : source.macros) target.macros.push_back(std::move(m));
    for (auto& ct : source.compileTimeBlocks) target.compileTimeBlocks.push_back(std::move(ct));
}

static void resolve_compile_time_blocks(Program& program, ConstEvaluator& evaluator)
{
    std::vector<std::unique_ptr<CompileTimeBlock>> blocks;
    blocks.swap(program.compileTimeBlocks);

    for (auto& block : blocks)
    {
        ConstValue val = evaluator.evaluate(*block->condition);
        LOG_DEBUG("[comptime] top-level compile-time block: condition=%s",
                  val.toBool() ? "true" : "false");
        if (val.toBool())
        {
            if (block->thenProgram)
            {
                resolve_compile_time_blocks(*block->thenProgram, evaluator);
                merge_program_into(program, *block->thenProgram);
            }
        }
        else
        {
            if (block->elseProgram)
            {
                resolve_compile_time_blocks(*block->elseProgram, evaluator);
                merge_program_into(program, *block->elseProgram);
            }
        }
    }
}

static void register_intrinsic_values(ConstEvaluator& eval)
{
    auto def = [&eval](const std::string& structName, const std::string& field, ConstValue val)
    {
        eval.defineConstant("std::sys::" + structName + "." + field, val);
        eval.defineConstant(structName + "." + field, val);
    };
    auto defBool = [&def](const std::string& s, const std::string& f, bool val)
    {
        def(s, f, ConstValue::makeInt(val ? 1 : 0, 1, false));
    };
    auto defInt = [&def](const std::string& s, const std::string& f, int64_t val)
    {
        def(s, f, ConstValue::makeInt(val));
    };

#ifdef _WIN32
    defBool("Platform", "Windows", true);
    defBool("Platform", "Linux", false);
    defBool("Platform", "MacOs", false);
#elif __linux__
    defBool("Platform", "Windows", false); defBool("Platform", "Linux", true); defBool("Platform", "MacOs", false);
#elif __APPLE__
    defBool("Platform", "Windows", false); defBool("Platform", "Linux", false); defBool("Platform", "MacOs", true);
#else
    defBool("Platform", "Windows", false); defBool("Platform", "Linux", false); defBool("Platform", "MacOs", false);
#endif

#if defined(__x86_64__) || defined(_M_X64)
    defBool("Arch", "X64", true);
    defBool("Arch", "X86", false);
    defBool("Arch", "Arm64", false);
#elif defined(__i386__) || defined(_M_IX86)
    defBool("Arch", "X64", false); defBool("Arch", "X86", true); defBool("Arch", "Arm64", false);
#elif defined(__aarch64__) || defined(_M_ARM64)
    defBool("Arch", "X64", false); defBool("Arch", "X86", false); defBool("Arch", "Arm64", true);
#else
    defBool("Arch", "X64", false); defBool("Arch", "X86", false); defBool("Arch", "Arm64", false);
#endif

#ifdef NDEBUG
    defBool("Build", "Debug", false); defBool("Build", "Release", true);
#else
    defBool("Build", "Debug", true);
    defBool("Build", "Release", false);
#endif

    defInt("Runtime", "PointerSize", sizeof(void*));
}

static ConstEvaluator create_comptime_evaluator(
    const std::vector<std::shared_ptr<Program>>& programs)
{
    ConstEvaluator eval;
    register_intrinsic_values(eval);

    for (const auto& prog : programs)
    {
        for (const auto& ce : prog->constExprs)
        {
            if (ce->isIntrinsic || !ce->value) continue;
            ConstValue val = eval.evaluate(*ce->value);
            if (!val.isError())
            {
                eval.defineConstant(ce->name.token_name, val);
                const std::string prefix = prog->getNamespacePrefix();
                if (!prefix.empty())
                    eval.defineConstant(prefix + ce->name.token_name, val);
            }
        }
    }

    return eval;
}

CompilerResult DjinnCompiler::compileFromDirectory(const std::filesystem::path& path, const CompilerOptions& options)
{
    auto global_watch = INIT_STOPWATCH_WITH_LEVEL("build time", logger::Level::INFO);
    namespace fs = std::filesystem;
    assert(!options.outputDirectory.empty() && "You need give output directory!");

    fs::create_directories(options.outputDirectory);

    {
        std::ofstream props(options.outputDirectory + "/runtime.properties");
        options.runtimeProperties.serialize(props);
    }

    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program>> programs;

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
        parser.printMacroExpansion = options.print_macro_expansion;

        if (registerPrelude)
        {
            for (const auto& name : preludeTypeNames)
                parser.registerKnownType(name);
        }
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

        if (options.dump_macro_expansion && isUserCode && !parser.macroExpansions.empty())
        {
            auto expandedSource = apply_macro_expansions(source, parser.macroExpansions);
            auto expandedPath = fs::path(options.outputDirectory) / fs::path(file_name).filename().replace_extension(
                ".djinn.expanded");
            if (std::ofstream expandedFile(expandedPath); expandedFile.is_open())
            {
                expandedFile << expandedSource;
                LOG_INFO("[macro-dump] Expanded source written to: %s", expandedPath.string().c_str());
            }
        }

        programs.emplace_back(std::move(program));
    };

    const auto stdLibPath = resolve_std_path(options.stdLibPath);
    const auto stdCanonical = fs::exists(stdLibPath) ? fs::canonical(stdLibPath) : fs::path{};

    std::set<fs::path> parsedFiles;

    try
    {
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

        for (const auto& prog : programs)
        {
            for (const auto& s : prog->structs) stdTypeNames.push_back(s->name.token_name);
            for (const auto& e : prog->enums) stdTypeNames.push_back(e->name.token_name);
        }

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

        auto comptimeEval = create_comptime_evaluator(programs);
        for (auto& prog : programs)
            resolve_compile_time_blocks(*prog, comptimeEval);

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

        {
            auto pass_stop_watch = INIT_STOPWATCH("run passes");
            generator.run_passes(options.optimize);
        }

        std::string generatedIr = generator.print();
        std::ofstream outFile(llPath);
        outFile << generatedIr;
        outFile.close();

        auto exePath = out_file_path +
#ifdef _WIN32
            ".exe";
#else
        "";
#endif
        if (options.generateBinary)
        {
            std::string runtimeArg;
            for (auto& runtime_path : runtimePaths)
            {
                runtimeArg += " " + runtime_path.string();
            }

            auto cmdString = "\"" DJINN_CLANG_PATH "\" " CLANG_ARGS " " + llPath + runtimeArg + " -o " + exePath;
            LOG_DEBUG("Executing compilation command: %s", cmdString.c_str());
            const auto compile_result = system(cmdString.c_str());
            LOG_DEBUG("Compile return: %d", compile_result);
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

        if (diagnostics.warningCount() > 0)
        {
            std::cerr << diagnostics.render();
        }

        LOG_DEBUG("exit code %d", programReturnCode);
        return {.returnCode = programReturnCode, .diagnostics = diagnostics.get_diagnostics()};
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
    auto global_watch = INIT_STOPWATCH_WITH_LEVEL("build time", logger::Level::INFO);
    namespace fs = std::filesystem;

    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program>> programs;
    std::shared_ptr<Program> userProgram;
    std::string generatedIr;
    std::string expandedSourceResult;

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
            .diagnostics = diagnostics.get_diagnostics(),
            .expandedSource = expandedSourceResult
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

        diagnostics.registerSource("main.djinn", source);

        Lexer lexer(source, "main");
        const auto tokens = lexer.tokenize();

        Parser parser(tokens, diagnostics);
        parser.printMacroExpansion = options.print_macro_expansion;

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

        if (options.dump_macro_expansion && !parser.macroExpansions.empty())
        {
            auto expandedSource = apply_macro_expansions(source, parser.macroExpansions);
            expandedSourceResult = expandedSource;

            auto expandedPath = fs::path(options.outputDirectory) / "main.djinn.expanded";
            if (std::ofstream expandedFile(expandedPath); expandedFile.is_open())
            {
                expandedFile << expandedSource;
                LOG_INFO("[macro-dump] Expanded source written to: %s", expandedPath.string().c_str());
            }
        }

        userProgram = std::shared_ptr<Program>(std::move(program));
        programs.emplace_back(userProgram);

        if (diagnostics.hasErrors())
        {
            return makeResult(1, std::stacktrace::current());
        }

        // Resolve top-level compile-time blocks before binding
        auto comptimeEval = create_comptime_evaluator(programs);
        for (auto& prog : programs)
            resolve_compile_time_blocks(*prog, comptimeEval);

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
        const std::string exePath = outputDir + "\\" + outputFileName +
#ifdef _WIN32
            ".exe";
#else
        "";
#endif

        {
            auto pass_stop_watch = INIT_STOPWATCH("run passes");
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

        std::string runtimeArg;
        for (const auto& runtime_path : runtimePaths)
        {
            runtimeArg += " " + runtime_path.string();
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

        if (diagnostics.warningCount() > 0)
        {
            std::cerr << diagnostics.render();
        }

        LOG_DEBUG("exit code %d", programReturnCode);
        return makeResult(programReturnCode, std::stacktrace::current());
    }
    catch (const CompileError& e)
    {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        return makeResult(1, std::stacktrace::current());
    }
}
