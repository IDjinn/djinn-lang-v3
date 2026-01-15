//
// Created by Luke on 29/12/2025.
//

#include "DjinnCompiler.h"

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

std::vector<std::filesystem::path> DjinnCompiler::collectStdFiles(const std::filesystem::path &stdLibPath) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;

    if (!fs::exists(stdLibPath)) {
        return files;
    }

    for (const auto &entry: fs::recursive_directory_iterator(stdLibPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".djinn") {
            files.push_back(entry.path());
        }
    }

    return files;
}

std::unique_ptr<Program> DjinnCompiler::loadStdLibrary(const std::filesystem::path &stdLibPath) {
    namespace fs = std::filesystem;

    auto stdProgram = std::make_unique<Program>();
    const auto stdFiles = collectStdFiles(stdLibPath);

    for (const auto &filePath: stdFiles) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            continue;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        const std::string source = buffer.str();
        const std::string fileId = filePath.filename().string();

        Lexer lexer(source, fileId);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();

        mergePrograms(*stdProgram, std::move(program));
    }

    return stdProgram;
}

CompilerResult DjinnCompiler::run(const std::string &source, CompilerOptions options) {
    namespace fs = std::filesystem;

    // Ensure output directory exists
    if (!options.outputDirectory.empty()) {
        fs::create_directories(options.outputDirectory);
    }

    if (options.outputFileName.empty()) {
        // Use outputDirectory if set, otherwise temp directory
        if (!options.outputDirectory.empty()) {
            options.outputFileName = (fs::path(options.outputDirectory) / std::to_string(rand())).string();
        } else {
            options.outputFileName = (fs::temp_directory_path() / std::to_string(rand())).string();
        }
    } else if (!options.outputDirectory.empty()) {
        options.outputFileName = (fs::path(options.outputDirectory) / options.outputFileName).string();
    }

    DiagnosticEngine diagnostics(source);

    try {
        Lexer lexer(source);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();

        // Include standard library if enabled
        if (options.includeStd) {
            auto stdProgram = loadStdLibrary(options.stdLibPath);
            if (stdProgram) {
                // Merge std into user program (std comes first)
                mergePrograms(*stdProgram, std::move(program));
                program = std::move(stdProgram);
            }
        }
        if (options.print_ast) {
            std::cout << "=====AST=====\n";
            program->print(std::cout);
            std::cout << "=====" << std::endl;
        }

        Binder binder(diagnostics);
        if (const auto bindResult = binder.bind(*program); !bindResult.success) {
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        auto generator = Generator(options.outputFileName);
        generator.generate(*program, options.libraryMode, options.stdDeclOnly);

        // Link external .ll modules
        if (!options.linkLibraries.empty()) {
            if (!generator.linkModules(options.linkLibraries)) {
                return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
            }
        }

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
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        if (!options.silentMode) {
            const auto stack = std::stacktrace::current();
            diagnostics.printToStderr(stack);
        }
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

    // Start with std library if enabled
    std::unique_ptr<Program> combinedProgram;
    if (options.includeStd) {
        combinedProgram = loadStdLibrary(options.stdLibPath);
        if (!combinedProgram) {
            combinedProgram = std::make_unique<Program>();
        }
    } else {
        combinedProgram = std::make_unique<Program>();
    }

    std::unordered_map<std::string, std::string> sources;

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
        const std::string fileId = filePath.filename().string();

        sources[fileId] = source;

        Lexer lexer(source, fileId);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens);
        auto program = parser.parse();

        mergePrograms(*combinedProgram, std::move(program));
    }

    if (options.outputFileName.empty()) {
        options.outputFileName = filePaths[0].stem().string();
    }

    // Disable includeStd for runFromProgram since we already included it
    options.includeStd = false;

    std::printf("processing %zu files, output: %s\n", filePaths.size(), options.outputFileName.c_str());
    return runFromProgram(std::move(combinedProgram), options, std::move(sources));
}

CompilerResult DjinnCompiler::runPerNamespace(const std::vector<std::filesystem::path> &filePaths,
                                              CompilerOptions options) {
    // For now, per-namespace compilation has the same behavior as bundled
    // because deep copying AST nodes with unique_ptr is complex
    // The user can use --bundle explicitly if needed
    options.libraryMode = true;
    return runFromFiles(filePaths, options);
}

// Helper to check if struct exists in list
static bool hasStruct(const std::vector<std::unique_ptr<StructDeclaration> > &list, const std::string &name) {
    for (const auto &s: list) {
        if (s->name.token_name == name) return true;
    }
    return false;
}

// Helper to check if function exists in list
static bool hasFunction(const std::vector<std::unique_ptr<FunctionDeclaration> > &list, const std::string &name) {
    for (const auto &f: list) {
        if (f->name.token_name == name) return true;
    }
    return false;
}

