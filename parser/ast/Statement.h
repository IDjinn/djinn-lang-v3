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

#endif //DJINN_STATEMENT_H