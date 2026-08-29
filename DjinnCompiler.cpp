//
// Created by Luke on 29/12/2025.
//

#include "DjinnCompiler.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stacktrace>
#include <functional>
#include <map>

#include "binder/Binder.h"
#include "generator/Generator.h"
#include "evaluator/ConstEvaluator.h"
#include "lexer/Lexer.h"
#include "parser/parser.h"
#include "utils/Logger.h"
#include "utils/StopWatch.h"
#include "lib/DjLibReader.h"
#include "lib/DjLibWriter.h"
#include "jit/JitRunner.h"

#ifndef DJINN_CLANG_PATH
#define DJINN_CLANG_PATH "clang"
#endif

#ifdef _WIN32
#define CLANG_LINK_ARGS "-flto -fuse-ld=lld"
#else
#define CLANG_LINK_ARGS "-flto -fuse-ld=lld -lm -lpthread"
#endif

namespace
{
    std::string compute_hash(const std::string& data)
    {
        auto h = std::hash<std::string>{}(data);
        std::ostringstream oss;
        oss << std::hex << h;
        return oss.str();
    }

    struct BuildCache
    {
        std::map<std::string, std::string> fileHashes;
        std::string irHash;
        int optimizationLevel = -1;

        static BuildCache load(const std::string& cachePath)
        {
            BuildCache cache;
            std::ifstream file(cachePath);
            if (!file) return cache;

            std::string line;
            while (std::getline(file, line))
            {
                if (line.starts_with("ir:"))
                    cache.irHash = line.substr(3);
                else if (line.starts_with("opt:"))
                    cache.optimizationLevel = std::stoi(line.substr(4));
                else
                {
                    auto sep = line.find(':');
                    if (sep != std::string::npos)
                        cache.fileHashes[line.substr(0, sep)] = line.substr(sep + 1);
                }
            }
            return cache;
        }

        void save(const std::string& cachePath) const
        {
            std::ofstream file(cachePath);
            for (const auto& [path, hash] : fileHashes)
                file << path << ":" << hash << "\n";
            file << "ir:" << irHash << "\n";
            file << "opt:" << optimizationLevel << "\n";
        }

        bool matches(const std::map<std::string, std::string>& current, int optLevel) const
        {
            if (optimizationLevel != optLevel) return false;
            if (fileHashes.size() != current.size()) return false;
            for (const auto& [path, hash] : current)
            {
                auto it = fileHashes.find(path);
                if (it == fileHashes.end() || it->second != hash) return false;
            }
            return true;
        }
    };

    std::map<std::string, std::string> hash_source_files(const std::set<std::filesystem::path>& files)
    {
        std::map<std::string, std::string> result;
        for (const auto& f : files)
        {
            std::ifstream in(f, std::ios::binary);
            if (!in) continue;
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            result[f.filename().string()] = compute_hash(content);
        }
        return result;
    }
}

const std::string preludes[] = {
    // DO NOT TOUCH IT! ORDERING MATTERS
    "sys/intrinsics.djinn",
    "sys/debug.djinn",
    "sys/console.djinn",
    "types/types.djinn",
    "builtin/coro.djinn",
};

const std::filesystem::path runtimePaths[] = {
    "runtime/djinn_runtime.c",
    "runtime/logger.c",
};


static std::filesystem::path resolve_std_path(const std::filesystem::path& given)
{
    namespace fs = std::filesystem;
    if (fs::exists(given)) return given;

    const fs::path thisFile(__FILE__);
    const auto projectRoot = thisFile.parent_path();
    const auto candidate = projectRoot / "std";
    if (fs::exists(candidate)) return candidate;

    return given;
}


static size_t find_matching_paren(const std::string& s, size_t openPos)
{
    int depth = 1;
    auto i = openPos;
    while (i < s.size() && depth > 0)
    {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        i++;
    }
    return i;
}

static void collect_expansion_steps(std::string& body,
                                    const std::vector<Parser::MacroExpansionRecord>& expansions,
                                    size_t startIdx,
                                    std::vector<std::string>& steps)
{
    for (size_t j = startIdx; j < expansions.size(); j++)
    {
        const auto& inner = expansions[j];
        std::string pattern = inner.macroName + "(";
        auto pos = body.find(pattern);
        if (pos == std::string::npos) continue;

        auto end = find_matching_paren(body, pos + pattern.size());
        body.replace(pos, end - pos, inner.expandedText);

        collect_expansion_steps(body, expansions, j + 1, steps);

        std::ostringstream step;
        step << inner.macroName << "(" << inner.argsText << ") => " << inner.expandedText;
        steps.push_back(step.str());
    }
}

