//
// Created by Luke on 06/12/2025.
//

#ifndef DJINN_STATEMENT_H
#define DJINN_STATEMENT_H

#include <vector>
#include <memory>
#include "ASTNode.h"
#include "Expression.h"

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

struct IfStatement : Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> thenBranch;
    std::unique_ptr<Block> elseBranch;

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "IfStatement\n";
        if (condition) condition->print(os, indent + 2);
        if (thenBranch) thenBranch->print(os, indent + 2);
        if (elseBranch) elseBranch->print(os, indent + 2);
    }
};

struct ForStatement : Statement {
    std::unique_ptr<Expression> initializer;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> postfix;
    std::unique_ptr<Block> body;

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "ForStatement\n";
        initializer->print(os, indent + 2);
        if (condition) condition->print(os, indent + 2);
        if (postfix) postfix->print(os, indent + 2);
        if (body) body->print(os, indent + 2);
    }
};

struct WhileStatement : Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> body;

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "WhileStatement\n";
        condition->print(os, indent + 2);
        if (body) body->print(os, indent + 2);
    }
};

struct DoWhileStatement : Statement {
    std::unique_ptr<Block> body;
    std::unique_ptr<Expression> condition;

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "DoWhileStatement\n";
        if (body) body->print(os, indent + 2);
        condition->print(os, indent + 2);
    }
};

struct BreakStatement : Statement {
    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "BreakStatement\n";
    }
};

struct ContinueStatement : Statement {
    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "ContinueStatement\n";
    }
};

struct SwitchCaseStatement : Statement {
    std::unique_ptr<Expression> expression;
    std::unique_ptr<Block> body;

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "SwitchCaseStatement\n";
        if (expression) expression->print(os, indent + 2);
        if (body) body->print(os, indent + 2);
    }
};

struct SwitchStatement : Statement {
    std::unique_ptr<Expression> value;
    std::vector<std::unique_ptr<SwitchCaseStatement> > cases;

    void print(std::ostream &os, const int indent = 0) const override {
        writeIndent(os, indent);
        os << "SwitchStatement\n";
        for (const auto &c: cases) {
            c->print(os, indent + 2);
        }
    }
};

#endif //DJINN_STATEMENT_H