//
// Created by Claude on 07/03/2026.
//

#ifndef DJINN_PROJECTCONFIG_H
#define DJINN_PROJECTCONFIG_H

#include <string>
#include <optional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ryml.hpp>
#include <ryml_std.hpp>

#include "../utils/Logger.h"
#include "../DjinnCompiler.h"

class YamlNode
{
    ryml::ConstNodeRef _node;
    bool _valid;

public:
    explicit YamlNode(ryml::ConstNodeRef node, bool valid = true)
        : _node(node), _valid(valid)
    {
    }

    YamlNode operator[](const std::string& key) const
    {
        if (!_valid || !_node.has_child(ryml::to_csubstr(key)))
            return YamlNode(_node, false);
        return YamlNode(_node[ryml::to_csubstr(key)]);
    }

    [[nodiscard]] YamlNode at(const std::string& path) const
    {
        YamlNode current = *this;
        size_t start = 0;
        while (start < path.size())
        {
            size_t dot = path.find('.', start);
            if (dot == std::string::npos) dot = path.size();
            current = current[path.substr(start, dot - start)];
            if (!current._valid) return current;
            start = dot + 1;
        }
        return current;
    }

    [[nodiscard]] bool is_scalar() const { return _valid && _node.has_val(); }
    [[nodiscard]] bool is_map() const { return _valid && _node.is_map(); }
    [[nodiscard]] bool is_seq() const { return _valid && _node.is_seq(); }

    template <typename T>
    [[nodiscard]] std::optional<T> get(const std::string& path) const
    {
        YamlNode node = at(path);
        if (!node._valid || !node._node.has_val()) return std::nullopt;
        try
        {
            T value;
            node._node >> value;
            return value;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    template <typename T>
    [[nodiscard]] T get(const std::string& path, const T& defaultValue) const
    {
        auto result = get<T>(path);
        return result.value_or(defaultValue);
    }

    [[nodiscard]] std::vector<std::string> get_string_list(const std::string& path) const
    {
        std::vector<std::string> result;
        YamlNode node = at(path);
        if (!node._valid || !node._node.is_seq()) return result;
        for (const auto& child : node._node.children())
        {
            if (child.has_val())
            {
                std::string val;
                child >> val;
                result.push_back(val);
            }
        }
        return result;
    }

    [[nodiscard]] bool has(const std::string& path) const
    {
        return at(path)._valid;
    }

    explicit operator bool() const { return _valid; }
};

struct LoggerConfig
{
    std::string level = "INFO";
    std::string format;
};

struct OutputConfig
{
    std::string fileName;
    std::string directory;
    std::optional<bool> generateBinary;
    std::optional<bool> runAfterCompile;
    std::optional<bool> bundleModules;
};

struct InternalsConfig
{
    std::optional<bool> printTokens;
    std::optional<bool> printAst;
    std::optional<bool> printIr;
    std::optional<bool> printMacroExpansion;
};

struct GeneratorConfig
{
    int optimizationLevel = 2;
};

struct CompilerConfig
{
    LoggerConfig logger;
    OutputConfig output;
    InternalsConfig internals;
    GeneratorConfig generator;
    std::vector<std::string> libs = {"std"}; // "std" = djinn stdlib, others = .ll link targets
    std::optional<bool> libraryMode;
};

struct RuntimeConfig
{
    LoggerConfig logger;
};

struct ProjectConfig
{
    std::string name;
    std::string version;
    CompilerConfig compiler;
    RuntimeConfig runtime;

    static ProjectConfig load(const std::filesystem::path& projFile)
    {
        ProjectConfig config;

        if (!std::filesystem::exists(projFile))
            return config;

        std::ifstream file(projFile);
        if (!file.is_open())
            return config;

        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        if (content.empty())
            return config;

        ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
        YamlNode root(tree.rootref());

        config.name = root.get<std::string>("project", "");
        if (config.name.empty())
            LOG_ERROR(" [project] project file '%s' does not have a valid 'project' name declared!",
                      projFile.string().c_str());
        config.version = root.get<std::string>("version", "1.0.0");

        config.compiler.logger.level = root.get<std::string>("compiler.logger.level", "TRACE");
        config.compiler.logger.format = root.get<std::string>("compiler.logger.format", "");

        // compiler.output
        config.compiler.output.fileName = root.get<std::string>("compiler.output.file", "");
        config.compiler.output.directory = root.get<std::string>("compiler.output.directory", "");
        config.compiler.output.generateBinary = root.get<bool>("compiler.output.generate-binary");
        config.compiler.output.runAfterCompile = root.get<bool>("compiler.output.run-after-compile");
        config.compiler.output.bundleModules = root.get<bool>("compiler.output.bundle-modules");

        // compiler.internals
        config.compiler.internals.printTokens = root.get<bool>("compiler.internals.print-tokens");
        config.compiler.internals.printAst = root.get<bool>("compiler.internals.print-ast");
        config.compiler.internals.printIr = root.get<bool>("compiler.internals.print-ir");
        config.compiler.internals.printMacroExpansion = root.get<bool>("compiler.internals.print-macro-expansion");

        // compiler.generator
        config.compiler.generator.optimizationLevel = root.get<int>("compiler.generator.optimization-level", 2);

        // compiler.libs
        if (root.has("compiler.libs"))
            config.compiler.libs = root.get_string_list("compiler.libs");

        // compiler (top-level)
        config.compiler.libraryMode = root.get<bool>("compiler.library-mode");

        // runtime
        config.runtime.logger.level = root.get<std::string>("runtime.logger.level", "INFO");
        return config;
    }

    void applyTo(CompilerOptions& options) const
    {
        const auto& cc = compiler;

        auto applyOpt = [](auto& target, const auto& source)
        {
            if (source.has_value()) target = *source;
        };

        applyOpt(options.generateBinary, cc.output.generateBinary);
        applyOpt(options.runAfterCompile, cc.output.runAfterCompile);
        applyOpt(options.bundleModules, cc.output.bundleModules);
        applyOpt(options.libraryMode, cc.libraryMode);
        applyOpt(options.print_ast, cc.internals.printAst);
        applyOpt(options.print_ir, cc.internals.printIr);
        applyOpt(options.print_macro_expansion, cc.internals.printMacroExpansion);

        if (!cc.output.fileName.empty() && options.outputFileName.empty())
            options.outputFileName = cc.output.fileName;
        if (!cc.output.directory.empty() && options.outputDirectory == "build")
            options.outputDirectory = cc.output.directory;

        bool hasStd = false;
        for (const auto& lib : cc.libs)
        {
            if (lib == "std")
                hasStd = true;
            else
                options.linkLibraries.emplace_back(lib);
        }
        options.includeStd = hasStd;

        options.runtimeProperties.loggerLevel = runtime.logger.level;
    }
};

#endif //DJINN_PROJECTCONFIG_H