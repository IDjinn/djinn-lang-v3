//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_EXPRESSION_H
#define DJINN_EXPRESSION_H

#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <optional>
#include "ASTNode.h"
#include "Type.h"
#include "../../utils/string_utils.h"
#include "../../lexer/TokenType.h"
#include "../../visitor/ExpressionVisitor.h"

constexpr std::string tokenTypeToString(const TokenType type)
{
    switch (type)
    {
    case TokenType::PLUS: return "+";
    case TokenType::MINUS: return "-";
    case TokenType::PLUS_PLUS: return "++";
    case TokenType::MINUS_MINUS: return "--";
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
    case TokenType::AMPERSAND: return "&";
    case TokenType::PIPE: return "|";
    case TokenType::CARET: return "^";
    case TokenType::TILDE: return "~";
    case TokenType::LESS_LESS: return "<<";
    case TokenType::GREATER_GREATER: return ">>";
    case TokenType::EQUAL: return "=";
    case TokenType::PLUS_EQUAL: return "+=";
    case TokenType::MINUS_EQUAL: return "-=";
    case TokenType::STAR_EQUAL: return "*=";
    case TokenType::SLASH_EQUAL: return "/=";
    case TokenType::PERCENT_EQUAL: return "%=";
    case TokenType::AMPERSAND_EQUAL: return "&=";
    case TokenType::PIPE_EQUAL: return "|=";
    case TokenType::CARET_EQUAL: return "^=";
    case TokenType::LESS_LESS_EQUAL: return "<<=";
    case TokenType::GREATER_GREATER_EQUAL: return ">>=";
    case TokenType::ARROW: return "=>";
    case TokenType::THIN_ARROW: return "->";
    case TokenType::DOT: return ".";
    case TokenType::DOT_DOT: return "..";
    case TokenType::DOT_DOT_EQUAL: return "..=";
    case TokenType::DOT_DOT_DOT: return "...";
    case TokenType::COLON: return ":";
    case TokenType::COLON_COLON: return "::";
    case TokenType::LPAREN: return "(";
    case TokenType::RPAREN: return ")";
    case TokenType::LBRACE: return "{";
    case TokenType::RBRACE: return "}";
    case TokenType::LBRACKET: return "[";
    case TokenType::RBRACKET: return "]";
    case TokenType::SEMICOLON: return ";";
    case TokenType::COMMA: return ",";
    case TokenType::AT: return "@";
    case TokenType::IF: return "if";
    case TokenType::ELSE: return "else";
    case TokenType::FOR: return "for";
    case TokenType::WHILE: return "while";
    case TokenType::DO: return "do";
    case TokenType::SWITCH: return "switch";
    case TokenType::CASE: return "case";
    case TokenType::DEFAULT: return "default";
    case TokenType::BREAK: return "break";
    case TokenType::CONTINUE: return "continue";
    case TokenType::RETURN: return "return";
    case TokenType::STRUCT: return "struct";
    case TokenType::ENUM: return "enum";
    case TokenType::INTERFACE: return "interface";
    case TokenType::NAMESPACE: return "namespace";
    case TokenType::IMPORT: return "import";
    case TokenType::EXTERN: return "extern";
    case TokenType::MUT: return "mut";
    case TokenType::PUBLIC: return "public";
    case TokenType::PRIVATE: return "private";
    case TokenType::STATIC: return "static";
    case TokenType::THIS: return "this";
    case TokenType::NEW: return "new";
    case TokenType::IMPL: return "impl";
    case TokenType::WHERE: return "where";
    case TokenType::ASYNC: return "async";
    case TokenType::AWAIT: return "await";
    case TokenType::IN: return "in";
    case TokenType::YIELD: return "yield";
    case TokenType::SPAWN: return "spawn";
    case TokenType::OPERATOR: return "operator";
    case TokenType::CONST_EXPR: return "constexpr";
    case TokenType::CONST_EVAL: return "consteval";
    case TokenType::CONST: return "const";
    case TokenType::INT: return "int";
    case TokenType::UINT: return "uint";
    case TokenType::VOID: return "void";
    case TokenType::FLOAT: return "float";
    case TokenType::STRING: return "string";
    case TokenType::AUTO: return "auto";
    case TokenType::TRUE: return "true";
    case TokenType::FALSE: return "false";
    case TokenType::IDENTIFIER: return "identifier";
    case TokenType::STRING_LITERAL: return "string_literal";
    case TokenType::INTEGER_LITERAL: return "integer_literal";
    case TokenType::FLOAT_LITERAL: return "float_literal";
    case TokenType::IS: return "is";
    case TokenType::END_OF_FILE: return "EOF";
    default: return "?";
    }
}

