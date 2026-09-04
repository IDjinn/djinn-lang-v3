#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

#include "DjinnCompiler.h"
#include "config/ProjectConfig.h"
#include "utils/Logger.h"
#include "lib/DjLibReader.h"

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
        << "  -O0..O3       Set optimization level (default: -O3, passed to clang)\n"
        << "  --no-coro-passes  Skip coroutine lowering passes\n"
        << "  -c            Compile only, do not execute\n"
        << "  -r            Recursively search directories for .djinn files\n"
        << "  -run          Auto run generated exe file\n"
        << "  --lib         Compile as library (no main required)\n"
        << "  --bundle      Bundle all into single .ll (default: per namespace)\n"
        << "  -l <file.ll>  Link external .ll module (can be used multiple times)\n"
        << "  --debug       Set build mode to debug: full runtime error reports (source\n"
        << "                snippets, stack traces, variable history) baked into the binary\n"
        << "  --release     Set build mode to release: minimal runtime error reports\n"
        << "                (file:line + operand values only)\n"
        << "  --exceptions  Opt in to native exceptions: LLVM zero-cost unwinding, classic\n"
        << "                try/catch/finally blocks and C++ exception interop (AOT only)\n"
        << "  --embed-debug-info  Keep debug metadata in the single .ll instead of splitting\n"
        << "                it into <name>.debug.ll (debug builds default to splitting)\n"
        << "  --reflect-all       Generate TypeInfoExt for all struct types\n"
        << "  --reflect-annotated Generate TypeInfoExt only for [Reflect] structs\n"
        << "  --error-enforcement <off|runtime|compiletime|strict>  Error-flow checks (default: compiletime)\n"
        << "  --no-std      Don't include standard library\n"
        << "  --std-decl    Include std declarations only (use with -l std.ll)\n"
        << "  --inspect <file.djlib>  Inspect djlib metadata\n"
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
    options.generateBinary = true;
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
        else if (arg == "--inspect" && i + 1 < argc)
        {
            djlib::DjLibReader reader;
            if (!reader.read(argv[++i]))
            {
                std::cerr << "Failed to read djlib file\n";
                return 1;
            }
            const auto& meta = reader.metadata();
            auto structs = meta.value("structs", nlohmann::json::array()).size();
            auto functions = meta.value("functions", nlohmann::json::array()).size();
            auto externFns = meta.value("externFunctions", nlohmann::json::array()).size();
            auto enums = meta.value("enums", nlohmann::json::array()).size();
            auto interfaces = meta.value("interfaces", nlohmann::json::array()).size();
            auto macros = meta.value("macros", nlohmann::json::array()).size();
            auto implBlocks = meta.value("implBlocks", nlohmann::json::array()).size();

            std::cout << "=== djlib inspect ===\n"
                << "  structs:    " << structs << "\n"
                << "  functions:  " << functions << "\n"
                << "  externs:    " << externFns << "\n"
                << "  enums:      " << enums << "\n"
                << "  interfaces: " << interfaces << "\n"
                << "  macros:     " << macros << "\n"
                << "  impl blocks:" << implBlocks << "\n"
                << "  bitcode:    " << reader.getBitcodeData().size() << " bytes\n";

            if (argc > i + 1 && std::string(argv[i + 1]) == "--json")
            {
                ++i;
                std::cout << "\n" << meta.dump(2) << "\n";
            }
            return 0;
        }
        else if (arg == "-o" && i + 1 < argc)
        {
            std::string name = argv[++i];
            if (name.size() > 6 && name.substr(name.size() - 6) == ".djlib")
            {
                options.outputDjLib = true;
                options.libraryMode = true;
                options.outputFileName = name.substr(0, name.size() - 6);
            }
            else
            {
                options.outputFileName = name;
            }
        }
        else if (arg == "-out" && i + 1 < argc)
        {
            options.outputDirectory = argv[++i];
        }
        else if (arg == "-run")
        {
            options.runAfterCompile = true;
        }
        else if (arg == "-O0" || arg == "-O1" || arg == "-O2" || arg == "-O3" || arg == "-o3")
        {
            options.optimizationLevel = arg.back() - '0';
        }
        else if (arg == "-ir")
        {
            options.print_ir = true;
        }
        else if (arg == "-ast")
        {
            options.print_ast = true;
        }
        else if (arg == "-macro")
        {
            options.print_macro_expansion = true;
        }
        else if (arg == "--no-coro-passes")
        {
            options.skipCoroPasses = true;
        }
        else if (arg == "-c")
        {
            options.generateBinary = false;
        }
        else if (arg == "--no-cache")
        {
            options.noCache = true;
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
        else if (arg == "--reflect-all")
        {
            options.reflectionMode = "all";
        }
        else if (arg == "--reflect-annotated")
        {
            options.reflectionMode = "annotated";
        }
        else if (arg == "--error-enforcement" && i + 1 < argc)
        {
            const std::string level = argv[++i];
            if (level == "off") options.errorEnforcement = ErrorEnforcement::Off;
            else if (level == "runtime") options.errorEnforcement = ErrorEnforcement::Runtime;
            else if (level == "compiletime") options.errorEnforcement = ErrorEnforcement::CompileTime;
            else if (level == "strict") options.errorEnforcement = ErrorEnforcement::Strict;
            else
            {
                LOG_ERROR("Unknown error enforcement level: %s (expected off|runtime|compiletime|strict)",
                          level.c_str());
                return 1;
            }
        }
        else if (arg == "--debug")
        {
            options.debugMode = true;
            options.releaseMode = false;
        }
        else if (arg == "--release")
        {
            options.debugMode = false;
            options.releaseMode = true;
        }
        else if (arg == "--exceptions")
        {
            options.exceptions = true;
        }
        else if (arg == "--embed-debug-info")
        {
            options.splitDebugInfo = false;
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
            fs::path libPath = argv[++i];
            if (libPath.extension() == ".djlib")
                options.djlibPaths.push_back(libPath);
            else
                options.linkLibraries.push_back(libPath);
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
        logger::configure(config.compiler.logger.level, config.compiler.logger.format);
        config.applyTo(options);

        if (!config.name.empty())
            LOG_INFO("Project: %s v%s", config.name.c_str(), config.version.c_str());
    }

    result = DjinnCompiler::compileFromDirectory(baseDir, options);

    return result.returnCode;
}