//
// Created by Claude on 04/01/2026.
//

#ifndef DJINN_SYMBOL_H
#define DJINN_SYMBOL_H

#include <string>
#include <vector>
#include <memory>
#include "../parser/ast/Type.h"
#include "../parser/ast/Statement.h"
#include "../parser/ast/Expression.h"
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
    GenericParam,
    Enum
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
    [[nodiscard]] bool isEnum() const { return kind == SymbolKind::Enum; }

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
            case SymbolKind::Enum: return "enum";
            default: return "unknown";
        }
    }
};


struct FunctionSymbol : Symbol {
    Type returnType;
    std::vector<Type> paramTypes;
    std::vector<std::string> paramNames;
    std::vector<bool> paramMutable;
    bool isVariadic = false;
    std::unique_ptr<Block> body;

    FunctionSymbol(std::string name, Type retType, const SourceLocation loc = {})
        : Symbol(SymbolKind::Function, std::move(name), Type::voided(), loc),
          returnType(std::move(retType)) {
    }

    void addParameter(const std::string &paramName, const Type &paramType, bool isMutable = false) {
        paramNames.push_back(paramName);
        paramTypes.push_back(paramType);
        paramMutable.push_back(isMutable);
    }

    void setBody(std::unique_ptr<Block> b) {
        body = std::move(b);
    }

    [[nodiscard]] bool hasBody() const { return body != nullptr; }
    [[nodiscard]] size_t arity() const { return paramTypes.size(); }
};

struct ExternFunctionSymbol : Symbol {
    Type returnType;
    std::vector<Type> paramTypes;
    std::vector<std::string> paramNames;
    bool isVariadic = false;
    std::string abi = "C";

    ExternFunctionSymbol(std::string name, Type retType, const SourceLocation loc = {})
        : Symbol(SymbolKind::ExternFunction, std::move(name), Type::voided(), loc),
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
    bool isStatic = false;

    std::unique_ptr<Block> body;
    std::unique_ptr<Expression> expressionBody;

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

    void setBody(std::unique_ptr<Block> b) { body = std::move(b); }
    void setExpressionBody(std::unique_ptr<Expression> e) { expressionBody = std::move(e); }

    [[nodiscard]] bool hasBody() const { return body != nullptr || expressionBody != nullptr; }
    [[nodiscard]] bool isExpressionBody() const { return expressionBody != nullptr; }
    [[nodiscard]] size_t arity() const { return paramTypes.size(); }
};

struct PropertySymbol {
    std::string name;
    Type type;
    bool hasGetter = false;
    bool hasSetter = false;

    std::unique_ptr<Block> getterBody;
    std::unique_ptr<Expression> getterExpr;

    std::unique_ptr<Block> setterBody;
    std::unique_ptr<Expression> setterExpr;

    PropertySymbol(std::string n, Type t, bool getter, bool setter)
        : name(std::move(n)), type(std::move(t)), hasGetter(getter), hasSetter(setter) {
    }

    [[nodiscard]] bool isAutoProperty() const {
        return (hasGetter && !getterBody && !getterExpr) ||
               (hasSetter && !setterBody && !setterExpr);
    }

    [[nodiscard]] bool needsBackingField() const {
        return isAutoProperty();
    }

    [[nodiscard]] std::string backingFieldName() const {
        return "_" + name;
    }
};

struct StructSymbol : Symbol {
    std::vector<FieldSymbol> fields;
    std::vector<std::shared_ptr<MethodSymbol> > methods;
    std::vector<std::shared_ptr<PropertySymbol> > properties;
    std::vector<std::string> genericParams;
    std::vector<std::string> implements;
    std::unique_ptr<Type> baseType;

    [[nodiscard]] bool isGeneric() const {
        return !this->genericParams.empty();
    }

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
        properties.push_back(std::make_shared<PropertySymbol>(propName, propType, hasGetter, hasSetter));
    }

    [[nodiscard]] bool hasProperty(const std::string &name) const {
        for (const auto &prop: properties) {
            if (prop->name == name) return true;
        }
        return false;
    }

    [[nodiscard]] const Type *getPropertyType(const std::string &name) const {
        for (const auto &prop: properties) {
            if (prop->name == name) return &prop->type;
        }
        return nullptr;
    }

    [[nodiscard]] bool hasMember(const std::string &name) const {
        return hasField(name) || hasProperty(name);
    }

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

// Ok(T, U) -> name="Ok", associatedTypes=[T, U]
struct EnumVariant {
    std::string name;
    std::vector<Type> associatedTypes;
    unsigned tag = 0; // discriminant value

    EnumVariant(std::string name, std::vector<Type> types, unsigned tag = 0)
        : name(std::move(name)), associatedTypes(std::move(types)), tag(tag) {
    }

    [[nodiscard]] bool hasAssociatedTypes() const { return !associatedTypes.empty(); }
};

struct EnumSymbol : Symbol {
    std::vector<EnumVariant> variants;
    std::vector<std::string> genericParams;

    explicit EnumSymbol(std::string name, const SourceLocation loc = {})
        : Symbol(SymbolKind::Enum, std::move(name), Type::voided(), loc) {
    }

    [[nodiscard]] bool isGeneric() const {
        return !genericParams.empty();
    }

    void addVariant(const std::string &variantName, const std::vector<Type> &types) {
        variants.emplace_back(variantName, types, static_cast<unsigned>(variants.size()));
    }

    void addGenericParam(const std::string &param) {
        genericParams.push_back(param);
    }

    [[nodiscard]] bool hasVariant(const std::string &variantName) const {
        for (const auto &v: variants) {
            if (v.name == variantName) return true;
        }
        return false;
    }

    [[nodiscard]] const EnumVariant *getVariant(const std::string &variantName) const {
        for (const auto &v: variants) {
            if (v.name == variantName) return &v;
        }
        return nullptr;
    }

    [[nodiscard]] unsigned getVariantTag(const std::string &variantName) const {
        for (const auto &v: variants) {
            if (v.name == variantName) return v.tag;
        }
        return UINT_MAX;
    }
};

#endif //DJINN_SYMBOL_H