//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_AST_H
#define DJINN_AST_H

#include <string>
#include <utility>
#include <vector>
#include <memory>
#include "../utils/string_utils.h"
#include "../lexer/TokenType.h"
#include "../lexer/Token.h"
#include <ostream>
#include <unordered_map>
#include <llvm/IR/Type.h>

inline void writeIndent(std::ostream &os, const int indent) {
    for (int i = 0; i < indent; ++i) os.put(' ');
}

struct ASTNode {
    virtual ~ASTNode() = default;

    virtual void print(std::ostream &os, int indent = 0) const = 0;
};

enum class TypeKind: uint8_t {
    INTEGER,
    STRING,
    VOID,
    F16,
    F32,
    F64,
    F128,
    STRUCT,
    AUTO,
    ARRAY
};

const std::unordered_map<std::string, TypeKind> string_to_type_kind = {
    {"f16", TypeKind::F16},
    {"f32", TypeKind::F32},
    {"f64", TypeKind::F64},
    {"f128", TypeKind::F128},
    {"void", TypeKind::VOID},
    {"auto", TypeKind::AUTO},
    {"struct", TypeKind::STRUCT},
    {"string", TypeKind::STRING},
};


struct Type : ASTNode {
    size_t size;
    TypeKind kind;
    bool sign;
    bool nullable;
    std::unique_ptr<Type> elementType;  // Para arrays: o tipo do elemento

    Type(TypeKind kind, size_t size, bool sign)
        : kind(kind),
          size(size),
          sign(sign),
          elementType(nullptr) {
    }

    Type(const Type& other)
        : kind(other.kind),
          size(other.size),
          sign(other.sign),
          nullable(other.nullable),
          elementType(other.elementType ? std::make_unique<Type>(*other.elementType) : nullptr) {
    }

    Type& operator=(const Type& other) {
        if (this != &other) {
            kind = other.kind;
            size = other.size;
            sign = other.sign;
            nullable = other.nullable;
            elementType = other.elementType ? std::make_unique<Type>(*other.elementType) : nullptr;
        }
        return *this;
    }

    Type(Type&&) = default;
    Type& operator=(Type&&) = default;

    static Type autod() {
        return Type(TypeKind::AUTO, 0, false);
    }

    static Type floated(const size_t size) {
        switch (size) {
            case static_cast<size_t>(16): return Type{TypeKind::F16, 16, true};
            case static_cast<size_t>(32): return Type{TypeKind::F32, 32, true};
            case static_cast<size_t>(64): return Type{TypeKind::F64, 64, true};
            case static_cast<size_t>(128): return Type{TypeKind::F128, 128, true};
            default: throw std::exception("invalid type bit size kind for float");
        }
    }

    static Type voided() {
        return Type(TypeKind::VOID, 0, false);
    }

    static Type stringed() {
        return Type(TypeKind::STRING, 0, false);
    }

    static Type array(Type elemType) {
        Type arr(TypeKind::ARRAY, 0, false);
        arr.elementType = std::make_unique<Type>(std::move(elemType));
        return arr;
    }

    static Type integer(size_t bits, bool sign) {
        return Type(TypeKind::INTEGER, bits, sign);
    }

    static std::string kindToString(TypeKind k) {
        switch (k) {
            case TypeKind::INTEGER: return "INTEGER";
            case TypeKind::STRING: return "STRING";
            case TypeKind::VOID: return "VOID";
            case TypeKind::F16: return "F16";
            case TypeKind::F32: return "F32";
            case TypeKind::F64: return "F64";
            case TypeKind::F128: return "F128";
            case TypeKind::STRUCT: return "STRUCT";
            case TypeKind::AUTO: return "AUTO";
            case TypeKind::ARRAY: return "ARRAY";
            default: return "UNKNOWN";
        }
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "Type(" << kindToString(kind);
        if (kind == TypeKind::ARRAY && elementType) {
            os << "<";
            elementType->print(os, 0);
            os << ">";
        } else if (size > 0) {
            os << ", " << size;
        }
        os << ")";
    }

    void floated(long bits);

