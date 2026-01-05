//
// Created by Luke on 29/12/2025.
//

#include "DjinnCompiler.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "binder/Binder.h"
#include "generator/Generator.h"
#include "lexer/Lexer.h"
#include "parser/parser.h"

CompilerResult DjinnCompiler::run(const std::string &source, CompilerOptions options) {
    namespace fs = std::filesystem;
    if (options.outputFileName == "") {
        const auto temp_file = fs::temp_directory_path() / std::to_string(rand());
        options.outputFileName = temp_file.string();
    }

    DiagnosticEngine diagnostics(source);

    try {
        Lexer lexer(source);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();
        // program->print(std::cout, 2);
        // std::cout << "\n\n";

        Binder binder(diagnostics);
        if (const auto bindResult = binder.bind(*program); !bindResult.success) {
            diagnostics.printToStderr();
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        Generator generator;
        generator.generate(*program);
        if (options.optimize) generator.optimize();
        const auto result = generator.print();

        std::ofstream output;
        output.open(options.outputFileName + ".ll");
        output << result;
        output.close();

        system(("clang " + options.outputFileName + ".ll -o " + options.outputFileName + ".exe").c_str());
        const auto returnCode = system((options.outputFileName + ".exe").c_str());
        return {
            .returnCode = returnCode,
            .tokens = tokens,
            .program = std::move(program),
            .ir = result,
        };
    } catch (const CompileError &e) {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        diagnostics.printToStderr();
        return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
    }
}