constexpr std::string operatorTypeToHumanString(const TokenType type)
{
    switch (type)
    {
    case TokenType::PLUS: return "+ (Addition)";
    case TokenType::MINUS: return "- (Subtraction)";
    case TokenType::STAR: return "* (Multiplication)";
    case TokenType::SLASH: return "/ (Division)";
    case TokenType::PERCENT: return "% (Modulo)";

    case TokenType::EQUAL_EQUAL: return "== (Equality)";
    case TokenType::BANG_EQUAL: return "!= (Inequality)";

    case TokenType::LESS: return "< (Less Than)";
    case TokenType::LESS_EQUAL: return "<= (Less Than or Equal)";
    case TokenType::GREATER: return "> (Greater Than)";
    case TokenType::GREATER_EQUAL: return ">= (Greater Than or Equal)";

    case TokenType::AMPERSAND: return "& (Bitwise AND)";
    case TokenType::PIPE: return "| (Bitwise OR)";
    case TokenType::CARET: return "^ (Bitwise XOR)";
    case TokenType::LESS_LESS: return "<< (Left Shift)";
    case TokenType::GREATER_GREATER: return ">> (Right Shift)";

    case TokenType::AND_AND: return "&& (Logical AND)";
    case TokenType::OR_OR: return "|| (Logical OR)";

    default: return "invalid";
    }
}

constexpr std::string tokenTypeToHumanString(const TokenType type)
{
    switch (type)
    {
    case TokenType::PLUS: return "Plus(+)";
    case TokenType::MINUS: return "Minus(-)";
    case TokenType::PLUS_PLUS: return "Increment(++)";
    case TokenType::MINUS_MINUS: return "Decrement(--)";
    case TokenType::STAR: return "Star(*)";
    case TokenType::SLASH: return "Slash(/)";
    case TokenType::PERCENT: return "Modulo(%)";
    case TokenType::EQUAL_EQUAL: return "Equal(==)";
    case TokenType::BANG_EQUAL: return "NotEqual(!=)";
    case TokenType::LESS: return "LessThan(<)";
    case TokenType::LESS_EQUAL: return "LessOrEqual(<=)";
    case TokenType::GREATER: return "GreaterThan(>)";
    case TokenType::GREATER_EQUAL: return "GreaterOrEqual(>=)";
    case TokenType::BANG: return "Not(!)";
    case TokenType::AND_AND: return "LogicalAnd(&&)";
    case TokenType::OR_OR: return "LogicalOr(||)";
    case TokenType::AMPERSAND: return "BitwiseAnd(&)";
    case TokenType::PIPE: return "BitwiseOr(|)";
    case TokenType::CARET: return "BitwiseXor(^)";
    case TokenType::TILDE: return "BitwiseNot(~)";
    case TokenType::LESS_LESS: return "LeftShift(<<)";
    case TokenType::GREATER_GREATER: return "RightShift(>>)";
    case TokenType::EQUAL: return "Assign(=)";
    case TokenType::PLUS_EQUAL: return "PlusAssign(+=)";
    case TokenType::MINUS_EQUAL: return "MinusAssign(-=)";
    case TokenType::STAR_EQUAL: return "StarAssign(*=)";
    case TokenType::SLASH_EQUAL: return "SlashAssign(/=)";
    case TokenType::PERCENT_EQUAL: return "ModuloAssign(%=)";
    case TokenType::AMPERSAND_EQUAL: return "AndAssign(&=)";
    case TokenType::PIPE_EQUAL: return "OrAssign(|=)";
    case TokenType::CARET_EQUAL: return "XorAssign(^=)";
    case TokenType::LESS_LESS_EQUAL: return "LeftShiftAssign(<<=)";
    case TokenType::GREATER_GREATER_EQUAL: return "RightShiftAssign(>>=)";
    case TokenType::ARROW: return "Arrow(=>)";
    case TokenType::THIN_ARROW: return "ThinArrow(->)";
    case TokenType::DOT: return "Dot(.)";
    case TokenType::DOT_DOT: return "Range(..)";
    case TokenType::DOT_DOT_EQUAL: return "RangeInclusive(..=)";
    case TokenType::DOT_DOT_DOT: return "Variadics(...)";
    case TokenType::COLON: return "Colon(:)";
    case TokenType::COLON_COLON: return "Scope(::)";
    default: return tokenTypeToString(type);
    }
}

