//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_EXPRESSION_H
#define DJINN_EXPRESSION_H

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "ASTNode.h"
#include "Type.h"
#include "../../utils/string_utils.h"
#include "../../lexer/TokenType.h"
#include "../../visitor/ExpressionVisitor.h"

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

struct Expression : Location {
    virtual void accept(djinn::IExpressionVisitor &visitor) const = 0;
};

struct VariableDeclaration : Expression {
    Type type;
    SourceIdentifier name;
    bool isMutable;

    VariableDeclaration(Type type, SourceIdentifier name, const bool isMutable)
        : type(std::move(type)),
          name(std::move(name)), isMutable(isMutable) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent) const override {
        writeIndent(os, indent);
        os << "VariableDeclaration(" << name.token_name << ": " << type << (isMutable ? ": mut" : "") << ")";
    }
};

struct Assignment : Expression {
    SourceIdentifier name;
    std::unique_ptr<Expression> value;

    Assignment(SourceIdentifier name, std::unique_ptr<Expression> value)
        : name(std::move(name)), value(std::move(value)) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent) const override {
        writeIndent(os, indent);
        os << "Assignment(" << name.token_name << " =\n";
        value->print(os, indent + 2);
        os << ")";
    }
};

struct VariableInit : Expression {
    Type type;
    SourceIdentifier name;
    std::unique_ptr<Expression> value;
    bool isMutable;

    VariableInit(Type type, SourceIdentifier name, std::unique_ptr<Expression> value, bool isMutable)
        : type(std::move(type)), name(std::move(name)), value(std::move(value)), isMutable(isMutable) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent) const override {
        writeIndent(os, indent);
        os << "VariableInit(" << name.token_name << ": " << type << " =\n";
        value->print(os, indent + 2);
        os << ")";
    }
};

struct StringLiteral : Expression {
    std::string value;

    explicit StringLiteral(std::string val) : value(std::move(val)) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

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

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "IntegerLiteral(" << value << ")";
    }
};

struct FloatLiteral : Expression {
    std::string value;

    explicit FloatLiteral(const std::string &val) : value(val) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "FloatLiteral(" << value << ")";
    }
};

struct Identifier : Expression {
    SourceIdentifier identifier;

    explicit Identifier(SourceIdentifier source_identifier) : identifier(std::move(source_identifier)) {
    }

    [[nodiscard]] const std::string &name() const { return identifier.token_name; }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "Identifier(" << identifier.token_name << ")";
    }
};

struct FieldAccess : Expression {
    std::unique_ptr<Expression> object;
    SourceIdentifier fieldName;

    FieldAccess(std::unique_ptr<Expression> obj, SourceIdentifier field)
        : object(std::move(obj)), fieldName(std::move(field)) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "FieldAccess(." << fieldName.token_name << ")\n";
        object->print(os, indent + 2);
    }
};

struct FieldAssignment : Expression {
    std::unique_ptr<Expression> object;
    SourceIdentifier fieldName;
    std::unique_ptr<Expression> value;

    FieldAssignment(std::unique_ptr<Expression> obj, SourceIdentifier field, std::unique_ptr<Expression> val)
        : object(std::move(obj)), fieldName(std::move(field)), value(std::move(val)) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "FieldAssignment(." << fieldName.token_name << " =\n";
        object->print(os, indent + 2);
        os << "\n";
        value->print(os, indent + 2);
        os << ")";
    }
};

struct FunctionCall : Expression {
    SourceIdentifier name;
    std::vector<std::unique_ptr<Expression> > arguments;
    std::unique_ptr<Expression> receiver; // optional: for method calls (object.method())
    std::vector<Type> typeArguments; // generic type arguments: Result<i32, string*>::Ok(...)
    bool hasVariadicForward = false; // true if call includes ... to forward variadic args

    FunctionCall(SourceIdentifier n, std::vector<std::unique_ptr<Expression> > args)
        : name(std::move(n)), arguments(std::move(args)) {
    }

    FunctionCall(SourceIdentifier n, std::vector<std::unique_ptr<Expression> > args, std::unique_ptr<Expression> recv)
        : name(std::move(n)), arguments(std::move(args)), receiver(std::move(recv)) {
    }

    FunctionCall(SourceIdentifier n, std::vector<Type> typeArgs, std::vector<std::unique_ptr<Expression> > args)
        : name(std::move(n)), arguments(std::move(args)), typeArguments(std::move(typeArgs)) {
    }

