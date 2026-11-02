#include "DjLibWriter.h"
#include "DjLibFormat.h"
#include "../utils/Logger.h"

#include <llvm/IR/Module.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Support/raw_ostream.h>

#include <fstream>
#include <cstring>
#include <set>

namespace djlib
{
    void DjLibWriter::collectSymbols(const ScopedSymbolTable& symbols)
    {
        for (const auto& sym : symbols.get_all_structs())
        {
            auto s = std::dynamic_pointer_cast<StructSymbol>(sym);
            if (!s) continue;
            _structs.push_back(serializeStruct(*s));
        }

        for (const auto& sym : symbols.get_all_functions())
        {
            auto f = std::dynamic_pointer_cast<FunctionSymbol>(sym);
            if (!f) continue;
            _functions.push_back(serializeFunction(*f));
        }

        for (const auto& sym : symbols.get_all_extern_functions())
        {
            auto f = std::dynamic_pointer_cast<ExternFunctionSymbol>(sym);
            if (!f) continue;
            _externFunctions.push_back(serializeExternFunction(*f));
        }

        for (const auto& sym : symbols.get_all_enums())
        {
            auto e = std::dynamic_pointer_cast<EnumSymbol>(sym);
            if (!e) continue;
            _enums.push_back(serializeEnum(*e));
        }

        for (const auto& sym : symbols.get_all_interfaces())
        {
            auto i = std::dynamic_pointer_cast<InterfaceSymbol>(sym);
            if (!i) continue;
            _interfaces.push_back(serializeInterface(*i));
        }
    }

    void DjLibWriter::collectMacros(const std::vector<std::shared_ptr<Program>>& programs)
    {
        for (const auto& program : programs)
        {
            const std::string prefix = program->getNamespacePrefix();
            for (const auto& macro : program->macros)
            {
                nlohmann::json j;
                j["name"] = prefix + macro->name.token_name;

                nlohmann::json rules = nlohmann::json::array();
                for (const auto& rule : macro->rules)
                    rules.push_back(rule);
                j["rules"] = std::move(rules);

                _macros.push_back(std::move(j));
            }
        }
    }

    void DjLibWriter::collectAttributeDecls(const std::vector<std::shared_ptr<Program>>& programs)
    {
        for (const auto& program : programs)
        {
            const std::string prefix = program->getNamespacePrefix();
            for (const auto& attrDecl : program->attributeDecls)
            {
                nlohmann::json j;
                j["name"] = prefix + attrDecl->name.token_name;
                j["targetArgs"] = attrDecl->targetArgs;

                if (!attrDecl->fields.empty())
                {
                    nlohmann::json fields = nlohmann::json::array();
                    for (const auto& f : attrDecl->fields)
                    {
                        nlohmann::json fj;
                        fj["name"] = f.name.token_name;
                        fj["type"] = *f.type;
                        fields.push_back(std::move(fj));
                    }
                    j["fields"] = std::move(fields);
                }

                _attributeDecls.push_back(std::move(j));
            }
        }
    }

    void DjLibWriter::collectImplBlocks(const std::vector<std::shared_ptr<Program>>& programs)
    {
        for (const auto& program : programs)
        {
            const std::string prefix = program->getNamespacePrefix();
            for (const auto& impl : program->impls)
            {
                nlohmann::json j;

                nlohmann::json targets = nlohmann::json::array();
                for (const auto& t : impl->targetTypes)
                    targets.push_back(t);
                j["targetTypes"] = std::move(targets);

                if (!impl->interfaceNames.empty())
                    j["interfaces"] = impl->interfaceNames;

                nlohmann::json methods = nlohmann::json::array();
                for (const auto& m : impl->methods)
                {
                    nlohmann::json mj;
                    mj["name"] = m->name.token_name;
                    mj["returnType"] = *m->returnType;

                    nlohmann::json params = nlohmann::json::array();
                    for (const auto& p : m->parameters)
                    {
                        nlohmann::json pj;
                        pj["name"] = p.name.token_name;
                        pj["type"] = *p.type;
                        if (p.isMutable) pj["mut"] = true;
                        params.push_back(std::move(pj));
                    }
                    mj["params"] = std::move(params);

                    bool isStatic = false;
                    bool isOperator = false;
                    for (auto mod : m->modifiers)
                    {
                        if (mod == VisibilityModifier::STATIC) isStatic = true;
                    }
                    if (m->isOperatorMethod) isOperator = true;

                    if (isStatic) mj["static"] = true;
                    if (isOperator)
                    {
                        mj["operator"] = true;
                        mj["operatorName"] = m->operatorCanonicalName;
                    }
                    if (m->isConstructorMethod) mj["constructor"] = true;
                    if (m->isAsync) mj["async"] = true;

                    methods.push_back(std::move(mj));
                }
                j["methods"] = std::move(methods);

                _implBlocks.push_back(std::move(j));
            }
        }
    }

