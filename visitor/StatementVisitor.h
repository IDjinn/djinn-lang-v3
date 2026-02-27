//
// Statement Visitor Interface - Base for all statement visitors
//

#ifndef DJINN_STATEMENT_VISITOR_H
#define DJINN_STATEMENT_VISITOR_H

// Forward declarations of all statement types (global namespace)
struct ExpressionStatement;
struct ReturnStatement;
struct Block;
struct IfStatement;
struct ForStatement;
struct WhileStatement;
struct DoWhileStatement;
struct SwitchStatement;
struct BreakStatement;
struct ContinueStatement;
struct YieldStatement;

namespace djinn
{
    class IStatementVisitor
    {
    public:
        virtual ~IStatementVisitor() = default;

        virtual void visit(const ExpressionStatement& stmt) = 0;

        virtual void visit(const ReturnStatement& stmt) = 0;

        virtual void visit(const Block& stmt) = 0;

        virtual void visit(const IfStatement& stmt) = 0;

        virtual void visit(const ForStatement& stmt) = 0;

        virtual void visit(const WhileStatement& stmt) = 0;

        virtual void visit(const DoWhileStatement& stmt) = 0;

        virtual void visit(const SwitchStatement& stmt) = 0;

        virtual void visit(const BreakStatement& stmt) = 0;

        virtual void visit(const ContinueStatement& stmt) = 0;

        virtual void visit(const YieldStatement& stmt) = 0;
    };
} // namespace djinn

#endif // DJINN_STATEMENT_VISITOR_H