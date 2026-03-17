//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_DECLARATION_H
#define DJINN_DECLARATION_H

#include <string>
#include <vector>
#include <memory>
#include "ASTNode.h"
#include "Type.h"
#include "Statement.h"
#include "Generic.h"
#include "../../visitor/DeclarationVisitor.h"

struct EnumDeclaration;

struct AttributeUsageDeclaration : Location
{
    SourceIdentifier name;

    AttributeUsageDeclaration(SourceIdentifier name) : name(std::move(name))
    {
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "AttributeUsageDeclaration(" << name.token_name << ")";
    }
};

enum class VisibilityModifier
{
    PUBLIC,
    PRIVATE,
    STATIC,
};

struct MethodParameter
{
    std::unique_ptr<Type> type;
    SourceIdentifier name;
    bool isMutable = false;

    MethodParameter(std::unique_ptr<Type> type, SourceIdentifier name, bool isMutable = false)
        : type(std::move(type)), name(std::move(name)), isMutable(isMutable)
    {
    }
};

struct StructMethodDeclaration : Location
{
    std::unique_ptr<Type> returnType;
    SourceIdentifier name;
    std::vector<MethodParameter> parameters;
    GenericParams genericParams;
    std::vector<VisibilityModifier> modifiers;
    std::unique_ptr<Block> body;
    std::unique_ptr<Expression> expression;
    bool isConstructorMethod = false; // True if this is a constructor (name == struct name)
    bool isVariadic = false; // True if method has variadic parameters (...)
    bool isAsync = false; // True if this is an async method
    bool isOperatorMethod = false; // True if this is an operator overload
    std::string operatorCanonicalName; // e.g., "__op_add", "__op_eq", "__op_index_get"

    // Variadic forwarding: if this method forwards its variadic args to another function
    // e.g., "return printf(msg, ...)" -> variadicForwardTarget = "printf"
    std::string variadicForwardTarget;
    std::vector<AttributeUsageDeclaration> attributes;

    [[nodiscard]] bool isVariadicForwarder() const { return !variadicForwardTarget.empty(); }

    [[nodiscard]] bool hasAttribute(const std::string& attr) const
    {
        for (const auto& a : attributes)
        {
            if (a.name.token_name == attr) return true;
        }
        return false;
    }

    [[nodiscard]] bool isExpressionBody() const { return expression != nullptr; }
    [[nodiscard]] bool isAbstract() const { return body == nullptr && expression == nullptr; }
    [[nodiscard]] bool isConstructor() const { return isConstructorMethod; }

    [[nodiscard]] bool isStatic() const
    {
        for (const auto& mod : modifiers)
        {
            if (mod == VisibilityModifier::STATIC) return true;
        }
        return false;
    }

    [[nodiscard]] bool isPublic() const
    {
        for (const auto& mod : modifiers)
        {
            if (mod == VisibilityModifier::PUBLIC) return true;
        }
        return false;
    }

    [[nodiscard]] bool isPrivate() const
    {
        for (const auto& mod : modifiers)
        {
            if (mod == VisibilityModifier::PRIVATE) return true;
        }
        return false;
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        for (const auto& mod : modifiers)
        {
            switch (mod)
            {
            case VisibilityModifier::PUBLIC: os << "public ";
                break;
            case VisibilityModifier::PRIVATE: os << "private ";
                break;
            case VisibilityModifier::STATIC: os << "static ";
                break;
            }
        }
        os << *returnType << " " << name.token_name;
        if (!genericParams.empty())
        {
            os << "<";
            for (size_t i = 0; i < genericParams.size(); ++i)
            {
                if (i > 0) os << ", ";
                os << genericParams.params[i].name;
            }
            os << ">";
        }
        os << "(";
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            if (i > 0) os << ", ";
            os << *parameters[i].type << " " << parameters[i].name;
        }
        if (isVariadic) os << ", ...";
        os << ")";
        if (isExpressionBody())
        {
            os << " => ...";
        }
        os << "\n";
    }
};

struct StructField : Location
{
    std::unique_ptr<Type> type;
    SourceIdentifier name;

    StructField(std::unique_ptr<Type> type, SourceIdentifier name)
        : type(std::move(type)), name(std::move(name))
    {
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "StructField(" << name.token_name << ": " << *type << ")";
    }
};

// T name { get; set; } or T name { get { ... } set { ... } }
struct StructProperty : Location
{
    std::unique_ptr<Type> type;
    SourceIdentifier name;
    bool hasGetter = false;
    bool hasSetter = false;
    std::unique_ptr<Block> getterBody;
    std::unique_ptr<Block> setterBody;
    std::unique_ptr<Expression> getterExpr;
    std::unique_ptr<Expression> setterExpr;