    void DjLibWriter::collectConstExprs(const ScopedSymbolTable& symbols)
    {
        for (const auto& [name, entry] : symbols.constExprConstants)
        {
            nlohmann::json j;
            j["name"] = name;
            j["type"] = entry.type;
            _constExprs.push_back(std::move(j));
        }
    }

    void DjLibWriter::collectStaticVars(const ScopedSymbolTable& symbols)
    {
        for (const auto& [name, entry] : symbols.staticVars)
        {
            nlohmann::json j;
            j["name"] = name;
            j["type"] = entry.type;
            if (entry.isMutable) j["mut"] = true;
            _staticVars.push_back(std::move(j));
        }
    }

    void DjLibWriter::collectGenericSources(const std::vector<std::shared_ptr<Program>>& programs,
                                            const std::unordered_map<std::string, SourceFile>& sources)
    {
        std::set<std::string> includedFiles;

        for (const auto& program : programs)
        {
            bool hasGenerics = false;
            for (const auto& s : program->structs)
                if (!s->genericParams.empty())
                {
                    hasGenerics = true;
                    break;
                }
            if (!hasGenerics)
                for (const auto& e : program->enums)
                    if (!e->genericParams.empty())
                    {
                        hasGenerics = true;
                        break;
                    }

            if (!hasGenerics) continue;
            if (includedFiles.contains(program->name)) continue;
            includedFiles.insert(program->name);

            auto it = sources.find(program->name);
            if (it == sources.end()) continue;

            nlohmann::json j;
            j["file"] = program->name;
            j["namespace"] = program->fileNamespace;
            j["source"] = it->second.source;
            _genericSources.push_back(std::move(j));
        }
    }

    nlohmann::json DjLibWriter::serializeMetadata() const
    {
        nlohmann::json meta;
        meta["version"] = VERSION;
        meta["structs"] = _structs;
        meta["functions"] = _functions;
        meta["externFunctions"] = _externFunctions;
        meta["enums"] = _enums;
        meta["interfaces"] = _interfaces;
        meta["macros"] = _macros;
        meta["attributeDecls"] = _attributeDecls;
        meta["implBlocks"] = _implBlocks;
        meta["constExprs"] = _constExprs;
        if (!_genericSources.empty()) meta["genericSources"] = _genericSources;
        meta["staticVars"] = _staticVars;
        return meta;
    }

    bool DjLibWriter::write(const std::filesystem::path& outputPath, llvm::Module& module) const
    {
        std::string metadataStr = serializeMetadata().dump();

        std::string bitcodeStr;
        llvm::raw_string_ostream bitcodeStream(bitcodeStr);
        llvm::WriteBitcodeToFile(module, bitcodeStream);
        bitcodeStream.flush();

        Header header{};
        std::memcpy(header.magic, MAGIC, sizeof(MAGIC));
        header.version = VERSION;
        header.flags = 0;
        header.metadataSize = static_cast<uint32_t>(metadataStr.size());
        header.bitcodeSize = static_cast<uint32_t>(bitcodeStr.size());

        std::ofstream out(outputPath, std::ios::binary);
        if (!out)
        {
            LOG_ERROR("Failed to open %s for writing", outputPath.string().c_str());
            return false;
        }

        out.write(reinterpret_cast<const char*>(&header), sizeof(Header));
        out.write(metadataStr.data(), static_cast<std::streamsize>(metadataStr.size()));
        out.write(bitcodeStr.data(), static_cast<std::streamsize>(bitcodeStr.size()));

        if (!out.good())
        {
            LOG_ERROR("Failed to write djlib to %s", outputPath.string().c_str());
            return false;
        }

        LOG_INFO("Written djlib: %s (metadata=%u bytes, bitcode=%u bytes)",
                 outputPath.string().c_str(), header.metadataSize, header.bitcodeSize);
        return true;
    }
}