static std::string apply_macro_expansions(const std::string& source,
                                          const std::vector<Parser::MacroExpansionRecord>& expansions)
{
    auto result = source;
    for (size_t i = 0; i < expansions.size(); i++)
    {
        const auto& expansion = expansions[i];
        std::string callPattern = expansion.macroName + "(";
        size_t searchFrom = 0;
        while (true)
        {
            auto pos = result.find(callPattern, searchFrom);
            if (pos == std::string::npos) break;

            auto commentCheck = result.rfind("/*", pos);
            auto commentEnd = result.rfind("*/", pos);
            if (commentCheck != std::string::npos &&
                (commentEnd == std::string::npos || commentEnd < commentCheck))
            {
                searchFrom = pos + 1;
                continue;
            }

            auto end = find_matching_paren(result, pos + callPattern.size());

            auto body = expansion.expandedText;
            std::vector<std::string> steps;
            collect_expansion_steps(body, expansions, i + 1, steps);

            std::ostringstream oss;
            oss << "/*\n";
            for (const auto& step : steps)
                oss << "    " << step << "\n";
            oss << "    " << expansion.macroName << "(" << expansion.argsText << ") => " << body << "\n";
            oss << "*/ " << body;

            auto replacement = oss.str();
            result.replace(pos, end - pos, replacement);
            searchFrom = pos + replacement.size();
            break;
        }
    }
    return result;
}

static void merge_program_into(Program& target, Program& source)
{
    for (auto& i : source.imports) target.imports.push_back(std::move(i));
    for (auto& s : source.structs) target.structs.push_back(std::move(s));
    for (auto& f : source.functions) target.functions.push_back(std::move(f));
    for (auto& e : source.enums) target.enums.push_back(std::move(e));
    for (auto& n : source.namespaces) target.namespaces.push_back(std::move(n));
    for (auto& ef : source.externFunctions) target.externFunctions.push_back(std::move(ef));
    for (auto& iface : source.interfaces) target.interfaces.push_back(std::move(iface));
    for (auto& impl : source.impls) target.impls.push_back(std::move(impl));
    for (auto& ce : source.constExprs) target.constExprs.push_back(std::move(ce));
    for (auto& m : source.macros) target.macros.push_back(std::move(m));
    for (auto& ct : source.compileTimeBlocks) target.compileTimeBlocks.push_back(std::move(ct));
}

static void resolve_compile_time_blocks(Program& program, ConstEvaluator& evaluator)
{
    std::vector<std::unique_ptr<CompileTimeBlock>> blocks;
    blocks.swap(program.compileTimeBlocks);

    for (auto& block : blocks)
    {
        ConstValue val = evaluator.evaluate(*block->condition);
        LOG_DEBUG("[comptime] top-level compile-time block: condition=%s",
                  val.toBool() ? "true" : "false");
        if (val.toBool())
        {
            if (block->thenProgram)
            {
                resolve_compile_time_blocks(*block->thenProgram, evaluator);
                merge_program_into(program, *block->thenProgram);
            }
        }
        else
        {
            if (block->elseProgram)
            {
                resolve_compile_time_blocks(*block->elseProgram, evaluator);
                merge_program_into(program, *block->elseProgram);
            }
        }
    }
}

static void register_intrinsic_values(ConstEvaluator& eval, const CompilerOptions& options)
{
    auto def = [&eval](const std::string& structName, const std::string& field, ConstValue val)
    {
        eval.defineConstant("std::sys::" + structName + "." + field, val);
        eval.defineConstant(structName + "." + field, val);
    };
    auto defBool = [&def](const std::string& s, const std::string& f, bool val)
    {
        def(s, f, ConstValue::makeInt(val ? 1 : 0, 1, false));
    };
    auto defInt = [&def](const std::string& s, const std::string& f, int64_t val)
    {
        def(s, f, ConstValue::makeInt(val));
    };

    constexpr bool isWindows =
#ifdef _WIN32
        true;
#else
    false;
#endif
    constexpr bool isLinux =
#ifdef __linux__
        true;
#else
        false;
#endif
    constexpr bool isMacOs =
#ifdef __APPLE__
        true;
#else
        false;
#endif

    defBool("Platform", "Windows", isWindows);
    defBool("Platform", "Linux", isLinux);
    defBool("Platform", "MacOs", isMacOs);

    constexpr bool isX64 =
#if defined(__x86_64__) || defined(_M_X64)
        true;
#else
    false;
#endif
    constexpr bool isX86 =
#if defined(__i386__) || defined(_M_IX86)
        true;
#else
        false;
#endif
    constexpr bool isArm64 =
#if defined(__aarch64__) || defined(_M_ARM64)
        true;
#else
        false;
#endif

    defBool("Arch", "X64", isX64);
    defBool("Arch", "X86", isX86);
    defBool("Arch", "Arm64", isArm64);

    defBool("Build", "Debug", options.debugMode);
    defBool("Build", "Release", options.releaseMode);

    defInt("Runtime", "PointerSize", sizeof(void*));
}

