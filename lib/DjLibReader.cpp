#include "DjLibReader.h"
#include "DjLibFormat.h"
#include "../utils/Logger.h"

#include <fstream>
#include <cstring>

namespace djlib
{
    bool DjLibReader::read(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
        {
            LOG_ERROR("Failed to open djlib: %s (cwd: %s)", path.string().c_str(),
                      std::filesystem::current_path().string().c_str());
            return false;
        }

        auto fileSize = in.tellg();
        if (fileSize < static_cast<std::streamoff>(sizeof(Header)))
        {
            LOG_ERROR("Invalid djlib (too small): %s", path.string().c_str());
            return false;
        }

        in.seekg(0);
        Header header{};
        in.read(reinterpret_cast<char*>(&header), sizeof(Header));

        if (std::memcmp(header.magic, MAGIC, sizeof(MAGIC)) != 0)
        {
            LOG_ERROR("Invalid djlib magic: %s", path.string().c_str());
            return false;
        }

        if (header.version > VERSION)
        {
            LOG_ERROR("Unsupported djlib version %u (max %u): %s",
                      header.version, VERSION, path.string().c_str());
            return false;
        }

        auto expectedSize = static_cast<std::streamoff>(
            sizeof(Header) + header.metadataSize + header.bitcodeSize);
        if (fileSize < expectedSize)
        {
            LOG_ERROR("Truncated djlib: %s (expected %lld, got %lld)",
                      path.string().c_str(),
                      static_cast<long long>(expectedSize),
                      static_cast<long long>(fileSize));
            return false;
        }

        std::string metadataStr(header.metadataSize, '\0');
        in.read(metadataStr.data(), header.metadataSize);

        _bitcodeData.resize(header.bitcodeSize);
        in.read(_bitcodeData.data(), header.bitcodeSize);

        if (!in.good())
        {
            LOG_ERROR("Read error on djlib: %s", path.string().c_str());
            return false;
        }

        try
        {
            _metadata = nlohmann::json::parse(metadataStr);
        }
        catch (const nlohmann::json::parse_error& e)
        {
            LOG_ERROR("Invalid djlib metadata JSON: %s (%s)", path.string().c_str(), e.what());
            return false;
        }

        _valid = true;
        LOG_INFO("Loaded djlib: %s (metadata=%u bytes, bitcode=%u bytes)",
                 path.string().c_str(), header.metadataSize, header.bitcodeSize);
        return true;
    }

    void DjLibReader::populateSymbolTable(ScopedSymbolTable& scope) const
    {
        if (!_valid) return;

        if (_metadata.contains("interfaces"))
        {
            for (const auto& j : _metadata["interfaces"])
            {
                auto sym = deserializeInterface(j);
                sym->isFromLibrary = true;
                scope.defineInterface(sym);

                if (auto pos = sym->name.rfind("::"); pos != std::string::npos)
                {
                    auto shortName = sym->name.substr(pos + 2);
                    auto alias = std::make_shared<InterfaceSymbol>(shortName);
                    alias->methods = sym->methods;
                    alias->genericParams = sym->genericParams;
                    scope.defineInterfaceAlias(shortName, alias);
                }
            }
        }

        bool hasGenericSources = _metadata.contains("genericSources") && !_metadata["genericSources"].empty();

        if (_metadata.contains("structs"))
        {
            for (const auto& j : _metadata["structs"])
            {
                auto sym = deserializeStruct(j);
                sym->isFromLibrary = true;
                if (hasGenericSources && sym->isGeneric()) continue;
                scope.defineStruct(sym);

                if (auto pos = sym->name.rfind("::"); pos != std::string::npos)
                {
                    auto shortName = sym->name.substr(pos + 2);
                    if (!scope.isDefinedLocally(shortName))
                        scope.defineAlias(shortName, sym);
                }
            }
        }

        if (_metadata.contains("enums"))
        {
            for (const auto& j : _metadata["enums"])
            {
                auto sym = deserializeEnum(j);
                sym->isFromLibrary = true;
                if (hasGenericSources && sym->isGeneric()) continue;
                scope.defineEnum(sym);

                if (auto pos = sym->name.rfind("::"); pos != std::string::npos)
                {
                    auto shortName = sym->name.substr(pos + 2);
                    if (!scope.isDefinedLocally(shortName))
                        scope.defineAlias(shortName, sym);
                }
            }
        }

        if (_metadata.contains("functions"))
        {
            for (const auto& j : _metadata["functions"])
            {
                auto sym = deserializeFunction(j);
                sym->isFromLibrary = true;
                scope.defineFunction(sym);

                if (auto pos = sym->name.rfind("::"); pos != std::string::npos)
                {
                    auto shortName = sym->name.substr(pos + 2);
                    if (!scope.isDefinedLocally(shortName))
                        scope.defineAlias(shortName, sym);
                }
            }
        }

        if (_metadata.contains("externFunctions"))
        {
            for (const auto& j : _metadata["externFunctions"])
            {
                auto sym = deserializeExternFunction(j);
                sym->isFromLibrary = true;
                scope.defineExternFunction(sym);
            }
        }
    }