    StructProperty(std::unique_ptr<Type> type, SourceIdentifier name)
        : type(std::move(type)), name(std::move(name))
    {
    }

    [[nodiscard]] bool isAutoProperty() const
    {
        return (hasGetter && !getterBody && !getterExpr) ||
            (hasSetter && !setterBody && !setterExpr);
    }

    // Needs a backing field generated automatically
    [[nodiscard]] bool needsBackingField() const
    {
        return isAutoProperty();
    }

    [[nodiscard]] std::string backingFieldName() const
    {
        return name.token_name;
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "StructProperty(" << name.token_name << ": " << *type << " { ";
        if (hasGetter)
        {
            os << "get";
            if (getterBody) os << " { ... }";
            else if (getterExpr) os << " => ...";
            else os << ";";
            os << " ";
        }
        if (hasSetter)
        {
            os << "set";
            if (setterBody) os << " { ... }";
            else if (setterExpr) os << " => ...";
            else os << ";";
        }
        os << " })";
    }
};

struct StructDeclaration : Location
{
    SourceIdentifier name;
    GenericParams genericParams;
    std::vector<StructField> fields;
    std::vector<StructProperty> properties;
    std::vector<std::unique_ptr<StructMethodDeclaration>> methods;
    std::vector<std::string> implements;
    std::unique_ptr<Type> baseType;
    std::vector<AttributeUsageDeclaration> attributes;

    StructDeclaration(SourceIdentifier name, std::vector<StructField> fields)
        : name(std::move(name)), fields(std::move(fields))
    {
    }

    StructDeclaration(SourceIdentifier name, GenericParams genericParams, std::vector<StructField> fields)
        : name(std::move(name)), genericParams(std::move(genericParams)), fields(std::move(fields))
    {
    }

    [[nodiscard]] bool isGeneric() const
    {
        return !genericParams.empty();
    }

    [[nodiscard]] bool isTransparent() const
    {
        return baseType != nullptr && fields.empty() && baseType->kind != TypeKind::STRUCT;
    }

    [[nodiscard]] bool hasBaseType() const
    {
        return baseType != nullptr;
    }

    void accept(djinn::IDeclarationVisitor& visitor, const std::string& prefix = "") const
    {
        visitor.visit(*this, prefix);
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "StructDeclaration(" << name.token_name;
        if (!genericParams.empty())
        {
            os << "<";
            for (size_t i = 0; i < genericParams.size(); ++i)
            {
                if (i > 0) os << ", ";
                os << genericParams.params[i].name;
            }
            os << ">";
        }
        if (baseType)
        {
            os << " : ";
            baseType->print(os, 0);
        }
        if (!implements.empty())
        {
            os << " implements ";
            for (size_t i = 0; i < implements.size(); ++i)
            {
                if (i > 0) os << ", ";
                os << implements[i];
            }
        }
        if (isTransparent())
        {
            os << " [transparent]";
        }
        os << ")\n";
        for (const auto& field : fields)
        {
            field.print(os, indent + 2);
            os << '\n';
        }
        for (const auto& prop : properties)
        {
            prop.print(os, indent + 2);
            os << '\n';
        }
        for (const auto& method : methods)
        {
            method->print(os, indent + 2);
        }
    }
};

struct InterfaceDeclaration : Location
{
    SourceIdentifier name;
    GenericParams genericParams;
    std::vector<std::unique_ptr<StructMethodDeclaration>> methods; // abstract method signatures

    void accept(djinn::IDeclarationVisitor& visitor, const std::string& prefix = "") const
    {
        visitor.visit(*this, prefix);
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "InterfaceDeclaration(" << name.token_name;
        if (!genericParams.empty())
        {
            os << "<";
            for (size_t i = 0; i < genericParams.size(); ++i)
            {
                if (i > 0) os << ", ";
                os << genericParams.params[i].name;
            }
            os << ">";
        }
        os << ")\n";
        for (const auto& method : methods)
        {
            method->print(os, indent + 2);
        }
    }
};

struct Parameter : Location
{
    std::unique_ptr<Type> type;
    SourceIdentifier name;
    bool isMutable;

    Parameter(std::unique_ptr<Type> type, SourceIdentifier name, const bool isMutable = false) : type(std::move(type)),
        name(std::move(name)),
        isMutable(isMutable)
    {
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "Parameter(" << type << " " << name.token_name << ")";
    }
};

struct FunctionDeclaration : Location
{
    std::unique_ptr<Type> returnType;
    SourceIdentifier name;
    std::vector<Parameter> parameters;
    std::unique_ptr<Block> body;
    bool isAsync = false;
    bool constExpr = false;
    bool constEval = false;

    FunctionDeclaration(std::unique_ptr<Type> retType, SourceIdentifier name, std::vector<Parameter>& parameters,
                        std::unique_ptr<Block> block)
        : returnType(std::move(retType)), name(std::move(name)), parameters(std::move(parameters)),
          body(std::move(block))
    {
    }