static ConstEvaluator create_comptime_evaluator(
    const std::vector<std::shared_ptr<Program>>& programs, const CompilerOptions& options)
{
    ConstEvaluator eval;
    register_intrinsic_values(eval, options);

    for (const auto& prog : programs)
    {
        for (const auto& ce : prog->constExprs)
        {
            if (ce->isIntrinsic || !ce->value) continue;
            ConstValue val = eval.evaluate(*ce->value);
            if (!val.isError())
            {
                eval.defineConstant(ce->name.token_name, val);
                const std::string prefix = prog->getNamespacePrefix();
                if (!prefix.empty())
                    eval.defineConstant(prefix + ce->name.token_name, val);
            }
        }
    }

    return eval;
}

CompilerResult DjinnCompiler::compileFromDirectory(const std::filesystem::path& path, const CompilerOptions& options)
{
    utils::BuildSummary summary;
    namespace fs = std::filesystem;
    assert(!options.outputDirectory.empty() && "You need give output directory!");

    fs::create_directories(options.outputDirectory);

    {
        std::ofstream props(options.outputDirectory + "/runtime.properties");
        options.runtimeProperties.serialize(props);
    }

    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program>> programs;

    std::vector<std::string> preludeTypeNames;
    std::vector<std::string> stdTypeNames;

    const auto parseFile = [&](const fs::path& filePath, const bool registerPrelude, const bool isUserCode)
    {
        std::ifstream file(filePath);
        if (!file) return;

        const auto source = std::string(
            std::istreambuf_iterator(file),
            std::istreambuf_iterator<char>()
        );

        auto file_name = filePath.string();
        diagnostics.registerSource(file_name, source);

        std::vector<Token> tokens;
        {
            auto _phase = summary.phase("lexer");
            Lexer lexer(source, file_name);
            tokens = lexer.tokenize();
        }

        std::unique_ptr<Program> program;
        std::vector<Parser::MacroExpansionRecord> macroExpansions;
        {
            auto _phase = summary.phase("parser");
            Parser parser(tokens, diagnostics);
            parser.printMacroExpansion = options.print_macro_expansion;

            if (registerPrelude)
            {
                for (const auto& name : preludeTypeNames)
                    parser.registerKnownType(name);
            }
            if (isUserCode)
            {
                for (const auto& name : stdTypeNames)
                    parser.registerKnownType(name);
            }

            program = parser.parse(file_name);
            macroExpansions = std::move(parser.macroExpansions);
        }

        if (options.print_ast && isUserCode)
        {
            std::ostringstream oss;
            oss << "=====AST [" << file_name << "]=====\n";
            program->print(oss);
            oss << "=====";
            LOG_DEBUG("%s", oss.str().c_str());
        }

        if (options.dump_macro_expansion && isUserCode && !macroExpansions.empty())
        {
            auto expandedSource = apply_macro_expansions(source, macroExpansions);
            auto expandedPath = fs::path(options.outputDirectory) / fs::path(file_name).filename().replace_extension(
                ".djinn.expanded");
            if (std::ofstream expandedFile(expandedPath); expandedFile.is_open())
            {
                expandedFile << expandedSource;
                LOG_INFO("[macro-dump] Expanded source written to: %s", expandedPath.string().c_str());
            }
        }

        programs.emplace_back(std::move(program));
    };

    const auto stdLibPath = resolve_std_path(options.stdLibPath);
    const auto stdCanonical = (options.includeStd && fs::exists(stdLibPath))
                                  ? fs::canonical(stdLibPath)
                                  : fs::path{};

    std::set<fs::path> parsedFiles;

    const std::string outputSep(1, static_cast<char>(fs::path::preferred_separator));
    const auto out_file_path = options.outputDirectory + outputSep + options.outputFileName;
    const std::string llPath = out_file_path + ".ll";
    const std::string cachePath = options.outputDirectory + outputSep + ".djinn.cache";
    auto exePath = out_file_path +
#ifdef _WIN32
        ".exe";
#else
    "";
#endif

    try
    {
        // Collect all source file paths first (for hashing)
        std::set<fs::path> allSourceFiles;
        if (options.includeStd && !stdCanonical.empty())
        {
            for (const auto& prelude : preludes)
            {
                auto preludePath = fs::canonical(stdLibPath / prelude);
                if (fs::exists(preludePath))
                    allSourceFiles.insert(preludePath);
            }
            for (const auto& entry : fs::recursive_directory_iterator(stdLibPath))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".djinn")
                    allSourceFiles.insert(fs::canonical(entry.path()));
            }
        }
        if (fs::exists(path))
        {
            for (const auto& entry : fs::recursive_directory_iterator(path))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".djinn") continue;
                auto canonical = fs::canonical(entry.path());
                if (!stdCanonical.empty())
                {
                    auto rel = canonical.lexically_relative(stdCanonical);
                    if (!rel.empty() && !rel.string().starts_with("..")) continue;
                }
                allSourceFiles.insert(canonical);
            }
        }

        // Check source-level cache: if no source file changed, skip everything
        std::map<std::string, std::string> currentHashes;
        BuildCache cache;
        if (!options.noCache)
        {
            auto _phase = summary.phase("cache");
            currentHashes = hash_source_files(allSourceFiles);
            cache = BuildCache::load(cachePath);
            LOG_DEBUG("[cache] hashed %zu source files, loaded cache from %s", currentHashes.size(), cachePath.c_str());
        }

        if (!options.noCache && cache.matches(currentHashes, options.optimizationLevel) && fs::exists(exePath))
        {
            LOG_INFO("[cache] sources unchanged, skipping build");

            int programReturnCode = 0;
            if (options.runAfterCompile)
            {
                auto _phase = summary.phase("run");
                programReturnCode = system(exePath.c_str());
#ifndef _WIN32
                if (WIFEXITED(programReturnCode))
                    programReturnCode = WEXITSTATUS(programReturnCode);
#endif
            }
            summary.print();
            return {.returnCode = programReturnCode};
        }

        // Sources changed — full compilation
        if (options.includeStd && !stdCanonical.empty())
        {
            for (const auto& prelude : preludes)
            {
                const auto preludePath = stdLibPath / prelude;
                if (!fs::exists(preludePath))
                {
                    LOG_ERROR("Prelude file %s was not found!", preludePath.c_str());
                    continue;
                }

                try
                {
                    parsedFiles.insert(fs::canonical(preludePath));
                    parseFile(preludePath, false, false);
                    if (!programs.empty())
                    {
                        const auto& preludeProgram = programs.back();
                        for (const auto& s : preludeProgram->structs) preludeTypeNames.push_back(s->name.token_name);
                        for (const auto& e : preludeProgram->enums) preludeTypeNames.push_back(e->name.token_name);
                        for (const auto& a : preludeProgram->attributeDecls)
                            if (!a->fields.empty()) preludeTypeNames.push_back(a->name.token_name);
                    }
                }
                catch (const CompileError& compile_error)
                {
                    LOG_ERROR("Error parsing prelude: %s", compile_error.message().c_str());
                }
            }

            for (const auto& entry : fs::recursive_directory_iterator(stdLibPath))
            {
                try
                {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".djinn") continue;

                    auto canonical = fs::canonical(entry.path());
                    if (parsedFiles.contains(canonical)) continue;
                    parsedFiles.insert(canonical);

                    parseFile(entry.path(), true, false);
                }
                catch (const CompileError& compile_error)
                {
                    LOG_ERROR("Error parsing %s: %s", entry.path().string().c_str(),
                              compile_error.message().c_str());
                }
            }
        }

        for (const auto& prog : programs)
        {
            for (const auto& s : prog->structs) stdTypeNames.push_back(s->name.token_name);
            for (const auto& e : prog->enums) stdTypeNames.push_back(e->name.token_name);
            for (const auto& a : prog->attributeDecls)
                if (!a->fields.empty()) stdTypeNames.push_back(a->name.token_name);
        }

        auto isPrimitiveTypeName = [](const std::string& n) -> bool
        {
            if (n.empty()) return false;
            if (n == "void" || n == "auto" || n == "bool" || n == "str" || n == "string") return true;
            if ((n[0] == 'i' || n[0] == 'u' || n[0] == 'f') && n.size() > 1)
            {
                for (size_t i = 1; i < n.size(); ++i)
                    if (!std::isdigit(n[i])) return false;
                return true;
            }
            return false;
        };

        for (const auto& djlibPath : options.djlibPaths)
        {
            djlib::DjLibReader reader;
            if (reader.read(djlibPath))
            {
                const auto& meta = reader.metadata();
                if (meta.contains("structs"))
                    for (const auto& s : meta["structs"])
                    {
                        auto name = s.at("name").get<std::string>();
                        auto shortName = name;
                        if (auto pos = name.rfind("::"); pos != std::string::npos)
                            shortName = name.substr(pos + 2);
                        if (isPrimitiveTypeName(shortName)) continue;
                        stdTypeNames.push_back(name);
                        if (shortName != name)
                            stdTypeNames.push_back(shortName);
                    }
                if (meta.contains("enums"))
                    for (const auto& e : meta["enums"])
                    {
                        auto name = e.at("name").get<std::string>();
                        auto shortName = name;
                        if (auto pos = name.rfind("::"); pos != std::string::npos)
                            shortName = name.substr(pos + 2);
                        if (isPrimitiveTypeName(shortName)) continue;
                        stdTypeNames.push_back(name);
                        if (shortName != name)
                            stdTypeNames.push_back(shortName);
                    }

                if (meta.contains("genericSources"))
                {
                    for (const auto& gs : meta["genericSources"])
                    {
                        auto source = gs.at("source").get<std::string>();
                        auto file = gs.at("file").get<std::string>();
                        auto ns = gs.value("namespace", std::string{});
                        auto libFileName = "[djlib]:" + file;

                        diagnostics.registerSource(libFileName, source);
                        Lexer lexer(source, libFileName);
                        auto tokens = lexer.tokenize();

                        Parser parser(tokens, diagnostics);
                        for (const auto& name : preludeTypeNames)
                            parser.registerKnownType(name);
                        for (const auto& name : stdTypeNames)
                            parser.registerKnownType(name);

                        auto program = parser.parse(libFileName);
                        program->fileNamespace = ns;

                        // Keep only generic declarations + attributes — non-generics already injected by DjLibReader
                        std::erase_if(program->structs, [](const auto& s) { return s->genericParams.empty(); });
                        std::erase_if(program->enums, [](const auto& e) { return e->genericParams.empty(); });
                        program->functions.clear();
                        program->externFunctions.clear();
                        program->staticVars.clear();
                        program->constExprs.clear();

                        programs.emplace_back(std::move(program));
                        LOG_DEBUG("[djlib] re-parsed generic source: %s (ns=%s)", file.c_str(), ns.c_str());
                    }
                }
            }
        }

        if (fs::exists(path))
        {
            for (const auto& entry : fs::recursive_directory_iterator(path))
            {
                try
                {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".djinn") continue;

                    auto canonical = fs::canonical(entry.path());
                    if (parsedFiles.contains(canonical)) continue;

                    if (!stdCanonical.empty())
                    {
                        auto rel = canonical.lexically_relative(stdCanonical);
                        if (!rel.empty() && !rel.string().starts_with("..")) continue;
                    }

                    parsedFiles.insert(canonical);
                    parseFile(entry.path(), true, true);
                }
                catch (const CompileError& compile_error)
                {
                    LOG_ERROR("Error parsing %s: %s", entry.path().string().c_str(), compile_error.message().c_str());
                }
            }
        }

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

        {
            auto _phase = summary.phase("comptime");
            auto comptimeEval = create_comptime_evaluator(programs, options);
            for (auto& prog : programs)
                resolve_compile_time_blocks(*prog, comptimeEval);
        }

        std::vector<djlib::DjLibReader> djlibReaders;
        if (!options.djlibPaths.empty())
        {
            auto _phase = summary.phase("djlib-load");
            for (const auto& djlibPath : options.djlibPaths)
            {
                djlib::DjLibReader reader;
                if (!reader.read(djlibPath))
                {
                    LOG_ERROR("Failed to load djlib: %s", djlibPath.string().c_str());
                    return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
                }
                djlibReaders.push_back(std::move(reader));
            }
        }

        Binder binder(diagnostics);

        for (const auto& reader : djlibReaders)
        {
            binder.injectLibrarySymbols(reader);
            for (auto& program : programs)
            {
                reader.populateMacros(*program);
            }
        }

        BindingResult bindResult;
        {
            auto _phase = summary.phase("binding");
            bindResult = binder.bindAll(programs);
        }
        if (!bindResult.success)
        {
            std::cerr << diagnostics.render();
            LOG_ERROR("Bind result return failed.");
            return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
        }

        auto generator = Generator(diagnostics, bindResult.globalScope);
        generator.libraryMode = options.libraryMode;
        generator.stdDeclOnly = options.stdDeclOnly;
        generator.moduleName = options.outputFileName.empty() ? "djinn_module" : options.outputFileName;
        generator.reflectionMode = options.reflectionMode;
        {
            auto _phase = summary.phase("codegen");
            generator.generate();
        }

        if (!options.linkLibraries.empty() || !djlibReaders.empty())
        {
            auto _phase = summary.phase("link");
            if (!options.linkLibraries.empty())
            {
                if (!generator.linkModules(options.linkLibraries))
                {
                    summary.print();
                    return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
                }
            }
            for (const auto& reader : djlibReaders)
            {
                if (!generator.linkBitcode(reader.getBitcodeData()))
                {
                    summary.print();
                    return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
                }
            }
        }

        if (!generator.verify())
        {
            std::cerr << diagnostics.render();
            summary.print();
            return {.returnCode = 1, .verifiedIr = false, .diagnostics = diagnostics.get_diagnostics()};
        }

        if (options.print_ir)
        {
            LOG_INFO("RESULT\n\n%s", generator.print().c_str());
        }

        {
            auto _phase = summary.phase("llvm passes");
            generator.run_passes(options.skipCoroPasses);
        }

        if (!generator.verify())
        {
            std::cerr << diagnostics.render();
            summary.print();
            return {.returnCode = 1, .verifiedIr = false, .diagnostics = diagnostics.get_diagnostics()};
        }

        if (options.outputDjLib)
        {
            auto _phase = summary.phase("djlib-write");
            djlib::DjLibWriter writer;
            writer.collectSymbols(*bindResult.globalScope);
            writer.collectMacros(programs);
            writer.collectAttributeDecls(programs);
            writer.collectImplBlocks(programs);
            writer.collectConstExprs(*bindResult.globalScope);
            writer.collectStaticVars(*bindResult.globalScope);
            writer.collectGenericSources(programs, diagnostics.getSources());

            auto djlibName = options.outputFileName.empty() ? "output" : options.outputFileName;
            auto djlibPath = fs::path(options.outputDirectory) / (djlibName + ".djlib");
            if (!writer.write(djlibPath, generator.getModule()))
            {
                LOG_ERROR("Failed to write djlib: %s", djlibPath.string().c_str());
                return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
            }
        }

        std::string generatedIr = generator.print();

        // Check IR-level cache: if generated IR unchanged, skip clang
        auto irHash = compute_hash(generatedIr);
        bool skipClang = (!options.noCache
            && cache.irHash == irHash
            && cache.optimizationLevel == options.optimizationLevel
            && fs::exists(exePath));

        // Always write .ll for debugging
        {
            std::ofstream outFile(llPath);
            outFile << generatedIr;
        }

        if (skipClang)
        {
            LOG_INFO("[cache] IR unchanged, skipping clang");
        }
        else if (options.generateBinary)
        {
            auto _phase = summary.phase("clang");
            std::string runtimeArg;
            for (auto& runtime_path : runtimePaths)
            {
                runtimeArg += " " + runtime_path.string();
            }

            auto optFlag = "-O" + std::to_string(options.optimizationLevel);
            auto cmdString = "\"" DJINN_CLANG_PATH "\" " CLANG_LINK_ARGS " " + optFlag + " " + llPath + runtimeArg +
                " -o " + exePath;
            LOG_DEBUG("Executing compilation command: %s", cmdString.c_str());
            const auto compile_result = system(cmdString.c_str());
            LOG_DEBUG("Compile return: %d", compile_result);
        }

        // Save cache
        BuildCache newCache;
        newCache.fileHashes = currentHashes;
        newCache.irHash = irHash;
        newCache.optimizationLevel = options.optimizationLevel;
        newCache.save(cachePath);
        LOG_DEBUG("[cache] saved %zu file hashes to %s", currentHashes.size(), cachePath.c_str());

        int programReturnCode = 0;
        if (options.runAfterCompile)
        {
            auto _phase = summary.phase("run");
            programReturnCode = system(exePath.c_str());
#ifndef _WIN32
            if (WIFEXITED(programReturnCode))
            {
                programReturnCode = WEXITSTATUS(programReturnCode);
            }
#endif
        }

        if (diagnostics.warningCount() > 0)
        {
            std::cerr << diagnostics.render();
        }

        summary.print();

        LOG_DEBUG("exit code %d", programReturnCode);
        return {.returnCode = programReturnCode, .diagnostics = diagnostics.get_diagnostics()};
    }
    catch (const CompileError& e)
    {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        diagnostics.printToStderr(std::stacktrace::current());
        summary.print();
        return {.returnCode = 1, .diagnostics = diagnostics.get_diagnostics()};
    }
}

