//
// Created by Luke on 29/12/2025.
//

#include "DjinnCompiler.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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

CompilerResult DjinnCompiler::compileFromDirectory(const std::filesystem::path &path, const CompilerOptions &options) {
    utils::StopWatch global_watch("build time");
    namespace fs = std::filesystem;
    assert(!options.outputDirectory.empty() && "You need give output directory!");

    fs::create_directories(options.outputDirectory);
    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program> > programs;

    const auto parseDirectory = [&](const fs::path &dir, const bool isStdLib) {
        if (!fs::exists(dir)) return;

        for (const auto &entry: fs::recursive_directory_iterator(dir)) {
            try {
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

                if (options.print_ast && !isStdLib) {
                    std::ostringstream oss;
                    oss << "=====AST [" << file_name << "]=====\n";
                    program->print(oss);
                    oss << "=====";
                    LOG_DEBUG("%s", oss.str().c_str());
                }

                programs.emplace_back(std::move(program));
            } catch (const CompileError &compile_error) {
                LOG_ERROR("Error parsing %s: %s", entry.path().string().c_str(), compile_error.message().c_str());
            }
        }
    };

    try {
        // Load standard library first if enabled
        if (options.includeStd && fs::exists(options.stdLibPath)) {
            parseDirectory(options.stdLibPath, true);
        }

        // Load user code
        parseDirectory(path, false);

        if (programs.empty()) {
            LOG_ERROR("No .djinn files found in %s", path.string().c_str());
            return {.returnCode = 1};
        }

        Binder binder(diagnostics);
        const auto bindResult = binder.bindAll(programs);
        if (!bindResult.success) {
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        auto generator = Generator(diagnostics, bindResult.globalScope);
        generator.generate();
        if (options.print_ir) {
            LOG_INFO("RESULT\n\n%s", generator.print().c_str());
        }

        if (options.optimize) {
            utils::StopWatch opt_stop_watch("optimize ir");
            generator.optimize();
        }

        std::ofstream outFile("./build/output.ll");
        outFile << generator.print();
        outFile.close();

        std::ofstream outFileOptimized("./build/output.opt.ll");
        generator.optimize();
        outFileOptimized << generator.print();
        outFileOptimized.close();

        if (options.generateBinary) {
            system(
                ("clang " + options.outputDirectory + "\\" + options.outputFileName + ".ll -o " + options.
                 outputDirectory + "\\" + options.outputFileName + ".exe").c_str());
        }


        return {.returnCode = 0, .diagnostics = diagnostics.get_diagnostics()};
    } catch (const CompileError &e) {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        diagnostics.printToStderr(std::stacktrace::current());
        return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
    }
}

CompilerResult DjinnCompiler::run(const std::string &source, const CompilerOptions &options) {
    namespace fs = std::filesystem;

    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program> > programs;

    try {
        // Load standard library first if enabled
        if (options.includeStd && fs::exists(options.stdLibPath)) {
            for (const auto &entry: fs::recursive_directory_iterator("../std")) {
                try {
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
                } catch (const CompileError &compile_error) {
                    if (!options.silentMode) {
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

        if (options.print_ast) {
            std::ostringstream oss;
            oss << "=====AST=====\n";
            program->print(oss);
            oss << "=====";
            LOG_DEBUG("%s", oss.str().c_str());
        }

        programs.emplace_back(std::move(program));

        Binder binder(diagnostics);
        const auto bindResult = binder.bindAll(programs);
        if (!bindResult.success) {
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        auto generator = Generator(diagnostics, bindResult.globalScope);
        generator.generate();

        if (options.print_ir) {
            LOG_INFO("RESULT\n\n%s", generator.print().c_str());
        }

        if (options.optimize) {
            utils::StopWatch opt_stop_watch("optimize ir");
            generator.optimize();
        }

        // Determine output directory and filename
        std::string outputDir = options.outputDirectory;
        std::string outputFileName = options.outputFileName.empty() ? "main" : options.outputFileName;
        if (options.useTempDirectory) {
            outputDir = (fs::temp_directory_path() / "djinn_build").string();
            fs::create_directories(outputDir);
        }

        const std::string llPath = outputDir + "\\" + outputFileName + ".ll";
        const std::string exePath = outputDir + "\\" + outputFileName + ".exe";

        std::ofstream optOutput(llPath);
        optOutput << generator.print();
        optOutput.close();

        int programReturnCode = 0;
        if (options.generateBinary) {
            const int clangResult = system(("clang " + llPath + " -o " + exePath).c_str());
            if (clangResult != 0) {
                return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
            }

            if (options.runAfterCompile) {
                programReturnCode = system(exePath.c_str());
#ifdef _WIN32
                // On Windows, system() returns the exit code directly
#else
                // On Unix, need to extract exit code with WEXITSTATUS
                if (WIFEXITED(programReturnCode)) {
                    programReturnCode = WEXITSTATUS(programReturnCode);
                }
#endif
            }
        }

        return {
            .returnCode = programReturnCode,
            .ir = generator.print(),
            .diagnostics = diagnostics.get_diagnostics()
        };
    } catch (const CompileError &e) {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        if (!options.silentMode) {
            diagnostics.printToStderr(std::stacktrace::current());
        }
        return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
    }
}