struct Expression : Location
{
    virtual void accept(djinn::IExpressionVisitor& visitor) const = 0;
};

struct VariableDeclaration : Expression
{
    Type type;
    SourceIdentifier name;
    bool isMutable;

    VariableDeclaration(Type type, SourceIdentifier name, const bool isMutable)
        : type(std::move(type)),
          name(std::move(name)), isMutable(isMutable)
    {
        location = name.location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent) const override
    {
        writeIndent(os, indent);
        os << "VariableDeclaration(" << name.token_name << ": " << type << (isMutable ? ": mut" : "") << ")";
    }
};

struct Assignment : Expression
{
    SourceIdentifier name;
    std::unique_ptr<Expression> value;

    Assignment(SourceIdentifier name, std::unique_ptr<Expression> value)
        : name(std::move(name)), value(std::move(value))
    {
        location = name.location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent) const override
    {
        writeIndent(os, indent);
        os << "Assignment(" << name.token_name << " =\n";
        value->print(os, indent + 2);
        os << ")";
    }
};

struct VariableInit : Expression
{
    Type type;
    SourceIdentifier name;
    std::unique_ptr<Expression> value;
    bool isMutable;

    VariableInit(Type type, SourceIdentifier name, std::unique_ptr<Expression> value, bool isMutable)
        : type(std::move(type)), name(std::move(name)), value(std::move(value)), isMutable(isMutable)
    {
        location = name.location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent) const override
    {
        writeIndent(os, indent);
        os << "VariableInit(" << name.token_name << ": " << type << " =\n";
        value->print(os, indent + 2);
        os << ")";
    }
};

struct StringLiteral : Expression
{
    std::string value;

    explicit StringLiteral(std::string val, const SourceLocation& location) : value(std::move(val))
    {
        this->location = location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "StringLiteral(\"" << string_utils::escape_visible(value) << "\")";
    }
};

struct IntegerLiteral : Expression
{
    std::string value;
    bool sign;

    explicit IntegerLiteral(std::string val, const bool sign, const SourceLocation& location) : value(std::move(val)),
        sign(sign)
    {
        this->location = location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "IntegerLiteral(" << value << ")";
    }
};

struct FloatLiteral : Expression
{
    std::string value;

    explicit FloatLiteral(std::string val, const SourceLocation& location) : value(std::move(val))
    {
        this->location = location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "FloatLiteral(" << value << ")";
    }
};

struct BooleanLiteral : Expression
{
    std::string value;

    explicit BooleanLiteral(std::string val, const SourceLocation& location) : value(std::move(val))
    {
        this->location = location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "BooleanLiteral(" << value << ")";
    }
};

struct NullLiteral : Expression
{
    explicit NullLiteral(const SourceLocation& location)
    {
        this->location = location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "NullLiteral";
    }
};

struct Identifier : Expression
{
    SourceIdentifier identifier;

    explicit Identifier(const SourceIdentifier& source_identifier)
        : identifier(source_identifier)
    {
        this->location = source_identifier.location;
    }

    [[nodiscard]] const std::string& name() const { return identifier.token_name; }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "Identifier(" << identifier.token_name << ")";
    }
};

enum class FieldAccessKind : uint8_t
{
    Normal, // a.b
    NullConditional, // a?.b
    NullForgiving // a!.b
};

