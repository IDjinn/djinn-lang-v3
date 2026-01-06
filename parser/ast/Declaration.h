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

enum class VisibilityModifier {
    PUBLIC,
    PRIVATE,
    STATIC,
};

struct MethodParameter {
    std::unique_ptr<Type> type;
    std::string name;
    bool isMutable = false;

    MethodParameter(std::unique_ptr<Type> type, std::string name, bool isMutable = false)
        : type(std::move(type)), name(std::move(name)), isMutable(isMutable) {
    }
};

struct StructMethodDeclaration : ASTNode {
    std::unique_ptr<Type> returnType;
    std::string name;
    std::vector<MethodParameter> parameters;
    GenericParams genericParams;
    std::vector<VisibilityModifier> modifiers;
    std::unique_ptr<Block> body; // { ... }
    std::unique_ptr<Expression> expression; // => expr;

    [[nodiscard]] bool isExpressionBody() const { return expression != nullptr; }
    [[nodiscard]] bool isAbstract() const { return body == nullptr && expression == nullptr; }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        for (const auto &mod: modifiers) {
            switch (mod) {
                case VisibilityModifier::PUBLIC: os << "public ";
                    break;
                case VisibilityModifier::PRIVATE: os << "private ";
                    break;
                case VisibilityModifier::STATIC: os << "static ";
                    break;
            }
        }
        os << *returnType << " " << name;
        if (!genericParams.empty()) {
            os << "<";
            for (size_t i = 0; i < genericParams.size(); ++i) {
                if (i > 0) os << ", ";
                os << genericParams.params[i].name;
            }
            os << ">";
        }
        os << "(";
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i > 0) os << ", ";
            os << *parameters[i].type << " " << parameters[i].name;
        }
        os << ")";
        if (isExpressionBody()) {
            os << " => ...";
        }
        os << "\n";
    }
};

struct StructField : ASTNode {
    std::unique_ptr<Type> type;
    std::string name;

    StructField(std::unique_ptr<Type> type, std::string name)
        : type(std::move(type)), name(std::move(name)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "StructField(" << name << ": " << *type << ")";
    }
};

struct StructDeclaration : ASTNode {
    std::string name;
    GenericParams genericParams;
    std::vector<StructField> fields;
    std::vector<std::unique_ptr<StructMethodDeclaration> > methods;
    std::vector<std::string> implements; // interfaces this struct implements

    StructDeclaration(std::string name, std::vector<StructField> fields)
        : name(std::move(name)), fields(std::move(fields)) {
    }

    StructDeclaration(std::string name, GenericParams genericParams, std::vector<StructField> fields)
        : name(std::move(name)), genericParams(std::move(genericParams)), fields(std::move(fields)) {
    }

    [[nodiscard]] bool isGeneric() const {
        return !genericParams.empty();
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "StructDeclaration(" << name;
        if (!genericParams.empty()) {
            os << "<";
            for (size_t i = 0; i < genericParams.size(); ++i) {
                if (i > 0) os << ", ";
                os << genericParams.params[i].name;
            }
            os << ">";
        }
        if (!implements.empty()) {
            os << " : ";
            for (size_t i = 0; i < implements.size(); ++i) {
                if (i > 0) os << ", ";
                os << implements[i];
            }
        }
        os << ")\n";
        for (const auto &field: fields) {
            field.print(os, indent + 2);
            os << '\n';
        }
        for (const auto &method: methods) {
            method->print(os, indent + 2);
        }
    }
};

struct InterfaceDeclaration : ASTNode {
    std::string name;
    GenericParams genericParams;
    std::vector<std::unique_ptr<StructMethodDeclaration> > methods; // abstract method signatures

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "InterfaceDeclaration(" << name;
        if (!genericParams.empty()) {
            os << "<";
            for (size_t i = 0; i < genericParams.size(); ++i) {
                if (i > 0) os << ", ";
                os << genericParams.params[i].name;
            }
            os << ">";
        }
        os << ")\n";
        for (const auto &method: methods) {
            method->print(os, indent + 2);
        }
    }
};

struct Parameter : ASTNode {
    std::unique_ptr<Type> type;
    std::string name;
    bool isMutable;

    Parameter(std::unique_ptr<Type> type, std::string name, const bool isMutable = false) : type(std::move(type)),
        name(std::move(name)),
        isMutable(isMutable) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "Parameter(" << type << " " << name << ")";
    }
};

struct FunctionDeclaration : ASTNode {
    std::unique_ptr<Type> returnType;
    std::string name;
    std::vector<Parameter> parameters;
    std::unique_ptr<Block> body;