    void accept(djinn::IDeclarationVisitor& visitor, const std::string& prefix = "") const
    {
        visitor.visit(*this, prefix);
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        if (isAsync) os << "async ";
        if (constExpr) os << "constexpr ";
        if (constEval) os << "consteval ";
        os << "Function " << name.token_name << "<" << returnType << ">(";
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            if (i > 0) os << ", ";
            os << parameters[i].type << " " << parameters[i].name.token_name;
        }
        os << ")\n";
        if (body) body->print(os, indent + 2);
    }
};


struct NamespaceDeclaration : Location
{
    SourceIdentifier name;
    std::vector<std::unique_ptr<StructDeclaration>> structs;
    std::vector<std::unique_ptr<FunctionDeclaration>> functions;
    std::vector<std::unique_ptr<NamespaceDeclaration>> namespaces; // Nested namespaces

    explicit NamespaceDeclaration(SourceIdentifier name) : name(std::move(name))
    {
    }

    void accept(djinn::IDeclarationVisitor& visitor, const std::string& prefix = "") const
    {
        visitor.visit(*this, prefix);
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "Namespace(" << name.token_name << ")\n";
        for (const auto& s : structs)
        {
            s->print(os, indent + 2);
            os << '\n';
        }
        for (const auto& func : functions)
        {
            func->print(os, indent + 2);
            os << '\n';
        }
        for (const auto& ns : namespaces)
        {
            ns->print(os, indent + 2);
            os << '\n';
        }
    }
};

struct QualifiedName
{
    std::vector<std::string> parts;

    QualifiedName() = default;

    explicit QualifiedName(std::string single) { parts.push_back(std::move(single)); }

    QualifiedName(std::vector<std::string> parts) : parts(std::move(parts))
    {
    }

    void addPart(const std::string& part) { parts.push_back(part); }

    [[nodiscard]] bool isQualified() const { return parts.size() > 1; }
    [[nodiscard]] std::string lastName() const { return parts.empty() ? "" : parts.back(); }

    [[nodiscard]] std::string toString() const
    {
        std::string result;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0) result += "::";
            result += parts[i];
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> namespacePath() const
    {
        if (parts.size() <= 1) return {};
        return std::vector<std::string>(parts.begin(), parts.end() - 1);
    }
};

struct ImportDeclaration : Location
{
    QualifiedName namespacePath;

    explicit ImportDeclaration(QualifiedName path) : namespacePath(std::move(path))
    {
    }

    void accept(djinn::IDeclarationVisitor& visitor, const std::string& prefix = "") const
    {
        visitor.visit(*this, prefix);
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "Import(" << namespacePath.toString() << ")";
    }
};

struct ExternFunctionDeclaration : Location
{
    std::unique_ptr<Type> returnType;
    SourceIdentifier name;
    std::vector<Parameter> parameters;
    bool isVariadic = false;
    std::string abi = "C";

    void accept(djinn::IDeclarationVisitor& visitor, const std::string& prefix = "") const
    {
        visitor.visit(*this, prefix);
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "extern \"" << abi << "\" fn " << name.token_name << "(";
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            if (i > 0) os << ", ";
            os << parameters[i].type << " " << parameters[i].name.token_name;
        }
        if (isVariadic) os << ", ...";
        os << ") -> " << *returnType;
    }
};

struct EnumValueDeclaration : Location
{
    SourceIdentifier name;
    std::vector<Type> types;

    EnumValueDeclaration(SourceIdentifier name, std::vector<Type> types) : name(std::move(name)),
                                                                           types(std::move(types))
    {
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "EnumValue(" << name.token_name << ")";
        for (size_t i = 0; i < types.size(); ++i)
        {
            if (i > 0) os << ", ";
            types[i].print(os, indent);
        }
    }
};

struct EnumDeclaration : Location
{
    SourceIdentifier name;
    GenericParams genericParams;
    std::vector<EnumValueDeclaration> values;

    explicit EnumDeclaration(SourceIdentifier name, std::vector<EnumValueDeclaration> values)
        : name(std::move(name)), values(std::move(values))
    {
    }

    EnumDeclaration(SourceIdentifier name, GenericParams genericParams, std::vector<EnumValueDeclaration> values)
        : name(std::move(name)), genericParams(std::move(genericParams)), values(std::move(values))
    {
    }

    [[nodiscard]] bool isGeneric() const { return !genericParams.empty(); }