struct FieldAccess : Expression
{
    std::unique_ptr<Expression> object;
    SourceIdentifier fieldName;
    FieldAccessKind accessKind = FieldAccessKind::Normal;

    FieldAccess(std::unique_ptr<Expression> obj, SourceIdentifier field)
        : object(std::move(obj)), fieldName(std::move(field))
    {
        location = field.location;
    }

    FieldAccess(std::unique_ptr<Expression> obj, SourceIdentifier field, FieldAccessKind kind)
        : object(std::move(obj)), fieldName(std::move(field)), accessKind(kind)
    {
        location = field.location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        const char* op = accessKind == FieldAccessKind::NullConditional
                             ? "?."
                             : accessKind == FieldAccessKind::NullForgiving
                             ? "!."
                             : ".";
        os << "FieldAccess(" << op << fieldName.token_name << ")\n";
        object->print(os, indent + 2);
    }
};

struct FieldAssignment : Expression
{
    std::unique_ptr<Expression> object;
    SourceIdentifier fieldName;
    std::unique_ptr<Expression> value;

    FieldAssignment(std::unique_ptr<Expression> obj, SourceIdentifier field, std::unique_ptr<Expression> val)
        : object(std::move(obj)), fieldName(std::move(field)), value(std::move(val))
    {
        location = field.location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "FieldAssignment(." << fieldName.token_name << " =\n";
        object->print(os, indent + 2);
        os << "\n";
        value->print(os, indent + 2);
        os << ")";
    }
};

struct FunctionCall : Expression
{
    SourceIdentifier name;
    std::vector<std::unique_ptr<Expression>> arguments;
    std::unique_ptr<Expression> receiver; // optional: for method calls (object.method())
    std::vector<Type> typeArguments; // generic type arguments: Result<i32, string*>::Ok(...)
    bool hasVariadicForward = false; // true if call includes ... to forward variadic args
    mutable std::string typeofResolvedName; // set by binder for typeof() intrinsic

    FunctionCall(SourceIdentifier n, std::vector<std::unique_ptr<Expression>> args)
        : name(std::move(n)), arguments(std::move(args))
    {
        location = n.location;
    }

    FunctionCall(SourceIdentifier n, std::vector<std::unique_ptr<Expression>> args, std::unique_ptr<Expression> recv)
        : name(std::move(n)), arguments(std::move(args)), receiver(std::move(recv))
    {
        location = n.location;
    }

    FunctionCall(SourceIdentifier n, std::vector<Type> typeArgs, std::vector<std::unique_ptr<Expression>> args)
        : name(std::move(n)), arguments(std::move(args)), typeArguments(std::move(typeArgs))
    {
        location = n.location;
    }

    [[nodiscard]] bool isMethodCall() const { return receiver != nullptr; }
    [[nodiscard]] bool hasTypeArguments() const { return !typeArguments.empty(); }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        if (isMethodCall())
        {
            os << "MethodCall(." << name.token_name << ")\n";
            receiver->print(os, indent + 2);
            os << '\n';
        }
        else
        {
            os << "FunctionCall(" << name.token_name;
            if (!typeArguments.empty())
            {
                os << "<";
                for (size_t i = 0; i < typeArguments.size(); ++i)
                {
                    if (i > 0) os << ", ";
                    typeArguments[i].print(os, 0);
                }
                os << ">";
            }
            os << ")\n";
        }
        for (const auto& arg : arguments)
        {
            arg->print(os, indent + 2);
            os << '\n';
        }
    }
};

struct UnaryExpression : Expression
{
    TokenType op;
    std::unique_ptr<Expression> operand;

    UnaryExpression(const TokenType op, std::unique_ptr<Expression> operand, const SourceLocation& location)
        : op(op), operand(std::move(operand))
    {
        this->location = location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "UnaryExpression(" << tokenTypeToString(op) << ")\n";
        operand->print(os, indent + 2);
    }
};

struct PostfixExpression : Expression
{
    TokenType op;
    std::unique_ptr<Expression> operand;

    PostfixExpression(const TokenType op, std::unique_ptr<Expression> operand, const SourceLocation& location)
        : op(op), operand(std::move(operand))
    {
        this->location = location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "PostfixExpression(" << tokenTypeToString(op) << ")\n";
        operand->print(os, indent + 2);
    }
};

