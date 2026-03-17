//
// Cast Expression - C-style explicit cast: (Type)expr
//

#include "../Generator.h"

llvm::Value* Generator::generate_cast_expression(const CastExpression& expr)
{
    llvm::Value* value = generate_expression(*expr.operand);
    llvm::Type* targetType = _currentGenericCtx
                                 ? generate_type_with_context(expr.targetType, _currentGenericCtx)
                                 : generate_type(expr.targetType);

    return cast_value(value, targetType, expr.targetType.sign);
}