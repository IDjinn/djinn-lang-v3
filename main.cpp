#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

#include "DjinnCompiler.h"
#include "config/ProjectConfig.h"
#include "utils/Logger.h"

#define BINDER_DEBUG 1
#define PARSER_DEBUG 1
#define GENERATOR_DEBUG 1

void printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options] <file.djinn|directory> [more files/dirs...]\n"
        << "\nOptions:\n"
        << "  -o <name>     Output file name (without extension)\n"
        << "  -out <dir>    Output file directory (default=build)\n"
        << "  -ir           print ir (default=false)\n"
        << "  -ast          print ast (default=false)\n"
        << "  -o3            Enable optimizations (default)\n"
        << "  -O0           Disable optimizations\n"
        << "  -c            Compile only, do not execute\n"
        << "  -r            Recursively search directories for .djinn files\n"
        << "  -run          Auto run generated exe file\n"
        << "  --lib         Compile as library (no main required)\n"
        << "  --bundle      Bundle all into single .ll (default: per namespace)\n"
        << "  -l <file.ll>  Link external .ll module (can be used multiple times)\n"
        << "  --no-std      Don't include standard library\n"
        << "  --std-decl    Include std declarations only (use with -l std.ll)\n"
        << "  -h, --help    Show this help message\n"
        << "\nExamples:\n"
        << "  " << programName << " main.djinn\n"
        << "  " << programName << " -o myprogram main.djinn lib.djinn\n"
        << "  " << programName << " -c -O0 test.djinn\n"
        << "  " << programName << " -r src/\n"
        << "\nLibrary compilation:\n"
        << "  " << programName <<
        " --lib --no-std -c -r std/            # Per namespace (std.types.ll, std.sys.ll)\n"
        << "  " << programName << " --lib --no-std -c --bundle -o std -r std/  # Single std.ll\n"
        << "  " << programName << " --std-decl -l build/std.types.ll -l build/std.sys.ll main.djinn\n";
}

void collectDjinnFiles(const std::filesystem::path& path, std::vector<std::filesystem::path>& files, bool recursive)
{
    namespace fs = std::filesystem;

    if (fs::is_regular_file(path))
    {
        if (path.extension() == ".djinn")
        {
            files.push_back(path);
        }
    }
    else if (fs::is_directory(path))
    {
        if (recursive)
        {
            for (const auto& entry : fs::recursive_directory_iterator(path))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".djinn")
                {
                    files.push_back(entry.path());
                }
            }
        }
        else
        {
            for (const auto& entry : fs::directory_iterator(path))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".djinn")
                {
                    files.push_back(entry.path());
                }
            }
        }
    }
}

int main(int argc, char* argv[])
{
    namespace fs = std::filesystem;

    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    CompilerOptions options;
    std::vector<fs::path> inputPaths;
    bool recursive = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-o" && i + 1 < argc)
        {
            options.outputFileName = argv[++i];
        }
        else if (arg == "-out" && i + 1 < argc)
        {
            options.outputDirectory = argv[++i];
        }
        else if (arg == "-run")
        {
            options.runAfterCompile = true;
        }
        else if (arg == "-o3")
        {
            options.optimize = true;
        }
        else if (arg == "-ir")
        {
            options.print_ir = true;
        }
        else if (arg == "-ast")
        {
            options.print_ast = true;
        }
        else if (arg == "-O0")
        {
            options.optimize = false;
        }
        else if (arg == "-c")
        {
            options.generateBinary = false;
        }
        else if (arg == "-r")
        {
            recursive = true;
        }
        else if (arg == "--lib")
        {
            options.libraryMode = true;
        }
        else if (arg == "--bundle")
        {
            options.bundleModules = true;
        }
        else if (arg == "--no-std")
        {
            options.includeStd = false;
        }
        else if (arg == "--std-decl")
        {
            options.stdDeclOnly = true;
        }
        else if ((arg == "-l" || arg == "--link") && i + 1 < argc)
        {
            options.linkLibraries.emplace_back(argv[++i]);
        }
        else if (arg[0] == '-')
        {
            LOG_ERROR("Unknown option: %s", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
        else
        {
            inputPaths.emplace_back(arg);
        }
    }

    if (inputPaths.empty())
    {
        LOG_ERROR("No input files specified");
        printUsage(argv[0]);
        return 1;
    }

    std::vector<fs::path> inputFiles;
    for (const auto& path : inputPaths)
    {
        if (!fs::exists(path))
        {
            LOG_ERROR("Path does not exist: %s", path.string().c_str());
            return 1;
        }
        collectDjinnFiles(path, inputFiles, recursive);
    }

    if (inputFiles.empty())
    {
        LOG_ERROR("No .djinn files found");
        return 1;
    }

    std::ranges::sort(inputFiles);

    CompilerResult result;
    fs::path baseDir = inputPaths[0];
    if (fs::is_regular_file(baseDir))
    {
        baseDir = baseDir.parent_path();
    }

    const auto projFile = baseDir / "djinn.proj";
    if (fs::exists(projFile))
    {
        LOG_DEBUG("loading project file %s", projFile.string().c_str());
        const auto config = ProjectConfig::load(projFile);
        if (!config.name.empty())
            LOG_INFO("Project: %s v%s", config.name.c_str(), config.version.c_str());

        config.applyTo(options);
    }

    result = DjinnCompiler::compileFromDirectory(baseDir, options);

    return result.returnCode;
}
