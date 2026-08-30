//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_STATEMENT_H
#define DJINN_STATEMENT_H

#include <vector>
#include <memory>
#include "ASTNode.h"
#include "Expression.h"
#include "../../visitor/StatementVisitor.h"

struct Statement : Location
{
    virtual void accept(djinn::IStatementVisitor& visitor) const = 0;
};

struct ExpressionStatement : Statement
{
    std::unique_ptr<Expression> expression;

    explicit ExpressionStatement(std::unique_ptr<Expression> expr)
        : expression(std::move(expr))
    {
    }

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "ExpressionStatement\n";
        expression->print(os, indent + 2);
    }
};

struct ReturnStatement : Statement
{
    std::unique_ptr<Expression> value;

    explicit ReturnStatement(std::unique_ptr<Expression> val)
        : value(std::move(val))
    {
    }

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "ReturnStatement\n";
        if (value) value->print(os, indent + 2);
    }
};

struct Block : Statement
{
    std::vector<std::unique_ptr<Statement>> statements;
    bool flatten = false;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "Block\n";
        for (const auto& stmt : statements)
        {
            stmt->print(os, indent + 2);
            os << '\n';
        }
    }
};

enum class CompileTimeKind : uint8_t { None, ConstExpr, ConstEval };

struct IfStatement : Statement
{
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> thenBranch;
    std::unique_ptr<Block> elseBranch;
    CompileTimeKind compileTimeKind = CompileTimeKind::None;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "IfStatement\n";
        if (condition) condition->print(os, indent + 2);
        if (thenBranch) thenBranch->print(os, indent + 2);
        if (elseBranch) elseBranch->print(os, indent + 2);
    }
};

struct ForStatement : Statement
{
    std::unique_ptr<Expression> initializer;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> postfix;
    std::unique_ptr<Block> body;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "ForStatement\n";
        initializer->print(os, indent + 2);
        if (condition) condition->print(os, indent + 2);
        if (postfix) postfix->print(os, indent + 2);
        if (body) body->print(os, indent + 2);
    }
};

struct RangeForStatement : Statement
{
    Type variableType;
    SourceIdentifier variableName;
    std::unique_ptr<Expression> start;
    std::unique_ptr<Expression> end;
    std::unique_ptr<Block> body;
    bool startInclusive = true;
    bool endInclusive = false;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "RangeForStatement(" << variableName.token_name << " in ";
        os << (startInclusive ? "[" : "(");
        start->print(os, 0);
        os << "..";
        end->print(os, 0);
        os << (endInclusive ? "]" : ")");
        os << ")\n";
        if (body) body->print(os, indent + 2);
    }
};

struct WhileStatement : Statement
{
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> body;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "WhileStatement\n";
        condition->print(os, indent + 2);
        if (body) body->print(os, indent + 2);
    }
};

struct DoWhileStatement : Statement
{
    std::unique_ptr<Block> body;
    std::unique_ptr<Expression> condition;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "DoWhileStatement\n";
        if (body) body->print(os, indent + 2);
        condition->print(os, indent + 2);
    }
};

struct BreakStatement : Statement
{
    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "BreakStatement\n";
    }
};

struct ContinueStatement : Statement
{
    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "ContinueStatement\n";
    }
};

struct YieldStatement : Statement
{
    std::unique_ptr<Expression> value; // yielded switch arm value ("yield expr;"); null = coroutine suspend

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "YieldStatement\n";
        if (value)
        {
            value->print(os, indent + 2);
        }
    }
};

struct SpawnStatement : Statement
{
    std::unique_ptr<Expression> expression;

    explicit SpawnStatement(std::unique_ptr<Expression> expr)
        : expression(std::move(expr))
    {
    }

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "SpawnStatement\n";
        if (expression) expression->print(os, indent + 2);
    }
};

struct ThrowStatement : Statement
{
    std::unique_ptr<Expression> expression;

    explicit ThrowStatement(std::unique_ptr<Expression> expr)
        : expression(std::move(expr))
    {
    }

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "ThrowStatement\n";
        if (expression) expression->print(os, indent + 2);
    }
};

struct SwitchCaseStatement : Statement
{
    std::unique_ptr<Expression> expression;
    std::unique_ptr<Block> body;

    // SwitchCaseStatement is not directly visited - it's part of SwitchStatement
    void accept(djinn::IStatementVisitor&) const override
    {
        // Not directly visited - handled by SwitchStatement visitor
    }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "SwitchCaseStatement\n";
        if (expression) expression->print(os, indent + 2);
        if (body) body->print(os, indent + 2);
    }
};

struct SwitchStatement : Statement
{
    std::unique_ptr<Expression> value;
    std::vector<std::unique_ptr<SwitchCaseStatement>> cases;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "SwitchStatement\n";
        for (const auto& c : cases)
        {
            c->print(os, indent + 2);
        }
    }
};

#endif //DJINN_STATEMENT_H