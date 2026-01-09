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
    Interface,
    Method,
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
    [[nodiscard]] bool isInterface() const { return kind == SymbolKind::Interface; }
    [[nodiscard]] bool isMethod() const { return kind == SymbolKind::Method; }
    [[nodiscard]] bool isField() const { return kind == SymbolKind::Field; }

    [[nodiscard]] static std::string kindToString(const SymbolKind k) {
        switch (k) {
            case SymbolKind::Variable: return "variable";
            case SymbolKind::Parameter: return "parameter";
            case SymbolKind::Function: return "function";
            case SymbolKind::ExternFunction: return "extern function";
            case SymbolKind::Struct: return "struct";
            case SymbolKind::Interface: return "interface";
            case SymbolKind::Method: return "method";
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

struct MethodSymbol : Symbol {
    Type returnType;
    std::vector<Type> paramTypes;
    std::vector<std::string> paramNames;
    std::vector<std::string> genericParams;
    bool isAbstract = false;

    MethodSymbol(std::string name, Type retType, const SourceLocation loc = {})
        : Symbol(SymbolKind::Method, std::move(name), Type::voided(), loc),
          returnType(std::move(retType)) {
    }

    void addParameter(const std::string &paramName, const Type &paramType) {
        paramNames.push_back(paramName);
        paramTypes.push_back(paramType);
    }

    void addGenericParam(const std::string &param) {
        genericParams.push_back(param);
    }

    [[nodiscard]] size_t arity() const { return paramTypes.size(); }
};

struct PropertySymbol {
    std::string name;
    Type type;
    bool hasGetter = false;
    bool hasSetter = false;

    PropertySymbol(std::string n, Type t, bool getter, bool setter)
        : name(std::move(n)), type(std::move(t)), hasGetter(getter), hasSetter(setter) {
    }
};

struct StructSymbol : Symbol {
    std::vector<FieldSymbol> fields;
    std::vector<std::shared_ptr<MethodSymbol> > methods;
    std::vector<PropertySymbol> properties;
    std::vector<std::string> genericParams;
    std::vector<std::string> implements;
    std::unique_ptr<Type> baseType; // for transparent types: struct Size : i32;

    [[nodiscard]] bool isGeneric() const {
        return !this->genericParams.empty();
    }

    // Transparent type: inherits from primitive and has no fields
    [[nodiscard]] bool isTransparent() const {
        return baseType != nullptr && fields.empty() && baseType->kind != TypeKind::STRUCT;
    }

    [[nodiscard]] bool hasBaseType() const {
        return baseType != nullptr;
    }

    explicit StructSymbol(std::string name, const SourceLocation loc = {})
        : Symbol(SymbolKind::Struct, std::move(name), Type::voided(), loc) {
    }

    void addField(const std::string &fieldName, const Type &fieldType, const bool isMutable = false) {
        fields.push_back({SymbolKind::Struct, fieldName, fieldType, {}, isMutable});
    }

    void addMethod(std::shared_ptr<MethodSymbol> method) {
        methods.push_back(std::move(method));
    }

    void addGenericParam(const std::string &param) {
        genericParams.push_back(param);
    }

    void addImplements(const std::string &interfaceName) {
        implements.push_back(interfaceName);
    }

    [[nodiscard]] bool hasField(const std::string &name) const {
        for (const auto &field: fields) {
            if (field.name == name) return true;
        }
        return false;
    }

    void addProperty(const std::string &propName, const Type &propType, bool hasGetter, bool hasSetter) {
        properties.emplace_back(propName, propType, hasGetter, hasSetter);
    }

    [[nodiscard]] bool hasProperty(const std::string &name) const {
        for (const auto &prop: properties) {
            if (prop.name == name) return true;
        }
        return false;
    }

    [[nodiscard]] const Type *getPropertyType(const std::string &name) const {
        for (const auto &prop: properties) {
            if (prop.name == name) return &prop.type;
        }
        return nullptr;
    }

    // Check if struct has field OR property
    [[nodiscard]] bool hasMember(const std::string &name) const {
        return hasField(name) || hasProperty(name);
    }

    // Get type of field or property
    [[nodiscard]] const Type *getMemberType(const std::string &name) const {
        if (const Type *t = getFieldType(name)) return t;
        return getPropertyType(name);
    }

    [[nodiscard]] bool hasMethod(const std::string &name) const {
        for (const auto &method: methods) {
            if (method->name == name) return true;
        }
        return false;
    }

    [[nodiscard]] std::shared_ptr<MethodSymbol> getMethod(const std::string &name) const {
        for (const auto &method: methods) {
            if (method->name == name) return method;
        }
        return nullptr;
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

struct InterfaceSymbol : Symbol {
    std::vector<std::shared_ptr<MethodSymbol> > methods;
    std::vector<std::string> genericParams;

    [[nodiscard]] bool isGeneric() const {
        return !this->genericParams.empty();
    }

    explicit InterfaceSymbol(std::string name, const SourceLocation loc = {})
        : Symbol(SymbolKind::Interface, std::move(name), Type::voided(), loc) {
    }

    void addMethod(std::shared_ptr<MethodSymbol> method) {
        methods.push_back(std::move(method));
    }

    void addGenericParam(const std::string &param) {
        genericParams.push_back(param);
    }

    [[nodiscard]] bool hasMethod(const std::string &name) const {
        for (const auto &method: methods) {
            if (method->name == name) return true;
        }
        return false;
    }

    [[nodiscard]] std::shared_ptr<MethodSymbol> getMethod(const std::string &name) const {
        for (const auto &method: methods) {
            if (method->name == name) return method;
        }
        return nullptr;
    }
};

#endif //DJINN_SYMBOL_H