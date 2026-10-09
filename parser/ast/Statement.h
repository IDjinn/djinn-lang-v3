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

// One catch arm of a block-form try: an error type (deriving from Exception),
// the catch-all "Error", or the wildcard "_" — with an optional binding —
// followed by the handler body.
struct CatchClause : Location
{
    SourceIdentifier errorType;
    std::optional<SourceIdentifier> binding;
    std::unique_ptr<Block> body;

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "Catch(" << errorType.token_name;
        if (binding) os << " " << binding->token_name;
        os << ")\n";
        if (body) body->print(os, indent + 2);
    }
};

// Block-form try/catch/finally (opt-in via --exceptions). Complements the
// expression forms (`try e`, `try e ?: f`, outcome switch) which stay
// available in every mode.
struct TryCatchStatement : Statement
{
    std::unique_ptr<Block> tryBlock;
    std::vector<CatchClause> catches;
    std::unique_ptr<Block> finallyBlock;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "TryCatchStatement\n";
        if (tryBlock) tryBlock->print(os, indent + 2);
        for (const auto& clause : catches)
        {
            clause.print(os, indent + 2);
        }
        if (finallyBlock)
        {
            writeIndent(os, indent + 2);
            os << "Finally\n";
            finallyBlock->print(os, indent + 4);
        }
    }
};

// Forward declaration: defined in Declaration.h (which includes this header).
enum class UowPhase : int;

// Inline uow lifecycle block inside a uow member body (`before_commit { }` /
// `after_commit { }`, VERIFICATION-SPEC.md §10). Real, compiled code: the
// statements are deferred to the member's success path (after_commit) or run
// just before commit (before_commit); any unwind skips them entirely.
struct UowPhaseBlockStatement : Statement
{
    UowPhase phase;
    std::unique_ptr<Block> body;

    explicit UowPhaseBlockStatement(UowPhase phase, std::unique_ptr<Block> body)
        : phase(phase), body(std::move(body))
    {
    }

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "UowPhaseBlock\n";
        if (body) body->print(os, indent + 2);
    }
};

// `lock (a, b) { ... }`: acquires each operand's object in listed order,
// releases in reverse on every exit path (normal or unwind). The verifier
// uses lock scopes as the discharge evidence for `atomic`/`exclusive` claims.
struct LockStatement : Statement
{
    std::vector<std::unique_ptr<Expression>> operands;
    std::unique_ptr<Block> body;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "LockStatement\n";
        for (const auto& operand : operands) operand->print(os, indent + 2);
        if (body) body->print(os, indent + 2);
    }
};

// `commit;` inside a uow member body: ends the transaction portion. Every
// statement after it (block or not, explicit `after_commit` block or not)
// runs in the member's after-commit window — success-only, skipped by any
// unwind before the commit point.
struct CommitStatement : Statement
{
    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "CommitStatement\n";
    }
};

// `rollback;` / `rollback (a.b, c.d);` — restores entry snapshots of the
// listed dotted paths (all captured paths when bare). Only valid inside the
// catch arm of a uow member; snapshots are taken at member entry.
struct RollbackStatement : Statement
{
    std::vector<std::vector<std::string>> paths;
    SourceLocation keywordLocation;

    void accept(djinn::IStatementVisitor& visitor) const override { visitor.visit(*this); }

    void print(std::ostream& os, const int indent = 0) const override
    {
        writeIndent(os, indent);
        os << "RollbackStatement";
        for (const auto& path : paths)
        {
            os << " ";
            for (size_t i = 0; i < path.size(); ++i)
            {
                if (i > 0) os << ".";
                os << path[i];
            }
        }
        os << "\n";
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