void DjinnCompiler::mergePrograms(Program &target, std::unique_ptr<Program> source) {
    if (source->hasFileNamespace()) {
        NamespaceDeclaration *targetNs = nullptr;

        for (auto &ns: target.namespaces) {
            if (ns->name.token_name == source->fileNamespace) {
                targetNs = ns.get();
                break;
            }
        }

        if (!targetNs) {
            auto newNs = std::make_unique<NamespaceDeclaration>(SourceIdentifier(source->fileNamespace));
            targetNs = newNs.get();
            target.namespaces.push_back(std::move(newNs));
        }

        for (auto &s: source->structs) {
            if (!hasStruct(targetNs->structs, s->name.token_name)) {
                targetNs->structs.push_back(std::move(s));
            }
        }

        for (auto &f: source->functions) {
            if (!hasFunction(targetNs->functions, f->name.token_name)) {
                targetNs->functions.push_back(std::move(f));
            }
        }

        for (auto &ns: source->namespaces) {
            targetNs->namespaces.push_back(std::move(ns));
        }
    } else {
        for (auto &s: source->structs) {
            if (!hasStruct(target.structs, s->name.token_name)) {
                target.structs.push_back(std::move(s));
            }
        }

        for (auto &f: source->functions) {
            if (!hasFunction(target.functions, f->name.token_name)) {
                target.functions.push_back(std::move(f));
            }
        }

        for (auto &ns: source->namespaces) {
            bool found = false;
            for (auto &existingNs: target.namespaces) {
                if (existingNs->name.token_name == ns->name.token_name) {
                    for (auto &s: ns->structs) {
                        if (!hasStruct(existingNs->structs, s->name.token_name)) {
                            existingNs->structs.push_back(std::move(s));
                        }
                    }
                    for (auto &f: ns->functions) {
                        if (!hasFunction(existingNs->functions, f->name.token_name)) {
                            existingNs->functions.push_back(std::move(f));
                        }
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
        bool isDuplicate = false;
        for (const auto &existing: target.externFunctions) {
            if (existing->name.token_name == ext->name.token_name) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            target.externFunctions.push_back(std::move(ext));
        }
    }

    for (auto &iface: source->interfaces) {
        bool isDuplicate = false;
        for (const auto &existing: target.interfaces) {
            if (existing->name.token_name == iface->name.token_name) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            target.interfaces.push_back(std::move(iface));
        }
    }

    for (auto &enumDecl: source->enums) {
        bool isDuplicate = false;
        for (const auto &existing: target.enums) {
            if (existing->name.token_name == enumDecl->name.token_name) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            target.enums.push_back(std::move(enumDecl));
        }
    }
}

CompilerResult DjinnCompiler::runFromProgram(std::unique_ptr<Program> program, CompilerOptions options,
                                             std::unordered_map<std::string, std::string> sources) {
    namespace fs = std::filesystem;

    // Include standard library if enabled
    if (options.includeStd) {
        auto stdProgram = loadStdLibrary(options.stdLibPath);
        if (stdProgram) {
            mergePrograms(*stdProgram, std::move(program));
            program = std::move(stdProgram);
        }
    }

    if (options.print_ast) {
        std::cout << "=====AST=====\n";
        program->print(std::cout);
        std::cout << "=====" << std::endl;
    }

    // Ensure output directory exists
    if (!options.outputDirectory.empty()) {
        fs::create_directories(options.outputDirectory);
    }

    if (options.outputFileName.empty()) {
        // Use outputDirectory if set, otherwise temp directory
        if (!options.outputDirectory.empty()) {
            options.outputFileName = (fs::path(options.outputDirectory) / std::to_string(rand())).string();
        } else {
            options.outputFileName = (fs::temp_directory_path() / std::to_string(rand())).string();
        }
    } else if (!options.outputDirectory.empty()) {
        options.outputFileName = (fs::path(options.outputDirectory) / options.outputFileName).string();
    }

    DiagnosticEngine diagnostics;
    for (const auto &[fileId, source]: sources) {
        diagnostics.registerSource(fileId, source);
    }

    try {
        Binder binder(diagnostics);
        if (const auto bindResult = binder.bind(*program); !bindResult.success) {
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        auto generator = Generator(options.outputFileName);
        generator.generate(*program, options.libraryMode, options.stdDeclOnly);

        // Link external .ll modules
        if (!options.linkLibraries.empty()) {
            if (!generator.linkModules(options.linkLibraries)) {
                return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
            }
        }

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
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        if (!options.silentMode) {
            const auto stack = std::stacktrace::current();
            diagnostics.printToStderr(stack);
        }
        return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
    }
}