    FunctionDeclaration(std::unique_ptr<Type> retType, std::string name, std::vector<Parameter> &parameters,
                        std::unique_ptr<Block> block)
        : returnType(std::move(retType)), name(std::move(name)), parameters(std::move(parameters)),
          body(std::move(block)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "Function " << name << "<" << returnType << ">(";
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i > 0) os << ", ";
            os << parameters[i].type << " " << parameters[i].name;
        }
        os << ")\n";
        if (body) body->print(os, indent + 2);
    }
};


struct NamespaceDeclaration : ASTNode {
    std::string name;
    std::vector<std::unique_ptr<StructDeclaration> > structs;
    std::vector<std::unique_ptr<FunctionDeclaration> > functions;
    std::vector<std::unique_ptr<NamespaceDeclaration> > namespaces; // Nested namespaces

    explicit NamespaceDeclaration(std::string name) : name(std::move(name)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "Namespace(" << name << ")\n";
        for (const auto &s: structs) {
            s->print(os, indent + 2);
            os << '\n';
        }
        for (const auto &func: functions) {
            func->print(os, indent + 2);
            os << '\n';
        }
        for (const auto &ns: namespaces) {
            ns->print(os, indent + 2);
            os << '\n';
        }
    }
};

struct QualifiedName {
    std::vector<std::string> parts;

    QualifiedName() = default;

    explicit QualifiedName(std::string single) { parts.push_back(std::move(single)); }

    QualifiedName(std::vector<std::string> parts) : parts(std::move(parts)) {
    }

    void addPart(const std::string &part) { parts.push_back(part); }

    [[nodiscard]] bool isQualified() const { return parts.size() > 1; }
    [[nodiscard]] std::string lastName() const { return parts.empty() ? "" : parts.back(); }

    [[nodiscard]] std::string toString() const {
        std::string result;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) result += "::";
            result += parts[i];
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> namespacePath() const {
        if (parts.size() <= 1) return {};
        return std::vector<std::string>(parts.begin(), parts.end() - 1);
    }
};

struct ImportDeclaration : ASTNode {
    QualifiedName namespacePath;

    explicit ImportDeclaration(QualifiedName path) : namespacePath(std::move(path)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "Import(" << namespacePath.toString() << ")";
    }
};

struct ExternFunctionDeclaration : ASTNode {
    std::unique_ptr<Type> returnType;
    std::string name;
    std::vector<Parameter> parameters;
    bool isVariadic = false;
    std::string abi = "C";

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "extern \"" << abi << "\" fn " << name << "(";
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (i > 0) os << ", ";
            os << parameters[i].type << " " << parameters[i].name;
        }
        if (isVariadic) os << ", ...";
        os << ") -> " << *returnType;
    }
};

// Forward declarations

struct Program : ASTNode {
    // File-scoped namespace: "namespace foo;" at top of file
    // Empty string means global namespace
    std::string fileNamespace;

    std::vector<std::unique_ptr<ImportDeclaration> > imports;
    std::vector<std::unique_ptr<ExternFunctionDeclaration> > externFunctions;
    std::vector<std::unique_ptr<InterfaceDeclaration> > interfaces;
    std::vector<std::unique_ptr<StructDeclaration> > structs;
    std::vector<std::unique_ptr<FunctionDeclaration> > functions;
    std::vector<std::unique_ptr<NamespaceDeclaration> > namespaces;

    [[nodiscard]] bool hasFileNamespace() const { return !fileNamespace.empty(); }

    [[nodiscard]] std::string getNamespacePrefix() const {
        return fileNamespace.empty() ? "" : fileNamespace + "::";
    }

    void print(std::ostream &os, const int indent = 0) const override {
        os << "Program";
        if (!fileNamespace.empty()) {
            os << " (namespace " << fileNamespace << ")";
        }
        os << "\n";
        for (const auto &imp: imports) {
            imp->print(os, indent + 2);
            os << '\n';
        }
        for (const auto &ext: externFunctions) {
            ext->print(os, indent + 2);
            os << '\n';
        }
        for (const auto &iface: interfaces) {
            iface->print(os, indent + 2);
            os << '\n';
        }
        for (const auto &s: structs) {
            s->print(os, indent + 2);
            os << '\n';
        }
        for (const auto &func: functions) {
            func->print(os, indent + 2);
            os << '\n';
        }
        for (const auto &ns: namespaces) {
            ns->print(os, indent + 2);
            os << '\n';
        }
    }
};


#endif //DJINN_DECLARATION_H