#ifndef DJINN_DJLIB_WRITER_H
#define DJINN_DJLIB_WRITER_H

#include <filesystem>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "../binder/SymbolTable.h"
#include "../parser/ast/Declaration.h"
#include "../diagnostics/Diagnostic.h"

namespace llvm { class Module; }

namespace djlib
{
    class DjLibWriter
    {
    public:
        void collectSymbols(const ScopedSymbolTable& symbols);
        void collectMacros(const std::vector<std::shared_ptr<Program>>& programs);
        void collectAttributeDecls(const std::vector<std::shared_ptr<Program>>& programs);
        void collectImplBlocks(const std::vector<std::shared_ptr<Program>>& programs);
        void collectConstExprs(const ScopedSymbolTable& symbols);
        void collectStaticVars(const ScopedSymbolTable& symbols);
        void collectGenericSources(const std::vector<std::shared_ptr<Program>>& programs,
                                   const std::unordered_map<std::string, SourceFile>& sources);

        [[nodiscard]] nlohmann::json serializeMetadata() const;

        bool write(const std::filesystem::path& outputPath, llvm::Module& module) const;

    private:
        nlohmann::json _structs = nlohmann::json::array();
        nlohmann::json _functions = nlohmann::json::array();
        nlohmann::json _externFunctions = nlohmann::json::array();
        nlohmann::json _enums = nlohmann::json::array();
        nlohmann::json _interfaces = nlohmann::json::array();
        nlohmann::json _macros = nlohmann::json::array();
        nlohmann::json _attributeDecls = nlohmann::json::array();
        nlohmann::json _implBlocks = nlohmann::json::array();
        nlohmann::json _constExprs = nlohmann::json::array();
        nlohmann::json _staticVars = nlohmann::json::array();
        nlohmann::json _genericSources = nlohmann::json::array();
    };
}

#endif //DJINN_DJLIB_WRITER_H
