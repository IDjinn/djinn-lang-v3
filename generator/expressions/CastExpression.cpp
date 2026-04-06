//
// Cast Expression - C-style explicit cast: (Type)expr
//

#include "../Generator.h"

llvm::Value* Generator::generate_cast_expression(const CastExpression& expr)
{
    llvm::Value* value = generate_expression(*expr.operand);

    // Explicit cast to object: box the value
    if (expr.targetType.kind == TypeKind::STRUCT &&
        (expr.targetType.structName == "object" || expr.targetType.structName == "std::types::object"))
    {
        llvm::Type* objectLlvmType = generate_type(expr.targetType);
        if (value->getType() != objectLlvmType)
        {
            return box_value(value, get_djinn_type_name(*expr.operand, value));
        }
    }

    llvm::Type* targetType = _currentGenericCtx
                                 ? generate_type_with_context(expr.targetType, _currentGenericCtx)
                                 : generate_type(expr.targetType);

    return cast_value(value, targetType, expr.targetType.sign);
}