#ifndef DJINN_DJLIB_READER_H
#define DJINN_DJLIB_READER_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../binder/SymbolTable.h"
#include "../parser/ast/Declaration.h"

namespace llvm { class MemoryBuffer; }

namespace djlib
{
    class DjLibReader
    {
    public:
        bool read(const std::filesystem::path& path);

        void populateSymbolTable(ScopedSymbolTable& scope) const;
        void populateMacros(Program& targetProgram) const;
        void populateAttributeTargets(std::unordered_map<std::string, int32_t>& targets) const;
        void populateConstExprs(ScopedSymbolTable& scope) const;
        void populateStaticVars(ScopedSymbolTable& scope) const;

        [[nodiscard]] const std::string& getBitcodeData() const { return _bitcodeData; }
        [[nodiscard]] const nlohmann::json& metadata() const { return _metadata; }
        [[nodiscard]] bool isValid() const { return _valid; }

    private:
        nlohmann::json _metadata;
        std::string _bitcodeData;
        bool _valid = false;
    };
}

#endif //DJINN_DJLIB_READER_H
