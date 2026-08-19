//
// Created by Claude on 04/01/2026.
//

#ifndef DJINN_SYMBOL_H
#define DJINN_SYMBOL_H

#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <variant>
#include <optional>
#include "../parser/ast/Type.h"
#include "../parser/ast/Statement.h"
#include "../parser/ast/Expression.h"
#include "../parser/ast/Declaration.h"
#include "../diagnostics/Diagnostic.h"

struct GenericParamInfo
{
    std::string name;
    std::vector<std::string> constraints;

    GenericParamInfo() = default;

    explicit GenericParamInfo(std::string name)
        : name(std::move(name))
    {
    }

    GenericParamInfo(std::string name, std::vector<std::string> constraints)
        : name(std::move(name)), constraints(std::move(constraints))
    {
    }
};

struct AttributeSymbol
{
    std::string name;
    std::vector<AttributeArg> args;

    AttributeSymbol() = default;

    explicit AttributeSymbol(std::string name) : name(std::move(name))
    {
    }

    AttributeSymbol(std::string name, std::vector<AttributeArg> args)
        : name(std::move(name)), args(std::move(args))
    {
    }

    [[nodiscard]] bool hasArgs() const { return !args.empty(); }

    [[nodiscard]] std::optional<int64_t> getInt(size_t index) const
    {
        if (index >= args.size()) return std::nullopt;
        if (auto* v = std::get_if<int64_t>(&args[index].value)) return *v;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> getString(size_t index) const
    {
        if (index >= args.size()) return std::nullopt;
        if (auto* v = std::get_if<std::string>(&args[index].value)) return *v;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<int64_t> getNamedInt(const std::string& argName) const
    {
        for (const auto& arg : args)
        {
            if (arg.name && *arg.name == argName)
            {
                if (auto* v = std::get_if<int64_t>(&arg.value)) return *v;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> getNamedString(const std::string& argName) const
    {
        for (const auto& arg : args)
        {
            if (arg.name && *arg.name == argName)
            {
                if (auto* v = std::get_if<std::string>(&arg.value)) return *v;
            }
        }
        return std::nullopt;
    }
};

enum class SymbolKind : uint8_t
{
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
    Enum,
    EnumConstruction,
    InstrisicCall,
    FunctionCall,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    BinaryExpression,
    UnaryExpression
};

struct Symbol
{
    SymbolKind kind;
    std::string name;
    Type type;
    SourceLocation location;
    bool isInitialized = false;
    bool isUsed = false;
    bool isMutable = false;
    bool isFromLibrary = false;

    Symbol(const SymbolKind kind, std::string name, Type type, const SourceLocation loc = {},
           const bool is_mutable = false)
        : kind(kind), name(std::move(name)), type(std::move(type)), location(loc), isMutable(is_mutable)
    {
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

    [[nodiscard]] static std::string kindToString(const SymbolKind k)
    {
        switch (k)
        {
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


struct FunctionSymbol : Symbol
{
    Type returnType;
    std::vector<Type> paramTypes;
    std::vector<std::string> paramNames;
    std::vector<bool> paramMutable;
    std::vector<std::vector<AttributeSymbol>> paramAttributes;
    bool isVariadic = false;
    bool isAsync = false;
    std::unique_ptr<Block> body;
    bool constEval;
    bool constExpr;
    bool throwsAny = false;
    std::vector<Type> throwsTypes;

    FunctionSymbol(std::string name, Type retType, const SourceLocation& loc = {})
        : Symbol(SymbolKind::Function, std::move(name), retType, loc),
          returnType(std::move(retType))
    {
    }

    void addParameter(const std::string& paramName, const Type& paramType, bool paramIsMutable = false,
                      std::vector<AttributeSymbol> attrs = {})
    {
        paramNames.push_back(paramName);
        paramTypes.push_back(paramType);
        paramMutable.push_back(paramIsMutable);
        paramAttributes.push_back(std::move(attrs));
    }

    void setBody(std::unique_ptr<Block> b)
    {
        body = std::move(b);
    }

    [[nodiscard]] bool hasBody() const { return body != nullptr; }
    [[nodiscard]] size_t arity() const { return paramTypes.size(); }
    [[nodiscard]] bool isThrowing() const { return throwsAny || !throwsTypes.empty(); }

    [[nodiscard]] size_t callerArity() const
    {
        size_t count = 0;
        for (size_t i = 0; i < paramAttributes.size(); i++)
        {
            bool transparent = false;
            for (const auto& a : paramAttributes[i])
                if (a.name == "Location") transparent = true;
            if (!transparent) count++;
        }
        return count;
    }

    [[nodiscard]] bool paramHasAttribute(size_t idx, const std::string& attr) const
    {
        if (idx >= paramAttributes.size()) return false;
        for (const auto& a : paramAttributes[idx])
            if (a.name == attr) return true;
        return false;
    }
};

struct ExternFunctionSymbol : FunctionSymbol
{
    std::string abi = "C";

    ExternFunctionSymbol(std::string name, const Type& retType, const SourceLocation& loc = {})
        : FunctionSymbol(std::move(name), retType, loc)
    {
        kind = SymbolKind::ExternFunction;
    }
};

struct FieldSymbol : Symbol
{
    bool isConstant = false;
    Expression* initializer = nullptr; // raw pointer, AST owns the memory

    FieldSymbol()
        : Symbol(SymbolKind::Field, "", Type::voided())
    {
    }

    FieldSymbol(const SymbolKind kind, const std::string& name, const Type& type, const SourceLocation& loc,
                const bool is_mutable, const bool is_constant = false, Expression* init = nullptr)
        : Symbol(kind, name, type, loc, is_mutable), isConstant(is_constant), initializer(init)
    {
    }
};

struct MethodSymbol : Symbol
{
    Type returnType;
    std::vector<Type> paramTypes;
    std::vector<std::string> paramNames;
    std::vector<std::vector<AttributeSymbol>> paramAttributes;
    std::vector<GenericParamInfo> genericParams;
    std::string variadicName;
    bool isAbstract = false;
    bool isStatic = false;
    bool isConstructor = false;
    bool isAsync = false;
    bool isOperator = false;
    std::string operatorCanonicalName;
    std::string structName;
    std::vector<AttributeSymbol> attributes;
    bool throwsAny = false;
    std::vector<Type> throwsTypes;

    Block* body = nullptr;
    Expression* expressionBody = nullptr;

    MethodSymbol(std::string name, Type retType, const SourceLocation loc = {})
        : Symbol(SymbolKind::Method, std::move(name), Type::voided(), loc),
          returnType(std::move(retType))
    {
    }

    void addParameter(const std::string& paramName, const Type& paramType,
                      std::vector<AttributeSymbol> attrs = {})
    {
        paramNames.push_back(paramName);
        paramTypes.push_back(paramType);
        paramAttributes.push_back(std::move(attrs));
    }

    void addGenericParam(const std::string& param)
    {
        genericParams.emplace_back(param);
    }

    void addGenericParam(const std::string& param, const std::vector<std::string>& constraints)
    {
        genericParams.emplace_back(param, constraints);
    }

    [[nodiscard]] bool isVariadic() const { return this->variadicName.length() > 0; }
    [[nodiscard]] bool hasBody() const { return body != nullptr || expressionBody != nullptr; }
    [[nodiscard]] bool isExpressionBody() const { return expressionBody != nullptr; }
    [[nodiscard]] size_t arity() const { return paramTypes.size(); }
    [[nodiscard]] bool isThrowing() const { return throwsAny || !throwsTypes.empty(); }

    [[nodiscard]] size_t callerArity() const
    {
        size_t count = 0;
        for (size_t i = 0; i < paramAttributes.size(); i++)
        {
            bool transparent = false;
            for (const auto& a : paramAttributes[i])
                if (a.name == "Location") transparent = true;
            if (!transparent) count++;
        }
        return count;
    }

    [[nodiscard]] bool paramHasAttribute(size_t idx, const std::string& attr) const
    {
        if (idx >= paramAttributes.size()) return false;
        for (const auto& a : paramAttributes[idx])
            if (a.name == attr) return true;
        return false;
    }

    [[nodiscard]] bool hasAttribute(const std::string& attr) const
    {
        for (const auto& a : attributes) { if (a.name == attr) return true; }
        return false;
    }

    [[nodiscard]] const AttributeSymbol* getAttribute(const std::string& attr) const
    {
        for (const auto& a : attributes) { if (a.name == attr) return &a; }
        return nullptr;
    }
};

struct PropertySymbol
{
    std::string name;
    Type type;
    bool hasGetter = false;
    bool hasSetter = false;

    std::unique_ptr<Block> getterBody;
    std::unique_ptr<Expression> getterExpr;

    std::unique_ptr<Block> setterBody;
    std::unique_ptr<Expression> setterExpr;

    PropertySymbol(std::string n, Type t, bool getter, bool setter)
        : name(std::move(n)), type(std::move(t)), hasGetter(getter), hasSetter(setter)
    {
    }

    [[nodiscard]] bool isAutoProperty() const
    {
        return (hasGetter && !getterBody && !getterExpr) ||
            (hasSetter && !setterBody && !setterExpr);
    }

    [[nodiscard]] bool needsBackingField() const
    {
        return isAutoProperty();
    }

    [[nodiscard]] std::string backingFieldName() const
    {
        return "_" + name;
    }
};

struct StructSymbol : Symbol
{
    std::vector<FieldSymbol> fields;
    std::vector<std::shared_ptr<MethodSymbol>> methods;
    std::vector<std::shared_ptr<PropertySymbol>> properties;
    std::vector<GenericParamInfo> genericParams;
    std::vector<std::string> implements;
    std::vector<AttributeSymbol> attributes;
    std::unique_ptr<Type> baseType;

    [[nodiscard]] bool isGeneric() const
    {
        return !this->genericParams.empty();
    }

    [[nodiscard]] bool isTransparent() const
    {
        return baseType != nullptr && fields.empty() && baseType->kind != TypeKind::STRUCT;
    }

    [[nodiscard]] bool hasBaseType() const
    {
        return baseType != nullptr;
    }

    [[nodiscard]] bool hasAttribute(const std::string& attr) const
    {
        for (const auto& a : attributes) { if (a.name == attr) return true; }
        return false;
    }

    [[nodiscard]] const AttributeSymbol* getAttribute(const std::string& attr) const
    {
        for (const auto& a : attributes) { if (a.name == attr) return &a; }
        return nullptr;
    }

    std::optional<FieldSymbol> findField(const std::string& fieldName)
    {
        const auto it = std::ranges::
            find_if(fields, [&](const FieldSymbol& f) { return f.name == fieldName; });

        if (it != fields.end())
            return *it;

        return std::nullopt;
    }

    explicit StructSymbol(std::string name, const SourceLocation loc = {})
        : Symbol(SymbolKind::Struct, std::move(name), Type::voided(), loc)
    {
    }

    void addField(const std::string& fieldName, const Type& fieldType, const bool fieldIsMutable = false,
                  const bool fieldIsConstant = false, Expression* fieldInitializer = nullptr,
                  const SourceLocation& loc = {})
    {
        fields.push_back({
            SymbolKind::Struct, fieldName, fieldType, loc, fieldIsMutable, fieldIsConstant, fieldInitializer
        });
    }

    void addMethod(std::shared_ptr<MethodSymbol> method)
    {
        methods.push_back(std::move(method));
    }

    void addGenericParam(const std::string& param)
    {
        genericParams.emplace_back(param);
    }

    void addGenericParam(const std::string& param, const std::vector<std::string>& constraints)
    {
        genericParams.emplace_back(param, constraints);
    }

    void addImplements(const std::string& interfaceName)
    {
        implements.push_back(interfaceName);
    }

    [[nodiscard]] bool hasConstraint(const std::string& constraint) const
    {
        return std::ranges::find(implements, constraint) != implements.end();
    }

    [[nodiscard]] bool hasField(const std::string& memberName) const
    {
        for (const auto& field : fields)
        {
            if (field.name == memberName) return true;
        }
        return false;
    }

    void addProperty(const std::string& propName, const Type& propType, bool hasGetter, bool hasSetter)
    {
        properties.push_back(std::make_shared<PropertySymbol>(propName, propType, hasGetter, hasSetter));
    }

    [[nodiscard]] bool hasProperty(const std::string& memberName) const
    {
        for (const auto& prop : properties)
        {
            if (prop->name == memberName) return true;
        }
        return false;
    }

    [[nodiscard]] const Type* getPropertyType(const std::string& memberName) const
    {
        for (const auto& prop : properties)
        {
            if (prop->name == memberName) return &prop->type;
        }
        return nullptr;
    }

    [[nodiscard]] bool hasMember(const std::string& memberName) const
    {
        return hasField(memberName) || hasProperty(memberName);
    }

    [[nodiscard]] const Type* getMemberType(const std::string& memberName) const
    {
        if (const Type* t = getFieldType(memberName)) return t;
        return getPropertyType(memberName);
    }

    [[nodiscard]] bool hasMethod(const std::string& memberName) const
    {
        for (const auto& method : methods)
        {
            if (method->name == memberName) return true;
        }
        return false;
    }

    [[nodiscard]] std::shared_ptr<MethodSymbol> getMethod(const std::string& memberName) const
    {
        for (const auto& method : methods)
        {
            if (method->name == memberName) return method;
        }
        return nullptr;
    }

    [[nodiscard]] const Type* getFieldType(const std::string& memberName) const
    {
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (fields[i].name == memberName) return &fields[i].type;
        }
        return nullptr;
    }

    [[nodiscard]] int getFieldIndex(const std::string& memberName) const
    {
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (fields[i].name == memberName) return static_cast<int>(i);
        }
        return -1;
    }
};

struct InterfaceSymbol : Symbol
{
    std::vector<std::shared_ptr<MethodSymbol>> methods;
    std::vector<GenericParamInfo> genericParams;

    [[nodiscard]] bool isGeneric() const
    {
        return !this->genericParams.empty();
    }

    explicit InterfaceSymbol(std::string name, const SourceLocation loc = {})
        : Symbol(SymbolKind::Interface, std::move(name), Type::voided(), loc)
    {
    }

    void addMethod(std::shared_ptr<MethodSymbol> method)
    {
        methods.push_back(std::move(method));
    }

    void addGenericParam(const std::string& param)
    {
        genericParams.emplace_back(param);
    }

    void addGenericParam(const std::string& param, const std::vector<std::string>& constraints)
    {
        genericParams.emplace_back(param, constraints);
    }

    [[nodiscard]] bool hasMethod(const std::string& methodName) const
    {
        for (const auto& method : methods)
        {
            if (method->name == methodName) return true;
        }
        return false;
    }

    [[nodiscard]] std::shared_ptr<MethodSymbol> getMethod(const std::string& methodName) const
    {
        for (const auto& method : methods)
        {
            if (method->name == methodName) return method;
        }
        return nullptr;
    }
};

// Ok(T, U) -> name="Ok", associatedTypes=[T, U]
struct EnumVariant
{
    std::string name;
    std::vector<Type> associatedTypes;
    unsigned tag = 0; // discriminant value

    EnumVariant() = default;

    EnumVariant(std::string name, std::vector<Type> types, unsigned tag = 0)
        : name(std::move(name)), associatedTypes(std::move(types)), tag(tag)
    {
    }

    [[nodiscard]] bool hasAssociatedTypes() const { return !associatedTypes.empty(); }
};

struct EnumSymbol : Symbol
{
    std::vector<EnumVariant> variants;
    std::vector<GenericParamInfo> genericParams;

    explicit EnumSymbol(std::string name, const SourceLocation loc = {})
        : Symbol(SymbolKind::Enum, std::move(name), Type::voided(), loc)
    {
    }

    [[nodiscard]] bool isGeneric() const
    {
        return !genericParams.empty();
    }

    void addVariant(const std::string& variantName, const std::vector<Type>& types)
    {
        variants.emplace_back(variantName, types, static_cast<unsigned>(variants.size()));
    }

    void addGenericParam(const std::string& param)
    {
        genericParams.emplace_back(param);
    }

    void addGenericParam(const std::string& param, const std::vector<std::string>& constraints)
    {
        genericParams.emplace_back(param, constraints);
    }

    [[nodiscard]] bool hasVariant(const std::string& variantName) const
    {
        for (const auto& v : variants)
        {
            if (v.name == variantName) return true;
        }
        return false;
    }

    [[nodiscard]] const EnumVariant* getVariant(const std::string& variantName) const
    {
        for (const auto& v : variants)
        {
            if (v.name == variantName) return &v;
        }
        return nullptr;
    }

    [[nodiscard]] unsigned getVariantTag(const std::string& variantName) const
    {
        for (const auto& v : variants)
        {
            if (v.name == variantName) return v.tag;
        }
        return UINT_MAX;
    }
};

struct IntrinsicCallSymbol : Symbol
{
    std::shared_ptr<Symbol> returnType;
    std::shared_ptr<Symbol> intrinsic;
    std::vector<std::shared_ptr<Symbol>> arguments;

    explicit IntrinsicCallSymbol(std::string name, const std::shared_ptr<Symbol>& returnType,
                                 const SourceLocation loc = {})
        : Symbol(SymbolKind::InstrisicCall, std::move(name), Type::voided(), loc), returnType(returnType)
    {
    }
};

struct EnumConstructionSymbol : Symbol
{
    std::string enumName;
    std::string variantName;
    unsigned variantTag;
    std::vector<std::shared_ptr<Symbol>> arguments;

    EnumConstructionSymbol(std::string enumName, std::string variantName, unsigned tag,
                           std::vector<std::shared_ptr<Symbol>> args, const SourceLocation loc = {})
        : Symbol(SymbolKind::EnumConstruction, enumName + "::" + variantName, Type::voided(), loc),
          enumName(std::move(enumName)), variantName(std::move(variantName)),
          variantTag(tag), arguments(std::move(args))
    {
    }
};

struct FunctionCallSymbol : Symbol
{
    std::shared_ptr<Symbol> function;
    std::shared_ptr<Symbol> receiver; // for method calls
    std::vector<std::shared_ptr<Symbol>> arguments;
    bool isMethodCall = false;

    FunctionCallSymbol(std::string name, std::shared_ptr<Symbol> func,
                       std::vector<std::shared_ptr<Symbol>> args, const SourceLocation loc = {})
        : Symbol(SymbolKind::FunctionCall, std::move(name), func ? func->type : Type::voided(), loc),
          function(std::move(func)), arguments(std::move(args))
    {
    }

    FunctionCallSymbol(std::string name, std::shared_ptr<Symbol> recv, std::shared_ptr<Symbol> func,
                       std::vector<std::shared_ptr<Symbol>> args, const SourceLocation loc = {})
        : Symbol(SymbolKind::FunctionCall, std::move(name), func ? func->type : Type::voided(), loc),
          function(std::move(func)), receiver(std::move(recv)), arguments(std::move(args)), isMethodCall(true)
    {
    }
};

struct IntegerLiteralSymbol : Symbol
{
    std::string value;
    bool isSigned;

    IntegerLiteralSymbol(std::string val, bool sign = true, const SourceLocation loc = {})
        : Symbol(SymbolKind::IntegerLiteral, val, Type::integer(32, sign), loc),
          value(std::move(val)), isSigned(sign)
    {
    }

    [[nodiscard]] int64_t asInt64() const { return std::stoll(value); }
};

struct FloatLiteralSymbol : Symbol
{
    std::string value;

    explicit FloatLiteralSymbol(std::string val, size_t size, const SourceLocation loc = {})
        : Symbol(SymbolKind::FloatLiteral, val, Type::floating(size), loc),
          value(std::move(val))
    {
    }

    [[nodiscard]] double asDouble() const { return std::stod(value); }
};

struct StringLiteralSymbol : Symbol
{
    std::string value;

    explicit StringLiteralSymbol(std::string val, const SourceLocation loc = {})
        : Symbol(SymbolKind::StringLiteral, val, Type::struct_type("str"), loc),
          value(std::move(val))
    {
    }
};

struct BinaryExpressionSymbol : Symbol
{
    std::string op;
    std::shared_ptr<Symbol> left;
    std::shared_ptr<Symbol> right;

    BinaryExpressionSymbol(std::string operation, std::shared_ptr<Symbol> lhs,
                           std::shared_ptr<Symbol> rhs, Type resultType, const SourceLocation loc = {})
        : Symbol(SymbolKind::BinaryExpression, operation, std::move(resultType), loc),
          op(std::move(operation)), left(std::move(lhs)), right(std::move(rhs))
    {
    }
};

struct UnaryExpressionSymbol : Symbol
{
    std::string op;
    std::shared_ptr<Symbol> operand;

    UnaryExpressionSymbol(std::string operation, std::shared_ptr<Symbol> expr,
                          Type resultType, const SourceLocation loc = {})
        : Symbol(SymbolKind::UnaryExpression, operation, std::move(resultType), loc),
          op(std::move(operation)), operand(std::move(expr))
    {
    }
};

#endif //DJINN_SYMBOL_H