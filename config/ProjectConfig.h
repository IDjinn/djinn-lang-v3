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

class YamlNode
{
    ryml::ConstNodeRef _node;
    bool _valid;

public:
    explicit YamlNode(ryml::ConstNodeRef node, bool valid = true)
        : _node(node), _valid(valid) {}

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

    template<typename T>
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

    template<typename T>
    [[nodiscard]] T get(const std::string& path, const T& defaultValue) const
    {
        auto result = get<T>(path);
        return result.value_or(defaultValue);
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
};

struct RuntimeConfig
{
    LoggerConfig logger;
};

struct CompilerConfig
{
    int optimizationLevel = 2;
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
            LOG_ERROR(" [project] project file '%s' does not have a valid 'project' name declared!", projFile.string().c_str());

        config.version = root.get<std::string>("version", "1.0.0");
        config.compiler.optimizationLevel = root.get<int>("compiler.options.optimization-level", 2);
        config.runtime.logger.level = root.get<std::string>("runtime.logger.level", "INFO");
        return config;
    }
};

#endif //DJINN_PROJECTCONFIG_H
