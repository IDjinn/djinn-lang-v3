//
// Binary and unary operator binding
//

#include "../Binder.h"

std::shared_ptr<Symbol> Binder::bindBinaryExpression(const BinaryExpression &expr) {
    auto left = bindExpression(*expr.left);
    auto right = bindExpression(*expr.right);

    // Determine result type from operands
    Type resultType = left ? left->type : Type::voided();

    return std::make_shared<BinaryExpressionSymbol>(
        tokenTypeToString(expr.op), std::move(left), std::move(right), resultType, expr.location
    );
}

std::shared_ptr<Symbol> Binder::bindUnaryExpression(const UnaryExpression &expr) {
    auto operand = bindExpression(*expr.operand);

    Type resultType = operand ? operand->type : Type::voided();

    return std::make_shared<UnaryExpressionSymbol>(
        tokenTypeToString(expr.op), std::move(operand), resultType, expr.location
    );
}
