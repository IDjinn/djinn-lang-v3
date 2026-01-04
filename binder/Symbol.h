//
// Created by Claude on 04/01/2026.
//

#ifndef DJINN_SYMBOL_H
#define DJINN_SYMBOL_H

#include <string>
#include <vector>
#include <memory>
#include "../parser/ast/Type.h"
#include "../diagnostics/Diagnostic.h"

enum class SymbolKind : uint8_t {
    Variable,
    Parameter,
    Function,
    ExternFunction,
    Struct,
    Field,
    GenericParam
};

struct Symbol {
    SymbolKind kind;
    std::string name;
    Type type;
    SourceLocation location;
    bool isInitialized = false;
    bool isUsed = false;

    Symbol(const SymbolKind kind, std::string name, Type type, const SourceLocation loc = {})
        : kind(kind), name(std::move(name)), type(std::move(type)), location(loc) {
    }

    virtual ~Symbol() = default;

    [[nodiscard]] bool isVariable() const { return kind == SymbolKind::Variable; }
    [[nodiscard]] bool isParameter() const { return kind == SymbolKind::Parameter; }
    [[nodiscard]] bool isFunction() const { return kind == SymbolKind::Function || kind == SymbolKind::ExternFunction; }
    [[nodiscard]] bool isStruct() const { return kind == SymbolKind::Struct; }
    [[nodiscard]] bool isField() const { return kind == SymbolKind::Field; }

    [[nodiscard]] static std::string kindToString(const SymbolKind k) {
        switch (k) {
            case SymbolKind::Variable: return "variable";
            case SymbolKind::Parameter: return "parameter";
            case SymbolKind::Function: return "function";
            case SymbolKind::ExternFunction: return "extern function";
            case SymbolKind::Struct: return "struct";
            case SymbolKind::Field: return "field";
            case SymbolKind::GenericParam: return "generic parameter";
            default: return "unknown";
        }
    }
};

struct FunctionSymbol : Symbol {
    Type returnType;
    std::vector<Type> paramTypes;
    std::vector<std::string> paramNames;
    bool isVariadic = false;

    FunctionSymbol(std::string name, Type retType, const SourceLocation loc = {})
        : Symbol(SymbolKind::Function, std::move(name), Type::voided(), loc),
          returnType(std::move(retType)) {
    }

    void addParameter(const std::string &paramName, const Type &paramType) {
        paramNames.push_back(paramName);
        paramTypes.push_back(paramType);
    }

    [[nodiscard]] size_t arity() const { return paramTypes.size(); }
};

struct StructSymbol : Symbol {
    std::vector<std::string> fieldNames;
    std::vector<Type> fieldTypes;
    std::vector<std::string> genericParams;
    bool isGeneric = false;

    explicit StructSymbol(std::string name, const SourceLocation loc = {})
        : Symbol(SymbolKind::Struct, std::move(name), Type::voided(), loc) {
    }

    void addField(const std::string &fieldName, const Type &fieldType) {
        fieldNames.push_back(fieldName);
        fieldTypes.push_back(fieldType);
    }

    void addGenericParam(const std::string &param) {
        genericParams.push_back(param);
        isGeneric = true;
    }

    [[nodiscard]] bool hasField(const std::string &name) const {
        for (const auto &f: fieldNames) {
            if (f == name) return true;
        }
        return false;
    }

    [[nodiscard]] const Type *getFieldType(const std::string &name) const {
        for (size_t i = 0; i < fieldNames.size(); ++i) {
            if (fieldNames[i] == name) return &fieldTypes[i];
        }
        return nullptr;
    }

    [[nodiscard]] int getFieldIndex(const std::string &name) const {
        for (size_t i = 0; i < fieldNames.size(); ++i) {
            if (fieldNames[i] == name) return static_cast<int>(i);
        }
        return -1;
    }
};

#endif //DJINN_SYMBOL_H
