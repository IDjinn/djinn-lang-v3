//
// Expression Visitor Interface - Base for all expression visitors
//

#ifndef DJINN_EXPRESSION_VISITOR_H
#define DJINN_EXPRESSION_VISITOR_H

// Forward declarations of all expression types (global namespace)
struct BooleanLiteral;
struct NullLiteral;
struct IntegerLiteral;
struct FloatLiteral;
struct StringLiteral;
struct Identifier;
struct FunctionCall;
struct FieldAccess;
struct FieldAssignment;
struct BinaryExpression;
struct UnaryExpression;
struct PostfixExpression;
struct VariableDeclaration;
struct VariableInit;
struct Assignment;
struct BraceInitializer;
struct SwitchExpression;
struct VariadicForward;
struct NewExpression;
struct ArrayLiteral;
struct IndexAccess;
struct IndexAssignment;
struct CastExpression;
struct AwaitExpression;
struct FixedArrayExpression;
struct IsExpression;
struct MacroExpansionExpression;
struct TernaryExpression;
struct TryExpression;

namespace djinn
{
    class IExpressionVisitor
    {
    public:
        virtual ~IExpressionVisitor() = default;

        virtual void visit(const IntegerLiteral& expr) = 0;

        virtual void visit(const FloatLiteral& expr) = 0;

        virtual void visit(const StringLiteral& expr) = 0;

        virtual void visit(const BooleanLiteral& expr) = 0;

        virtual void visit(const NullLiteral& expr) = 0;

        virtual void visit(const Identifier& expr) = 0;

        virtual void visit(const FunctionCall& expr) = 0;

        virtual void visit(const FieldAccess& expr) = 0;

        virtual void visit(const FieldAssignment& expr) = 0;

        virtual void visit(const BinaryExpression& expr) = 0;

        virtual void visit(const UnaryExpression& expr) = 0;

        virtual void visit(const PostfixExpression& expr) = 0;

        virtual void visit(const VariableDeclaration& expr) = 0;

        virtual void visit(const VariableInit& expr) = 0;

        virtual void visit(const Assignment& expr) = 0;

        virtual void visit(const BraceInitializer& expr) = 0;

        virtual void visit(const SwitchExpression& expr) = 0;

        virtual void visit(const VariadicForward& expr) = 0;

        virtual void visit(const NewExpression& expr) = 0;

        virtual void visit(const ArrayLiteral& expr) = 0;

        virtual void visit(const IndexAccess& expr) = 0;

        virtual void visit(const IndexAssignment& expr) = 0;

        virtual void visit(const CastExpression& expr) = 0;

        virtual void visit(const AwaitExpression& expr) = 0;

        virtual void visit(const FixedArrayExpression& expr) = 0;

        virtual void visit(const IsExpression& expr) = 0;

        virtual void visit(const MacroExpansionExpression& expr) = 0;

        virtual void visit(const TernaryExpression& expr) = 0;

        virtual void visit(const TryExpression& expr) = 0;
    };
} // namespace djinn

#endif // DJINN_EXPRESSION_VISITOR_H