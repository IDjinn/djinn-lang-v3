//
// Created by Luke on 29/12/2025.
//

#include "DjinnCompiler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stacktrace>

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
        if (options.print_ast) {
            std::cout << "=====AST=====\n";
            program->print(std::cout);
            std::cout << "=====" << std::endl;
        }

        Binder binder(diagnostics);
        if (const auto bindResult = binder.bind(*program); !bindResult.success) {
            diagnostics.printToStderr({});
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        // Print warnings even if compilation succeeds
        if (diagnostics.warningCount() > 0) {
            std::cerr << diagnostics.render();
        }

        auto generator = Generator(options.outputFileName);
        generator.generate(*program);
        if (options.print_ir) std::cout << "=====IR=====\n" << generator.print() << "=====";

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
        const auto stack = std::stacktrace::current();
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        diagnostics.printToStderr(stack);
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

    if (filePaths.empty()) {
        std::cerr << "Error: no files provided" << std::endl;
        return {.returnCode = 1};
    }

    auto combinedProgram = std::make_unique<Program>();

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

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        const std::string source = buffer.str();

        Lexer lexer(source);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();

        mergePrograms(*combinedProgram, std::move(program));
    }

    if (options.outputFileName.empty()) {
        options.outputFileName = filePaths[0].stem().string();
    }

    std::printf("processing %zu files, output: %s\n", filePaths.size(), options.outputFileName.c_str());
    return runFromProgram(std::move(combinedProgram), options);
}

void DjinnCompiler::mergePrograms(Program &target, std::unique_ptr<Program> source) {
    if (source->hasFileNamespace()) {
        NamespaceDeclaration *targetNs = nullptr;

        for (auto &ns: target.namespaces) {
            if (ns->name == source->fileNamespace) {
                targetNs = ns.get();
                break;
            }
        }

        if (!targetNs) {
            auto newNs = std::make_unique<NamespaceDeclaration>(source->fileNamespace);
            targetNs = newNs.get();
            target.namespaces.push_back(std::move(newNs));
        }

        for (auto &s: source->structs) {
            targetNs->structs.push_back(std::move(s));
        }

        for (auto &f: source->functions) {
            targetNs->functions.push_back(std::move(f));
        }

        for (auto &ns: source->namespaces) {
            targetNs->namespaces.push_back(std::move(ns));
        }
    } else {
        for (auto &s: source->structs) {
            target.structs.push_back(std::move(s));
        }

        for (auto &f: source->functions) {
            target.functions.push_back(std::move(f));
        }

        for (auto &ns: source->namespaces) {
            bool found = false;
            for (auto &existingNs: target.namespaces) {
                if (existingNs->name == ns->name) {
                    for (auto &s: ns->structs) {
                        existingNs->structs.push_back(std::move(s));
                    }
                    for (auto &f: ns->functions) {
                        existingNs->functions.push_back(std::move(f));
                    }
                    for (auto &nestedNs: ns->namespaces) {
                        existingNs->namespaces.push_back(std::move(nestedNs));
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                target.namespaces.push_back(std::move(ns));
            }
        }
    }

    for (auto &imp: source->imports) {
        target.imports.push_back(std::move(imp));
    }

    for (auto &ext: source->externFunctions) {
        target.externFunctions.push_back(std::move(ext));
    }

    for (auto &iface: source->interfaces) {
        target.interfaces.push_back(std::move(iface));
    }
}

CompilerResult DjinnCompiler::runFromProgram(std::unique_ptr<Program> program, CompilerOptions options) {
    if (options.print_ast) {
        std::cout << "=====AST=====\n";
        program->print(std::cout);
        std::cout << "=====" << std::endl;
    }
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

    DiagnosticEngine diagnostics("");

    try {
        Binder binder(diagnostics);
        if (const auto bindResult = binder.bind(*program); !bindResult.success) {
            diagnostics.printToStderr({});
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        // Print warnings even if compilation succeeds
        if (diagnostics.warningCount() > 0) {
            std::cerr << diagnostics.render();
        }

        auto generator = Generator(options.outputFileName);
        generator.generate(*program);
        if (options.print_ir) std::cout << "=====IR=====\n" << generator.print() << "=====";

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
            .program = std::move(program),
            .ir = result,
        };
    } catch (const CompileError &e) {
        const auto stack = std::stacktrace::current();
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        diagnostics.printToStderr(stack);
        return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
    }
}
