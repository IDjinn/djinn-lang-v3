//
// Binary and unary operator binding
//

#include "../Binder.h"

void Binder::bindBinaryExpression(const BinaryExpression &expr) {
    bindExpression(*expr.left);
    bindExpression(*expr.right);
}

void Binder::bindUnaryExpression(const UnaryExpression &expr) {
    bindExpression(*expr.operand);
}