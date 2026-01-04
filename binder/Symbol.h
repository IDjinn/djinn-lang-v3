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
    StructField,
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
    bool isMutable = false;

    Symbol(const SymbolKind kind, std::string name, Type type, const SourceLocation loc = {},
           const bool is_mutable = false)
        : kind(kind), name(std::move(name)), type(std::move(type)), location(loc), isMutable(is_mutable) {
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

struct FieldSymbol : Symbol {
    FieldSymbol(const SymbolKind kind, const std::string &name, const Type &type, const SourceLocation &loc,
                const bool is_mutable)
        : Symbol(kind, name, type, loc, is_mutable) {
    }
};

struct StructSymbol : Symbol {
    std::vector<FieldSymbol> fields;
    std::vector<std::string> genericParams;

    bool isGeneric() const {
        return this->genericParams.size() > 0;
    }

    explicit StructSymbol(std::string name, const SourceLocation loc = {})
        : Symbol(SymbolKind::Struct, std::move(name), Type::voided(), loc) {
    }

    void addField(const std::string &fieldName, const Type &fieldType, const bool isMutable = false) {
        fields.push_back({SymbolKind::Struct, fieldName, fieldType, {}, isMutable});
    }

    void addGenericParam(const std::string &param) {
        genericParams.push_back(param);
    }

    [[nodiscard]] bool hasField(const std::string &name) const {
        for (const auto &field: fields) {
            if (field.name == name) return true;
        }
        return false;
    }

    [[nodiscard]] const Type *getFieldType(const std::string &name) const {
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == name) return &fields[i].type;
        }
        return nullptr;
    }

    [[nodiscard]] int getFieldIndex(const std::string &name) const {
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }
};

#endif //DJINN_SYMBOL_H