struct BinaryExpression : Expression
{
    std::unique_ptr<Expression> left;
    TokenType op;
    std::unique_ptr<Expression> right;

    BinaryExpression(
        std::unique_ptr<Expression> left,
        const TokenType op,
        std::unique_ptr<Expression> right,
        const SourceLocation& loc
    )
        : left(std::move(left)), op(op), right(std::move(right))
    {
        location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "BinaryExpression(" << tokenTypeToString(op) << ")\n";
        left->print(os, indent + 2);
        os << '\n';
        right->print(os, indent + 2);
    }
};

struct InitializerElement : Location
{
    SourceIdentifier fieldName;
    std::unique_ptr<Expression> value;

    InitializerElement(SourceIdentifier fieldName, std::unique_ptr<Expression> value)
        : fieldName(std::move(fieldName)), value(std::move(value))
    {
        location = fieldName.location;
    }

    explicit InitializerElement(std::unique_ptr<Expression> value)
        : fieldName(), value(std::move(value))
    {
        if (this->value) location = this->value->location;
    }

    [[nodiscard]] bool isDesignated() const { return !fieldName.token_name.empty(); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        if (isDesignated())
        {
            os << "DesignatedInit(." << fieldName.token_name << " =\n";
        }
        else
        {
            os << "PositionalInit(\n";
        }
        value->print(os, indent + 2);
        os << ")";
    }
};

struct BraceInitializer : Expression
{
    std::vector<InitializerElement> elements;

    explicit BraceInitializer(std::vector<InitializerElement> elements, const SourceLocation& loc)
        : elements(std::move(elements))
    {
        location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "BraceInitializer {\n";
        for (const auto& elem : elements)
        {
            elem.print(os, indent + 2);
            os << '\n';
        }
        writeIndent(os, indent);
        os << "}";
    }
};

struct IndexAccess : Expression
{
    std::unique_ptr<Expression> object;
    std::unique_ptr<Expression> index;

    explicit IndexAccess(std::unique_ptr<Expression> obj, std::unique_ptr<Expression> idx,
                         const SourceLocation& loc)
        : object(std::move(obj)), index(std::move(idx))
    {
        location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "IndexAccess[\n";
        object->print(os, indent + 2);
        os << "\n";
        index->print(os, indent + 2);
        os << "]";
    }
};

struct IndexAssignment : Expression
{
    std::unique_ptr<Expression> object;
    std::unique_ptr<Expression> index;
    std::unique_ptr<Expression> value;

    IndexAssignment(
        std::unique_ptr<Expression> obj,
        std::unique_ptr<Expression> idx,
        std::unique_ptr<Expression> val,
        const SourceLocation& loc
    )
        : object(std::move(obj)), index(std::move(idx)), value(std::move(val))
    {
        this->location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
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
struct SwitchArm : Location
{
    SourceIdentifier variantName; // The variant to match (e.g., "Value", "Empty")
    std::optional<SourceIdentifier> binding; // Optional binding name (e.g., "val" in "Value val")
    std::unique_ptr<Expression> result; // The result expression

    SwitchArm(SourceIdentifier variant, std::optional<SourceIdentifier> bind, std::unique_ptr<Expression> res)
        : variantName(std::move(variant)), binding(std::move(bind)), result(std::move(res))
    {
        location = variant.location;
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "SwitchArm(" << variantName.token_name;
        if (binding)
        {
            os << " " << binding->token_name;
        }
        os << " ->\n";
        result->print(os, indent + 2);
        os << ")";
    }
};

// Pattern matching switch expression: switch expr { arms... }
struct SwitchExpression : Expression
{
    std::unique_ptr<Expression> value; // The enum value being matched
    std::vector<SwitchArm> arms; // The match arms

    explicit SwitchExpression(
        std::unique_ptr<Expression> val,
        std::vector<SwitchArm> a,
        const SourceLocation& loc
    )
        : value(std::move(val)), arms(std::move(a))
    {
        this->location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "SwitchExpression\n";
        value->print(os, indent + 2);
        os << "\n";
        for (const auto& arm : arms)
        {
            arm.print(os, indent + 2);
            os << "\n";
        }
    }
};

// Variadic forwarding: ... (forwards variadic arguments to another variadic function)
struct VariadicForward : Expression
{
    VariadicForward() = default;

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "VariadicForward(...)";
    }
};

// Array literal: [1, 2, 3] or i32[1, 2, 3]
struct ArrayLiteral : Expression
{
    std::vector<std::unique_ptr<Expression>> elements;
    std::optional<Type> elementType; // set for typed literals like i32[1,2,3], empty for [1,2,3]
    bool isHeap = false; // set by NewExpression wrapping

