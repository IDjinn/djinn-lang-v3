#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

#include "DjinnCompiler.h"

void printUsage(const char *programName) {
    std::cout << "Usage: " << programName << " [options] <file.djinn|directory> [more files/dirs...]\n"
            << "\nOptions:\n"
            << "  -o <name>     Output file name (without extension)\n"
            << "  -O            Enable optimizations (default)\n"
            << "  -O0           Disable optimizations\n"
            << "  -c            Compile only, do not execute\n"
            << "  -r            Recursively search directories for .djinn files\n"
            << "  -h, --help    Show this help message\n"
            << "\nExamples:\n"
            << "  " << programName << " main.djinn\n"
            << "  " << programName << " -o myprogram main.djinn lib.djinn\n"
            << "  " << programName << " -c -O0 test.djinn\n"
            << "  " << programName << " -r src/\n";
}

void collectDjinnFiles(const std::filesystem::path &path, std::vector<std::filesystem::path> &files, bool recursive) {
    namespace fs = std::filesystem;

    if (fs::is_regular_file(path)) {
        if (path.extension() == ".djinn") {
            files.push_back(path);
        }
    } else if (fs::is_directory(path)) {
        if (recursive) {
            for (const auto &entry: fs::recursive_directory_iterator(path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".djinn") {
                    files.push_back(entry.path());
                }
            }
        } else {
            for (const auto &entry: fs::directory_iterator(path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".djinn") {
                    files.push_back(entry.path());
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    namespace fs = std::filesystem;

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    CompilerOptions options;
    std::vector<fs::path> inputPaths;
    bool recursive = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-o" && i + 1 < argc) {
            options.outputFileName = argv[++i];
        } else if (arg == "-O") {
            options.optimize = true;
        } else if (arg == "-O0") {
            options.optimize = false;
        } else if (arg == "-c") {
            options.executeAfterCompile = false;
        } else if (arg == "-r") {
            recursive = true;
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        } else {
            inputPaths.emplace_back(arg);
        }
    }

    if (inputPaths.empty()) {
        std::cerr << "Error: no input files specified\n";
        printUsage(argv[0]);
        return 1;
    }

    std::vector<fs::path> inputFiles;
    for (const auto &path: inputPaths) {
        if (!fs::exists(path)) {
            std::cerr << "Error: path does not exist: " << path << "\n";
            return 1;
        }
        collectDjinnFiles(path, inputFiles, recursive);
    }

    if (inputFiles.empty()) {
        std::cerr << "Error: no .djinn files found\n";
        return 1;
    }

    std::ranges::sort(inputFiles);

    CompilerResult result;
    if (inputFiles.size() == 1) {
        result = DjinnCompiler::runFromFile(inputFiles[0], options);
    } else {
        result = DjinnCompiler::runFromFiles(inputFiles, options);
    }

    if (!result.ir.empty() && !options.executeAfterCompile) {
        std::cout << result.ir;
    }

    return result.returnCode;
}