    void accept(djinn::IDeclarationVisitor& visitor, const std::string& prefix = "") const
    {
        visitor.visit(*this, prefix);
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "Enum(" << name.token_name;
        if (!genericParams.empty())
        {
            os << "<";
            for (size_t i = 0; i < genericParams.params.size(); ++i)
            {
                if (i > 0) os << ", ";
                os << genericParams.params[i].name;
            }
            os << ">";
        }
        os << ": ";
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i > 0) os << ", ";
            values[i].print(os, indent);
        }
        os << ")";
    }
};


struct ImplDeclaration : Location
{
    std::vector<Type> targetTypes{};
    std::vector<std::string> interfaceNames{}; // empty for inherent impl, filled for "impl Interface for Type"
    std::vector<std::unique_ptr<StructMethodDeclaration>> methods;

    [[nodiscard]] bool isInterfaceImpl() const { return !interfaceNames.empty(); }

    [[nodiscard]] std::string targetTypeName(const size_t index) const
    {
        const auto targetType = targetTypes.at(index);
        if (targetType.kind == TypeKind::STRUCT) return targetType.structName;
        // For primitive types, build the name from kind+size
        if (targetType.kind == TypeKind::INTEGER)
        {
            return (targetType.sign ? "i" : "u") + std::to_string(targetType.size);
        }
        if (targetType.kind == TypeKind::F32) return "f32";
        if (targetType.kind == TypeKind::F64) return "f64";
        if (targetType.kind == TypeKind::F16) return "f16";
        if (targetType.kind == TypeKind::F128) return "f128";
        return targetType.toHumanString();
    }

    void accept(djinn::IDeclarationVisitor& visitor, const std::string& prefix = "") const
    {
        visitor.visit(*this, prefix);
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "ImplDeclaration(";
        if (isInterfaceImpl())
        {
            for (size_t i = 0; i < interfaceNames.size(); ++i)
            {
                if (i > 0) os << ", ";
                os << interfaceNames[i];
            }
            os << " for ";
        }

        for (int i = 0; i < this->targetTypes.size(); ++i)
        {
            os << targetTypeName(i);
        }
        os << ")\n";

        for (const auto& method : methods)
        {
            method->print(os, indent + 2);
        }
    }
};

struct Program : Location
{
    // File-scoped namespace: "namespace foo;" at top of file
    // Empty string means global namespace
    std::string name;
    std::string fileNamespace;

    std::vector<std::unique_ptr<ImportDeclaration>> imports;
    std::vector<std::unique_ptr<ExternFunctionDeclaration>> externFunctions;
    std::vector<std::unique_ptr<InterfaceDeclaration>> interfaces;
    std::vector<std::unique_ptr<StructDeclaration>> structs;
    std::vector<std::unique_ptr<FunctionDeclaration>> functions;
    std::vector<std::unique_ptr<NamespaceDeclaration>> namespaces;
    std::vector<std::unique_ptr<EnumDeclaration>> enums;
    std::vector<std::unique_ptr<ImplDeclaration>> impls;

    explicit Program(const std::string& name)
    {
        this->name = name;
    }

    [[nodiscard]] bool hasFileNamespace() const { return !fileNamespace.empty(); }

    [[nodiscard]] std::string getNamespacePrefix() const
    {
        return fileNamespace.empty() ? "" : fileNamespace + "::";
    }

    void acceptAll(djinn::IDeclarationVisitor& visitor) const
    {
        const std::string prefix = fileNamespace;
        for (const auto& decl : imports) decl->accept(visitor, prefix);
        for (const auto& decl : externFunctions) decl->accept(visitor, prefix);
        for (const auto& decl : interfaces) decl->accept(visitor, prefix);
        for (const auto& decl : enums) decl->accept(visitor, prefix);
        for (const auto& decl : structs) decl->accept(visitor, prefix);
        for (const auto& decl : impls) decl->accept(visitor, prefix);
        for (const auto& decl : functions) decl->accept(visitor, prefix);
        for (const auto& decl : namespaces) decl->accept(visitor, "");
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        os << "Program";
        if (!fileNamespace.empty())
        {
            os << " (namespace " << fileNamespace << ")";
        }
        os << "\n";
        for (const auto& imp : imports)
        {
            imp->print(os, indent + 2);
            os << '\n';
        }
        for (const auto& ext : externFunctions)
        {
            ext->print(os, indent + 2);
            os << '\n';
        }
        for (const auto& iface : interfaces)
        {
            iface->print(os, indent + 2);
            os << '\n';
        }
        for (const auto& s : structs)
        {
            s->print(os, indent + 2);
            os << '\n';
        }
        for (const auto& impl : impls)
        {
            impl->print(os, indent + 2);
            os << '\n';
        }
        for (const auto& func : functions)
        {
            func->print(os, indent + 2);
            os << '\n';
        }
        for (const auto& ns : namespaces)
        {
            ns->print(os, indent + 2);
            os << '\n';
        }
    }
};


#endif //DJINN_DECLARATION_H