    [[nodiscard]] bool isMethodCall() const { return receiver != nullptr; }
    [[nodiscard]] bool hasTypeArguments() const { return !typeArguments.empty(); }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        if (isMethodCall()) {
            os << "MethodCall(." << name.token_name << ")\n";
            receiver->print(os, indent + 2);
            os << '\n';
        } else {
            os << "FunctionCall(" << name.token_name;
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

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

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

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "BinaryExpression(" << tokenTypeToString(op) << ")\n";
        left->print(os, indent + 2);
        os << '\n';
        right->print(os, indent + 2);
    }
};

struct InitializerElement : Location {
    SourceIdentifier fieldName;
    std::unique_ptr<Expression> value;

    InitializerElement(SourceIdentifier fieldName, std::unique_ptr<Expression> value)
        : fieldName(std::move(fieldName)), value(std::move(value)) {
    }

    explicit InitializerElement(std::unique_ptr<Expression> value)
        : fieldName(), value(std::move(value)) {
    }

    [[nodiscard]] bool isDesignated() const { return !fieldName.token_name.empty(); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        if (isDesignated()) {
            os << "DesignatedInit(." << fieldName.token_name << " =\n";
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

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

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

struct IndexAccess : Expression {
    std::unique_ptr<Expression> object;
    std::unique_ptr<Expression> index;

    IndexAccess(std::unique_ptr<Expression> obj, std::unique_ptr<Expression> idx)
        : object(std::move(obj)), index(std::move(idx)) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "IndexAccess[\n";
        object->print(os, indent + 2);
        os << "\n";
        index->print(os, indent + 2);
        os << "]";
    }
};

struct IndexAssignment : Expression {
    std::unique_ptr<Expression> object;
    std::unique_ptr<Expression> index;
    std::unique_ptr<Expression> value;

    IndexAssignment(std::unique_ptr<Expression> obj, std::unique_ptr<Expression> idx, std::unique_ptr<Expression> val)
        : object(std::move(obj)), index(std::move(idx)), value(std::move(val)) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "IndexAssignment[\n";
        object->print(os, indent + 2);
        os << "\n";
        index->print(os, indent + 2);
        os << "] =\n";
        value->print(os, indent + 2);
    }
};

// Pattern matching switch arm: VariantName binding -> result_expr
struct SwitchArm : Location {
    SourceIdentifier variantName; // The variant to match (e.g., "Value", "Empty")
    std::optional<SourceIdentifier> binding; // Optional binding name (e.g., "val" in "Value val")
    std::unique_ptr<Expression> result; // The result expression

    SwitchArm(SourceIdentifier variant, std::optional<SourceIdentifier> bind, std::unique_ptr<Expression> res)
        : variantName(std::move(variant)), binding(std::move(bind)), result(std::move(res)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "SwitchArm(" << variantName.token_name;
        if (binding) {
            os << " " << binding->token_name;
        }
        os << " ->\n";
        result->print(os, indent + 2);
        os << ")";
    }
};

// Pattern matching switch expression: switch expr { arms... }
struct SwitchExpression : Expression {
    std::unique_ptr<Expression> value; // The enum value being matched
    std::vector<SwitchArm> arms; // The match arms

    SwitchExpression(std::unique_ptr<Expression> val, std::vector<SwitchArm> a)
        : value(std::move(val)), arms(std::move(a)) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "SwitchExpression\n";
        value->print(os, indent + 2);
        os << "\n";
        for (const auto &arm: arms) {
            arm.print(os, indent + 2);
            os << "\n";
        }
    }
};

// Variadic forwarding: ... (forwards variadic arguments to another variadic function)
struct VariadicForward : Expression {
    VariadicForward() = default;

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "VariadicForward(...)";
    }
};

// Heap-allocated constructor call: new StructName(args)
struct NewExpression : Expression {
    std::unique_ptr<FunctionCall> constructorCall;

    explicit NewExpression(std::unique_ptr<FunctionCall> call)
        : constructorCall(std::move(call)) {
    }

    void accept(djinn::IExpressionVisitor &visitor) const override { visitor.visit(*this); }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "NewExpression(\n";
        constructorCall->print(os, indent + 2);
        os << ")";
    }
};

#endif //DJINN_EXPRESSION_H