    explicit ArrayLiteral(
        std::vector<std::unique_ptr<Expression>> elements,
        const std::optional<Type>& elementType,
        const SourceLocation& loc
    )
        : elements(std::move(elements)), elementType(elementType)
    {
        this->location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "ArrayLiteral(";
        if (elementType)
        {
            elementType->print(os, 0);
        }
        os << "[" << elements.size() << "])\n";
        for (const auto& elem : elements)
        {
            elem->print(os, indent + 2);
            os << "\n";
        }
    }
};

// Fixed-size array allocation: type[length] e.g. i8[8192]
struct FixedArrayExpression : Expression
{
    Type elementType;
    std::unique_ptr<Expression> sizeExpr;

    FixedArrayExpression(Type elemType, std::unique_ptr<Expression> size, const SourceLocation& loc)
        : elementType(std::move(elemType)), sizeExpr(std::move(size))
    {
        this->location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "FixedArrayExpression(";
        elementType.print(os, 0);
        os << "[";
        sizeExpr->print(os, 0);
        os << "])";
    }
};

// Heap-allocated constructor call: new StructName(args)
struct NewExpression : Expression
{
    std::unique_ptr<FunctionCall> constructorCall;

    explicit NewExpression(std::unique_ptr<FunctionCall> call)
        : constructorCall(std::move(call))
    {
        location = constructorCall->location;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "NewExpression(\n";
        constructorCall->print(os, indent + 2);
        os << ")";
    }
};

struct AwaitExpression : Expression
{
    std::unique_ptr<Expression> operand;

    explicit AwaitExpression(std::unique_ptr<Expression> operand, const SourceLocation& loc)
        : operand(std::move(operand))
    {
        location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "AwaitExpression\n";
        operand->print(os, indent + 2);
    }
};

struct CastExpression : Expression
{
    Type targetType;
    std::unique_ptr<Expression> operand;

    CastExpression(Type targetType, std::unique_ptr<Expression> operand, const SourceLocation& loc)
        : targetType(std::move(targetType)), operand(std::move(operand))
    {
        this->location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "CastExpression(" << targetType << ")\n";
        operand->print(os, indent + 2);
    }
};

struct IsExpression : Expression
{
    std::unique_ptr<Expression> operand;
    Type targetType;
    std::optional<std::string> bindingName;

    IsExpression(std::unique_ptr<Expression> operand, Type targetType, const SourceLocation& loc,
                 std::optional<std::string> bindingName = std::nullopt)
        : operand(std::move(operand)), targetType(std::move(targetType)), bindingName(std::move(bindingName))
    {
        this->location = loc;
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "IsExpression(" << targetType;
        if (bindingName) os << " " << *bindingName;
        os << ")\n";
        operand->print(os, indent + 2);
    }
};

struct MacroExpansionExpression : Expression
{
    struct LocalBinding
    {
        std::string varName;
        std::unique_ptr<Expression> value;
    };

    std::vector<LocalBinding> locals;
    std::unique_ptr<Expression> body;

    MacroExpansionExpression(std::vector<LocalBinding> locals, std::unique_ptr<Expression> body)
        : locals(std::move(locals)), body(std::move(body))
    {
    }

    void accept(djinn::IExpressionVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "MacroExpansion(";
        for (size_t i = 0; i < locals.size(); i++)
        {
            if (i > 0) os << ", ";
            os << locals[i].varName << " = ";
            locals[i].value->print(os, 0);
        }
        os << ")\n";
        body->print(os, indent + 2);
    }
};

#endif //DJINN_EXPRESSION_H
