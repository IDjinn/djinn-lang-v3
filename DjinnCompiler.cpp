//
// Created by Luke on 29/12/2025.
//

#include "DjinnCompiler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "binder/Binder.h"
#include "generator/Generator.h"
#include "lexer/Lexer.h"
#include "parser/parser.h"

CompilerResult DjinnCompiler::run(const std::string &source, CompilerOptions options) {
    namespace fs = std::filesystem;

    // Ensure output directory exists
    if (!options.outputDirectory.empty()) {
        fs::create_directories(options.outputDirectory);
    }

    if (options.outputFileName.empty()) {
        const auto temp_file = fs::temp_directory_path() / std::to_string(rand());
        options.outputFileName = temp_file.string();
    } else if (!options.outputDirectory.empty()) {
        options.outputFileName = (fs::path(options.outputDirectory) / options.outputFileName).string();
    }

    DiagnosticEngine diagnostics(source);

    try {
        Lexer lexer(source);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();

        Binder binder(diagnostics);
        if (const auto bindResult = binder.bind(*program); !bindResult.success) {
            diagnostics.printToStderr();
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        Generator generator;
        generator.generate(*program);

        // Save unoptimized IR
        const auto unoptimizedResult = generator.print();
        std::ofstream unoptOutput(options.outputFileName + ".ll");
        unoptOutput << unoptimizedResult;
        unoptOutput.close();

        std::string finalIrFile = options.outputFileName + ".ll";
        std::string result = unoptimizedResult;

        if (options.optimize) {
            generator.optimize();
            result = generator.print();

            // Save optimized IR with .opt.ll suffix
            std::ofstream optOutput(options.outputFileName + ".opt.ll");
            optOutput << result;
            optOutput.close();

            finalIrFile = options.outputFileName + ".opt.ll";
        }

        int returnCode = 0;
        if (options.executeAfterCompile) {
            system(("clang " + finalIrFile + " -o " + options.outputFileName + ".exe").c_str());
            returnCode = system((options.outputFileName + ".exe").c_str());
        }

        if (result.starts_with("Erro:")) {
            std::cerr << "LLVM Compilation error: \n" << result << std::endl;
        }

        std::printf("Done. Return code %d", returnCode);
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

CompilerResult DjinnCompiler::runFromFile(const std::filesystem::path &filePath, CompilerOptions options) {
    namespace fs = std::filesystem;

    if (!fs::exists(filePath)) {
        std::cerr << "Error: file not found: " << filePath << std::endl;
        return {.returnCode = 1};
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Error: could not open file: " << filePath << std::endl;
        return {.returnCode = 1};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    if (options.outputFileName.empty()) {
        options.outputFileName = filePath.stem().string();
    }

    std::printf("processing path: %s", options.outputFileName.c_str());
    return run(buffer.str(), options);
}

CompilerResult DjinnCompiler::runFromFiles(const std::vector<std::filesystem::path> &filePaths,
                                           CompilerOptions options) {
    namespace fs = std::filesystem;

    std::stringstream combinedSource;

    for (const auto &filePath: filePaths) {
        if (!fs::exists(filePath)) {
            std::cerr << "Error: file not found: " << filePath << std::endl;
            return {.returnCode = 1};
        }

        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Error: could not open file: " << filePath << std::endl;
            return {.returnCode = 1};
        }

        combinedSource << file.rdbuf();
        combinedSource << "\n";
        file.close();
    }

    if (options.outputFileName.empty() && !filePaths.empty()) {
        options.outputFileName = filePaths[0].stem().string();
    }

    std::printf("processing path: %s", options.outputFileName.c_str());
    return run(combinedSource.str(), options);
}