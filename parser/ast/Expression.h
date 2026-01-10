//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_EXPRESSION_H
#define DJINN_EXPRESSION_H

#include <string>
#include <vector>
#include <memory>
#include "ASTNode.h"
#include "Type.h"
#include "../../utils/string_utils.h"
#include "../../lexer/TokenType.h"

inline std::string tokenTypeToString(const TokenType type) {
    switch (type) {
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::STAR: return "*";
        case TokenType::SLASH: return "/";
        case TokenType::PERCENT: return "%";
        case TokenType::EQUAL_EQUAL: return "==";
        case TokenType::BANG_EQUAL: return "!=";
        case TokenType::LESS: return "<";
        case TokenType::LESS_EQUAL: return "<=";
        case TokenType::GREATER: return ">";
        case TokenType::GREATER_EQUAL: return ">=";
        case TokenType::BANG: return "!";
        case TokenType::AND_AND: return "&&";
        case TokenType::OR_OR: return "||";
        case TokenType::EQUAL: return "=";
        default: return "?";
    }
}

struct Expression : ASTNode {
};

struct VariableDeclaration : Expression {
    Type type;
    std::string name;
    bool isMutable;

    VariableDeclaration(Type type, std::string name, const bool isMutable)
        : type(std::move(type)),
          name(std::move(name)), isMutable(isMutable) {
    }

    void print(std::ostream &os, const int indent) const override {
        writeIndent(os, indent);
        os << "VariableDeclaration(" << name << ": " << type << (isMutable ? ": mut" : "") << ")";
    }
};

struct Assignment : Expression {
    std::string name;
    std::unique_ptr<Expression> value;

    Assignment(std::string name, std::unique_ptr<Expression> value)
        : name(std::move(name)), value(std::move(value)) {
    }

    void print(std::ostream &os, const int indent) const override {
        writeIndent(os, indent);
        os << "Assignment(" << name << " =\n";
        value->print(os, indent + 2);
        os << ")";
    }
};

struct VariableInit : Expression {
    Type type;
    std::string name;
    std::unique_ptr<Expression> value;
    bool isMutable;

    VariableInit(Type type, std::string name, std::unique_ptr<Expression> value, bool isMutable)
        : type(std::move(type)), name(std::move(name)), value(std::move(value)), isMutable(isMutable) {
    }

    void print(std::ostream &os, const int indent) const override {
        writeIndent(os, indent);
        os << "VariableInit(" << name << ": " << type << " =\n";
        value->print(os, indent + 2);
        os << ")";
    }
};

struct StringLiteral : Expression {
    std::string value;

    explicit StringLiteral(std::string val) : value(std::move(val)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "StringLiteral(\"" << string_utils::escape_visible(value) << "\")";
    }
};

struct IntegerLiteral : Expression {
    std::string value;
    bool sign;

    IntegerLiteral(const std::string &val, const bool sign) : value(val), sign(sign) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "IntegerLiteral(" << value << ")";
    }
};

struct FloatLiteral : Expression {
    std::string value;

    explicit FloatLiteral(const std::string &val) : value(val) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "FloatLiteral(" << value << ")";
    }
};

struct Identifier : Expression {
    std::string name;

    explicit Identifier(std::string n) : name(std::move(n)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "Identifier(" << name << ")";
    }
};

struct FieldAccess : Expression {
    std::unique_ptr<Expression> object;
    std::string fieldName;

    FieldAccess(std::unique_ptr<Expression> obj, std::string field)
        : object(std::move(obj)), fieldName(std::move(field)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "FieldAccess(." << fieldName << ")\n";
        object->print(os, indent + 2);
    }
};

struct FieldAssignment : Expression {
    std::unique_ptr<Expression> object;
    std::string fieldName;
    std::unique_ptr<Expression> value;

    FieldAssignment(std::unique_ptr<Expression> obj, std::string field, std::unique_ptr<Expression> val)
        : object(std::move(obj)), fieldName(std::move(field)), value(std::move(val)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "FieldAssignment(." << fieldName << " =\n";
        object->print(os, indent + 2);
        os << "\n";
        value->print(os, indent + 2);
        os << ")";
    }
};

struct FunctionCall : Expression {
    std::string name;
    std::vector<std::unique_ptr<Expression> > arguments;
    std::unique_ptr<Expression> receiver; // optional: for method calls (object.method())
    std::vector<Type> typeArguments; // generic type arguments: Result<i32, string*>::Ok(...)

    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression> > args)
        : name(std::move(n)), arguments(std::move(args)) {
    }

    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression> > args, std::unique_ptr<Expression> recv)
        : name(std::move(n)), arguments(std::move(args)), receiver(std::move(recv)) {
    }

    FunctionCall(std::string n, std::vector<Type> typeArgs, std::vector<std::unique_ptr<Expression> > args)
        : name(std::move(n)), arguments(std::move(args)), typeArguments(std::move(typeArgs)) {
    }

    [[nodiscard]] bool isMethodCall() const { return receiver != nullptr; }
    [[nodiscard]] bool hasTypeArguments() const { return !typeArguments.empty(); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        if (isMethodCall()) {
            os << "MethodCall(." << name << ")\n";
            receiver->print(os, indent + 2);
            os << '\n';
        } else {
            os << "FunctionCall(" << name;
            if (!typeArguments.empty()) {
                os << "<";
                for (size_t i = 0; i < typeArguments.size(); ++i) {
                    if (i > 0) os << ", ";
                    typeArguments[i].print(os, 0);
                }
                os << ">";
            }
            os << ")\n";
        }
        for (const auto &arg: arguments) {
            arg->print(os, indent + 2);
            os << '\n';
        }
    }
};

struct UnaryExpression : Expression {
    TokenType op;
    std::unique_ptr<Expression> operand;

    UnaryExpression(const TokenType op, std::unique_ptr<Expression> operand)
        : op(op), operand(std::move(operand)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "UnaryExpression(" << tokenTypeToString(op) << ")\n";
        operand->print(os, indent + 2);
    }
};

struct BinaryExpression : Expression {
    std::unique_ptr<Expression> left;
    TokenType op;
    std::unique_ptr<Expression> right;

    BinaryExpression(std::unique_ptr<Expression> left, const TokenType op, std::unique_ptr<Expression> right)
        : left(std::move(left)), op(op), right(std::move(right)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "BinaryExpression(" << tokenTypeToString(op) << ")\n";
        left->print(os, indent + 2);
        os << '\n';
        right->print(os, indent + 2);
    }
};

struct InitializerElement : ASTNode {
    std::string fieldName;
    std::unique_ptr<Expression> value;

    InitializerElement(std::string fieldName, std::unique_ptr<Expression> value)
        : fieldName(std::move(fieldName)), value(std::move(value)) {
    }

    explicit InitializerElement(std::unique_ptr<Expression> value)
        : fieldName(""), value(std::move(value)) {
    }

    [[nodiscard]] bool isDesignated() const { return !fieldName.empty(); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        if (isDesignated()) {
            os << "DesignatedInit(." << fieldName << " =\n";
        } else {
            os << "PositionalInit(\n";
        }
        value->print(os, indent + 2);
        os << ")";
    }
};

struct BraceInitializer : Expression {
    std::vector<InitializerElement> elements;

    explicit BraceInitializer(std::vector<InitializerElement> elements)
        : elements(std::move(elements)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "BraceInitializer {\n";
        for (const auto &elem: elements) {
            elem.print(os, indent + 2);
            os << '\n';
        }
        writeIndent(os, indent);
        os << "}";
    }
};

#endif //DJINN_EXPRESSION_H