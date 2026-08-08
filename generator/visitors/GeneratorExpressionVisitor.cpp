//
// Generator Expression Visitor Implementation
//

#include "GeneratorExpressionVisitor.h"
#include "../Generator.h"
#include "../../parser/ast/Expression.h"

namespace djinn
{
    void GeneratorExpressionVisitor::visit(const IntegerLiteral& expr)
    {
        _result = _generator.generate_integer_literal(expr);
    }

    void GeneratorExpressionVisitor::visit(const FloatLiteral& expr)
    {
        _result = _generator.generate_float_literal(expr);
    }

    void GeneratorExpressionVisitor::visit(const StringLiteral& expr)
    {
        _result = _generator.generate_string_literal(expr);
    }

    void GeneratorExpressionVisitor::visit(const BooleanLiteral& expr)
    {
        _result = llvm::ConstantInt::get(_generator.builder->getInt1Ty(), expr.value == "true" ? 1 : 0);
    }

    void GeneratorExpressionVisitor::visit(const NullLiteral&)
    {
        _result = llvm::ConstantPointerNull::get(
            llvm::PointerType::get(_generator.builder->getInt8Ty(), 0));
    }

    void GeneratorExpressionVisitor::visit(const Identifier& expr)
    {
        _result = _generator.generate_identifier(expr);
    }

    void GeneratorExpressionVisitor::visit(const FunctionCall& expr)
    {
        _result = _generator.generate_function_call(expr);
    }

    void GeneratorExpressionVisitor::visit(const FieldAccess& expr)
    {
        _result = _generator.generate_field_access(expr);
    }

    void GeneratorExpressionVisitor::visit(const FieldAssignment& expr)
    {
        _result = _generator.generate_field_assignment(expr);
    }

    void GeneratorExpressionVisitor::visit(const BinaryExpression& expr)
    {
        _result = _generator.generate_binary_expression(expr);
    }

    void GeneratorExpressionVisitor::visit(const UnaryExpression& expr)
    {
        _result = _generator.generate_unary_expression(expr);
    }

    void GeneratorExpressionVisitor::visit(const PostfixExpression& expr)
    {
        _result = _generator.generate_postfix_expression(expr);
    }

    void GeneratorExpressionVisitor::visit(const VariableDeclaration& expr)
    {
        _result = _generator.generate_variable_declaration(expr);
    }

    void GeneratorExpressionVisitor::visit(const VariableInit& expr)
    {
        _result = _generator.generate_variable_init(expr);
    }

    void GeneratorExpressionVisitor::visit(const Assignment& expr)
    {
        _result = _generator.generate_assignment(expr);
    }

    void GeneratorExpressionVisitor::visit(const BraceInitializer& expr)
    {
        _result = _generator.generate_brace_initializer(expr);
    }

    void GeneratorExpressionVisitor::visit(const SwitchExpression& expr)
    {
        _result = _generator.generate_switch_expression(expr);
    }

    void GeneratorExpressionVisitor::visit(const VariadicForward&)
    {
        throw CompileError(DiagnosticCode::UNEXPECTED_TOKEN,
                           "variadic forwarding '...' can only be used as function argument");
    }

    void GeneratorExpressionVisitor::visit(const NewExpression& expr)
    {
        _result = _generator.generate_new_expression(expr);
    }

    void GeneratorExpressionVisitor::visit(const ArrayLiteral& expr)
    {
        _result = _generator.generate_array_literal(expr);
    }

    void GeneratorExpressionVisitor::visit(const IndexAccess& expr)
    {
        _result = _generator.generate_index_access(expr);
    }

    void GeneratorExpressionVisitor::visit(const IndexAssignment& expr)
    {
        _result = _generator.generate_index_assignment(expr);
    }

    void GeneratorExpressionVisitor::visit(const CastExpression& expr)
    {
        _result = _generator.generate_cast_expression(expr);
    }

    void GeneratorExpressionVisitor::visit(const AwaitExpression& expr)
    {
        _result = _generator.generate_await_expression(expr);
    }

    void GeneratorExpressionVisitor::visit(const FixedArrayExpression& expr)
    {
        _result = _generator.generate_fixed_array(expr);
    }

    void GeneratorExpressionVisitor::visit(const IsExpression& expr)
    {
        _result = _generator.generate_is_expression(expr);
    }

    void GeneratorExpressionVisitor::visit(const MacroExpansionExpression& expr)
    {
        _result = _generator.generate_macro_expansion(expr);
    }
} // namespace djinn