CompilerResult DjinnCompiler::run(const std::string& source, const CompilerOptions& options)
{
    auto global_watch = INIT_STOPWATCH_WITH_LEVEL("build time", logger::Level::INFO);
    namespace fs = std::filesystem;

    DiagnosticEngine diagnostics;
    std::vector<std::shared_ptr<Program>> programs;
    std::shared_ptr<Program> userProgram;
    std::string generatedIr;
    std::string expandedSourceResult;

    bool irVerified = true;
    std::string runtimeErrorReport;
    auto makeResult = [&](int returnCode, const std::stacktrace& trace)
    {
        if (!options.silentMode && !diagnostics.get_diagnostics().empty())
        {
            diagnostics.printToStderr(trace);
        }
        return CompilerResult{
            .returnCode = returnCode,
            .verifiedIr = irVerified,
            .program = userProgram,
            .ir = generatedIr,
            .diagnostics = diagnostics.get_diagnostics(),
            .expandedSource = expandedSourceResult,
            .runtimeErrorReport = runtimeErrorReport
        };
    };

    try
    {
        std::set<fs::path> parsedFiles;
        std::vector<std::string> preludeTypeNames;

        auto stdLibPath = resolve_std_path(options.stdLibPath);
        if (options.includeStd && fs::exists(stdLibPath))
        {
            for (const auto& prelude : preludes)
            {
                const auto preludePath = fs::path(stdLibPath) / prelude;
                if (!fs::exists(preludePath))
                    continue;

                try
                {
                    std::ifstream file(preludePath);
                    if (!file)
                    {
                        LOG_ERROR("Prelude file %s was not found!", preludePath.c_str());
                        continue;
                    }

                    parsedFiles.insert(fs::canonical(preludePath));
                    const auto stdSource = std::string(
                        std::istreambuf_iterator(file),
                        std::istreambuf_iterator<char>()
                    );

                    auto file_id = preludePath.string();
                    diagnostics.registerSource(file_id, stdSource);

                    Lexer lexer(stdSource);
                    const auto tokens = lexer.tokenize();

                    Parser parser(tokens, diagnostics);
                    auto program = parser.parse(file_id);

                    for (const auto& s : program->structs) preludeTypeNames.push_back(s->name.token_name);
                    for (const auto& e : program->enums) preludeTypeNames.push_back(e->name.token_name);
                    for (const auto& a : program->attributeDecls)
                        if (!a->fields.empty()) preludeTypeNames.push_back(a->name.token_name);

                    programs.emplace_back(std::move(program));
                }
                catch (const CompileError& compile_error)
                {
                    if (!options.silentMode)
                    {
                        LOG_ERROR("Error parsing prelude: %s", compile_error.message().c_str());
                    }
                }
            }

            // Phase 2: Parse remaining std library files (with prelude types registered)
            for (const auto& entry : fs::recursive_directory_iterator(stdLibPath))
            {
                try
                {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".djinn") continue;

                    auto canonical = fs::canonical(entry.path());
                    if (parsedFiles.contains(canonical)) continue;

                    // if (fs::equivalent(entry.path(), preludePath)) continue;

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
                    // Register prelude types so other std files can use them
                    for (const auto& name : preludeTypeNames)
                        parser.registerKnownType(name);

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

        diagnostics.registerSource("main.djinn", source);

        Lexer lexer(source, "main");
        const auto tokens = lexer.tokenize();

        Parser parser(tokens, diagnostics);
        parser.printMacroExpansion = options.print_macro_expansion;

        for (const auto& name : preludeTypeNames)
            parser.registerKnownType(name);
        for (const auto& prog : programs)
        {
            for (const auto& s : prog->structs) parser.registerKnownType(s->name.token_name);
            for (const auto& e : prog->enums) parser.registerKnownType(e->name.token_name);
            for (const auto& a : prog->attributeDecls)
                if (!a->fields.empty()) parser.registerKnownType(a->name.token_name);
        }

        auto program = parser.parse("main");

        if (options.print_ast)
        {
            std::ostringstream oss;
            oss << "=====AST=====\n";
            program->print(oss);
            oss << "=====";
            LOG_DEBUG("%s", oss.str().c_str());
        }

        if (options.dump_macro_expansion && !parser.macroExpansions.empty())
        {
            auto expandedSource = apply_macro_expansions(source, parser.macroExpansions);
            expandedSourceResult = expandedSource;

            auto expandedPath = fs::path(options.outputDirectory) / "main.djinn.expanded";
            if (std::ofstream expandedFile(expandedPath); expandedFile.is_open())
            {
                expandedFile << expandedSource;
                LOG_INFO("[macro-dump] Expanded source written to: %s", expandedPath.string().c_str());
            }
        }

        userProgram = std::shared_ptr<Program>(std::move(program));
        programs.emplace_back(userProgram);

        if (diagnostics.hasErrors())
        {
            return makeResult(1, std::stacktrace::current());
        }

        // Resolve top-level compile-time blocks before binding
        auto comptimeEval = create_comptime_evaluator(programs, options);
        for (auto& prog : programs)
            resolve_compile_time_blocks(*prog, comptimeEval);

        Binder binder(diagnostics);
        const auto bindResult = binder.bindAll(programs);
        if (!bindResult.success)
        {
            return makeResult(1, std::stacktrace::current());
        }

        auto generator = Generator(diagnostics, bindResult.globalScope);
        generator.libraryMode = options.libraryMode;
        generator.stdDeclOnly = options.stdDeclOnly;
        generator.moduleName = options.outputFileName.empty() ? "djinn_module" : options.outputFileName;
        generator.reflectionMode = options.reflectionMode;
        generator.generate();
        irVerified = generator.verify();

        if (options.print_ir)
        {
            LOG_INFO("RESULT\n\n%s", generator.print().c_str());
        }

        generatedIr = generator.print();

        const bool useJit = options.jitExecution && options.generateBinary && options.runAfterCompile
            && std::getenv("DJINN_DISABLE_JIT") == nullptr
            && djinn::jitRuntimeAvailable();

        std::string outputDir = options.outputDirectory;
        std::string outputFileName = options.outputFileName.empty() ? "main" : options.outputFileName;
        if (!useJit && options.useTempDirectory)
        {
            // unique per invocation: rand() alone collides across processes (parallel test runs)
            const auto dirName = std::to_string(std::chrono::system_clock::now().time_since_epoch().count())
                + "_" + std::to_string(rand());
            outputDir = (fs::temp_directory_path() / "djinn_build" / dirName).string();
            fs::create_directories(outputDir);
        }

        const std::string outputSep(1, static_cast<char>(fs::path::preferred_separator));
        const std::string llPath = outputDir + outputSep + outputFileName + ".ll";
        const std::string exePath = outputDir + outputSep + outputFileName +
#ifdef _WIN32
            ".exe";
#else
        "";
#endif

        {
            auto pass_stop_watch = INIT_STOPWATCH("run passes");
            generator.run_passes(options.skipCoroPasses);
        }

        generatedIr = generator.print();
        std::ofstream optOutput(llPath);
        optOutput << generatedIr;
        optOutput.close();

        if (!options.generateBinary)
        {
            return makeResult(5555, std::stacktrace::current());
        }

        if (useJit)
        {
            auto [jitModule, jitContext] = generator.takeModule();
            const auto jitExitCode = djinn::executeModule(std::move(jitModule), std::move(jitContext),
                                                          options.optimizationLevel, &runtimeErrorReport);
            if (jitExitCode >= 0)
            {
                LOG_DEBUG("jit exit code %d", jitExitCode);
                return makeResult(jitExitCode, std::stacktrace::current());
            }
            LOG_ERROR("In-process JIT execution failed (code %d)", jitExitCode);
            return makeResult(jitExitCode, std::stacktrace::current());
        }

        std::string runtimeArg;
        for (const auto& runtime_path : runtimePaths)
        {
            runtimeArg += " " + runtime_path.string();
        }

        auto optFlag = "-O" + std::to_string(options.optimizationLevel);
        const auto cmdString = "\"" DJINN_CLANG_PATH "\" " CLANG_LINK_ARGS " " + optFlag + " " + llPath + runtimeArg +
            " -o " + exePath;
        LOG_DEBUG("Executing compilation command: %s", cmdString.c_str());
        int clangResult = system(cmdString.c_str());
        if (clangResult != 0)
        {
            return makeResult(clangResult, std::stacktrace::current());
        }

        int programReturnCode = 0;
        if (options.runAfterCompile)
        {
            programReturnCode = system(exePath.c_str());
#ifndef _WIN32
            if (WIFEXITED(programReturnCode))
            {
                programReturnCode = WEXITSTATUS(programReturnCode);
            }
#endif
        }

        if (diagnostics.warningCount() > 0)
        {
            std::cerr << diagnostics.render();
        }

        LOG_DEBUG("exit code %d", programReturnCode);
        return makeResult(programReturnCode, std::stacktrace::current());
    }
    catch (const CompileError& e)
    {
        diagnostics.emit(Diagnostic(Severity::Error, e.code(), e.message(), e.location()));
        return makeResult(1, std::stacktrace::current());
    }
}
