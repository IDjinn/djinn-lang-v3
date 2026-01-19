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
#include <stacktrace>

#include "binder/Binder.h"
#include "generator/Generator.h"
#include "lexer/Lexer.h"
#include "parser/parser.h"

CompilerResult DjinnCompiler::compileFromDirectory(const std::filesystem::path &path, const CompilerOptions &options) {
    namespace fs = std::filesystem;
    assert(!options.outputDirectory.empty() && "You need give output directory!");

    fs::create_directories(options.outputDirectory);
    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program> > programs;
    try {
        for (const auto &entry: fs::recursive_directory_iterator(path)) {
            try {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".djinn") continue;

                std::ifstream file(entry.path());
                if (!file) continue;

                const auto source = std::string(
                    std::istreambuf_iterator(file),
                    std::istreambuf_iterator<char>()
                );

                diagnostics.registerSource(entry.path().string(), source);

                Lexer lexer(source);
                const auto tokens = lexer.tokenize();

                Parser parser(tokens);
                auto program = parser.parse();

                if (options.print_ast) {
                    std::cout << "=====AST [" << entry.path().string() << "]=====\n";
                    program->print(std::cout);
                    std::cout << "=====" << std::endl;
                }

                programs.emplace_back(std::move(program));
            } catch (const CompileError &compile_error) {
                std::cerr << "Error parsing " << entry.path().string() << ": "
                        << compile_error.message() << std::endl;
            }
        }

        if (programs.empty()) {
            std::cerr << "No .djinn files found in " << path << std::endl;
            return {.returnCode = 1};
        }

        Binder binder(diagnostics);
        const auto bindResult = binder.bindAll(programs);
        if (!bindResult.success) {
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        auto generator = Generator(diagnostics, bindResult.globalScope);
        generator.generate();
        std::cout << "RESULT\n\n" << generator.print() << std::endl;

        std::ofstream outFile("./build/output.ll");
        outFile << generator.print();
        outFile.close();

        std::ofstream outFileOptimized("./build/output.opt.ll");
        generator.optimize();
        outFileOptimized << generator.print();
        outFileOptimized.close();

        return {.returnCode = 0, .diagnostics = diagnostics.get_diagnostics()};
    } catch (const CompileError &e) {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        diagnostics.printToStderr(std::stacktrace::current());
        return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
    }
}