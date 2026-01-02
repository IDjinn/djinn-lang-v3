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
    std::vector<StructField> fields;

    StructDeclaration(std::string name, std::vector<StructField> fields)
        : name(std::move(name)), fields(std::move(fields)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "StructDeclaration(" << name << ")\n";
        for (const auto &field: fields) {
            field.print(os, indent + 2);
            os << '\n';
        }
    }
};

struct Parameter : ASTNode {
    std::unique_ptr<Type> type;
    std::string name;

    Parameter(std::unique_ptr<Type> type, std::string name) : type(std::move(type)), name(std::move(name)) {
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

struct Program : ASTNode {
    std::vector<std::unique_ptr<StructDeclaration> > structs;
    std::vector<std::unique_ptr<FunctionDeclaration> > functions;

    void print(std::ostream &os, const int indent = 0) const override {
        os << "Program\n";
        for (const auto &s: structs) {
            s->print(os, indent + 2);
            os << '\n';
        }
        for (const auto &func: functions) {
            func->print(os, indent + 2);
            os << '\n';
        }
    }
};

#endif //DJINN_DECLARATION_H