    static Type fromToken(const Token &token) {
        if (token.value.starts_with("i") || token.value.starts_with("u")) {
            const auto bits = std::stol(token.value.substr(1));
            return Type(TypeKind::INTEGER, bits, token.value.starts_with("i"));
        }

        if (token.value == "f16") return Type(TypeKind::F16, 16, true);
        if (token.value == "f32") return Type(TypeKind::F32, 32, true);
        if (token.value == "f64") return Type(TypeKind::F64, 64, true);
        if (token.value == "f128") return Type(TypeKind::F128, 128, true);
        if (token.value == "void") return Type(TypeKind::VOID, 0, false);
        if (token.value == "string") return Type(TypeKind::STRING, 0, false);
        if (token.value == "auto") return Type(TypeKind::AUTO, 0, false);

        return Type(TypeKind::VOID, 0, false);
    }
};

inline std::ostream &operator<<(std::ostream &os, const ASTNode &node) {
    node.print(os);
    return os;
}

struct FieldType{
    Type type;
    uint8_t index{};
    bool pointer;
    bool stack;
} ;

struct Struct : ASTNode {
    std::string name;
    std::unordered_map<uint32_t, FieldType> fields{};
};

struct Expression : ASTNode {
};

struct VariableDeclaration : Expression {
    Type type;
    std::string name;

    VariableDeclaration(Type type, std::string name)
        : type(std::move(type)),
          name(std::move(name)) {
    }

    void print(std::ostream &os, int indent) const override {
        writeIndent(os, indent);
        os << "VariableDeclaration(" << name << ": " << type << ")";
    }
};

struct Assignment : Expression {
    std::string name;
    std::unique_ptr<Expression> value;

    Assignment(std::string name, std::unique_ptr<Expression> value)
        : name(std::move(name)), value(std::move(value)) {
    }

    void print(std::ostream &os, int indent) const override {
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

    VariableInit(Type type, std::string name, std::unique_ptr<Expression> value)
        : type(std::move(type)), name(std::move(name)), value(std::move(value)) {
    }

    void print(std::ostream &os, int indent) const override {
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

    IntegerLiteral(const std::string &val, bool sign) : value(val), sign(sign) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "IntegerLiteral(" << value << ")";
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

struct FunctionCall : Expression {
    std::string name;
    std::vector<std::unique_ptr<Expression> > arguments;

    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression> > args)
        : name(std::move(n)), arguments(std::move(args)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "FunctionCall(" << name << ")\n";
        for (const auto &arg: arguments) {
            arg->print(os, indent + 2);
            os << '\n';
        }
    }
};

inline std::string tokenTypeToString(TokenType type) {
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

struct UnaryExpression : Expression {
    TokenType op;
    std::unique_ptr<Expression> operand;

    UnaryExpression(TokenType op, std::unique_ptr<Expression> operand)
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

    BinaryExpression(std::unique_ptr<Expression> left, TokenType op, std::unique_ptr<Expression> right)
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

struct Statement : ASTNode {
};

struct ExpressionStatement : Statement {
    std::unique_ptr<Expression> expression;

    explicit ExpressionStatement(std::unique_ptr<Expression> expr)
        : expression(std::move(expr)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "ExpressionStatement\n";
        expression->print(os, indent + 2);
    }
};

struct ReturnStatement : Statement {
    std::unique_ptr<Expression> value;

    explicit ReturnStatement(std::unique_ptr<Expression> val)
        : value(std::move(val)) {
    }

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "ReturnStatement\n";
        if (value) value->print(os, indent + 2);
    }
};

struct Block : Statement {
    std::vector<std::unique_ptr<Statement> > statements;

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "Block\n";
        for (const auto &stmt: statements) {
            stmt->print(os, indent + 2);
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
    std::vector<std::unique_ptr<FunctionDeclaration> > functions;

    void print(std::ostream &os, const int indent = 0) const override {
        os << "Program\n";
        for (const auto &func: functions) {
            func->print(os, indent + 2);
            os << '\n';
        }
    }
};

#endif //DJINN_AST_H
