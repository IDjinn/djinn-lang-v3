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

CompilerResult DjinnCompiler::compileFromDirectory(const std::filesystem::path& path, const CompilerOptions& options)
{
    utils::StopWatch global_watch("build time");
    namespace fs = std::filesystem;
    assert(!options.outputDirectory.empty() && "You need give output directory!");

    fs::create_directories(options.outputDirectory);
    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program>> programs;

    const auto parseDirectory = [&](const fs::path& dir, const bool isStdLib)
    {
        if (!fs::exists(dir)) return;

        for (const auto& entry : fs::recursive_directory_iterator(dir))
        {
            try
            {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".djinn") continue;

                std::ifstream file(entry.path());
                if (!file) continue;

                const auto source = std::string(
                    std::istreambuf_iterator(file),
                    std::istreambuf_iterator<char>()
                );

                auto file_name = entry.path().string();
                diagnostics.registerSource(file_name, source);

                Lexer lexer(source);
                const auto tokens = lexer.tokenize();

                Parser parser(tokens, diagnostics);
                auto program = parser.parse(file_name);

                if (options.print_ast && !isStdLib)
                {
                    std::ostringstream oss;
                    oss << "=====AST [" << file_name << "]=====\n";
                    program->print(oss);
                    oss << "=====";
                    LOG_DEBUG("%s", oss.str().c_str());
                }

                programs.emplace_back(std::move(program));
            }
            catch (const CompileError& compile_error)
            {
                LOG_ERROR("Error parsing %s: %s", entry.path().string().c_str(), compile_error.message().c_str());
            }
        }
    };

    try
    {
        // Load standard library first if enabled
        if (options.includeStd && fs::exists(options.stdLibPath))
        {
            parseDirectory(options.stdLibPath, true);
        }

        // Load user code
        parseDirectory(path, false);

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
        std::ofstream outFile(out_file_path + ".ll");
        outFile << generator.print();
        outFile.close();

        if (options.optimize)
        {
            utils::StopWatch opt_stop_watch("optimize ir");
            generator.optimize();
        }

        std::ofstream outFileOptimized(out_file_path + ".opt.ll");
        outFileOptimized << generator.print();
        outFileOptimized.close();

        if (options.generateBinary)
            system(("clang " + out_file_path + ".ll -o " + out_file_path + ".exe").c_str());


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

    auto makeResult = [&](int returnCode, const std::stacktrace& trace) {
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
        // Load standard library first if enabled
        if (options.includeStd && fs::exists(options.stdLibPath))
        {
            for (const auto& entry : fs::recursive_directory_iterator("../std"))
            {
                try
                {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".djinn") continue;

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
        const std::string llOptPath = outputDir + "\\" + outputFileName + ".opt.ll";
        const std::string exePath = outputDir + "\\" + outputFileName + ".exe";

        std::ofstream optOutput(llPath);
        optOutput << generatedIr;
        optOutput.close();

        if (options.optimize)
        {
            utils::StopWatch opt_stop_watch("optimize ir");
            generator.optimize();

            std::ofstream otimized_output(llOptPath);
            otimized_output << generatedIr;
            otimized_output.close();
        }

        if (!options.generateBinary)
        {
            return makeResult(0, std::stacktrace::current());
        }

        int clangResult = system(("clang " + llPath + " -o " + exePath).c_str());
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