    void DjLibReader::populateMacros(Program& targetProgram) const
    {
        if (!_valid || !_metadata.contains("macros")) return;

        for (const auto& j : _metadata["macros"])
        {
            auto name = j.at("name").get<std::string>();
            auto macro = std::make_unique<MacroDeclaration>(SourceIdentifier(name));

            for (const auto& rj : j.at("rules"))
            {
                MacroRule rule = rj.get<MacroRule>();
                macro->rules.push_back(std::move(rule));
            }

            targetProgram.macros.push_back(std::move(macro));
        }
    }

    void DjLibReader::populateAttributeTargets(std::unordered_map<std::string, int32_t>& targets) const
    {
        if (!_valid || !_metadata.contains("attributeDecls")) return;

        for (const auto& j : _metadata["attributeDecls"])
        {
            auto name = j.at("name").get<std::string>();
            int32_t targetMask = 0;

            if (j.contains("targetArgs"))
            {
                for (const auto& arg : j["targetArgs"])
                {
                    if (arg.contains("type") && arg["type"] == "string")
                    {
                        auto val = arg.at("value").get<std::string>();
                        if (val.find("Function") != std::string::npos) targetMask |= (1 << 0);
                        else if (val.find("Method") != std::string::npos) targetMask |= (1 << 1);
                        else if (val.find("Struct") != std::string::npos) targetMask |= (1 << 2);
                        else if (val.find("Field") != std::string::npos) targetMask |= (1 << 3);
                        else if (val.find("Parameter") != std::string::npos) targetMask |= (1 << 4);
                        else if (val.find("ReturnValue") != std::string::npos) targetMask |= (1 << 5);
                        else if (val.find("Variable") != std::string::npos) targetMask |= (1 << 6);
                        else if (val.find("All") != std::string::npos) targetMask = 127;
                    }
                }
            }

            if (targetMask == 0) targetMask = 127;
            targets[name] = targetMask;

            auto shortPos = name.rfind("::");
            if (shortPos != std::string::npos)
            {
                auto shortName = name.substr(shortPos + 2);
                targets[shortName] = targetMask;
            }
        }
    }

    void DjLibReader::populateConstExprs(ScopedSymbolTable& scope) const
    {
        if (!_valid || !_metadata.contains("constExprs")) return;

        for (const auto& j : _metadata["constExprs"])
        {
            auto name = j.at("name").get<std::string>();
            auto type = j.at("type").get<Type>();
            scope.constExprConstants[name] = {type, nullptr};

            auto sym = std::make_shared<Symbol>(SymbolKind::Variable, name, type);
            scope.define(sym);

            if (auto pos = name.rfind("::"); pos != std::string::npos)
            {
                auto shortName = name.substr(pos + 2);
                scope.constExprConstants[shortName] = {type, nullptr};
                auto shortSym = std::make_shared<Symbol>(SymbolKind::Variable, shortName, type);
                scope.define(shortSym);
            }
        }
    }

    void DjLibReader::populateStaticVars(ScopedSymbolTable& scope) const
    {
        if (!_valid || !_metadata.contains("staticVars")) return;

        for (const auto& j : _metadata["staticVars"])
        {
            auto name = j.at("name").get<std::string>();
            auto type = j.at("type").get<Type>();
            bool mut = j.value("mut", false);

            // Only define Symbol for binder lookup — do NOT add to staticVars map
            // The generator would create duplicate GlobalVariable conflicting with bitcode
            auto sym = std::make_shared<Symbol>(SymbolKind::Variable, name, type);
            sym->isMutable = mut;
            sym->isFromLibrary = true;
            scope.define(sym);

            if (auto pos = name.rfind("::"); pos != std::string::npos)
            {
                auto shortName = name.substr(pos + 2);
                auto shortSym = std::make_shared<Symbol>(SymbolKind::Variable, shortName, type);
                shortSym->isMutable = mut;
                shortSym->isFromLibrary = true;
                scope.define(shortSym);
            }
        }
    }
}
