//
// Fixed-size array allocation: type[length] e.g. i8[8192]
// Allocates [N x T] on the stack and returns pointer to first element
//

#include "../Generator.h"
#include "../../utils/Logger.h"

llvm::Value* Generator::generate_fixed_array(const FixedArrayExpression& expr)
{
    llvm::Type* elemType = generate_type(expr.elementType);

    llvm::Value* sizeVal = generate_expression(*expr.sizeExpr);
    if (!sizeVal)
    {
        GENERATOR_ERROR(DiagnosticCode::INVALID_OPERATION,
                        "failed to generate fixed array size expression",
                        expr.location);
    }

    // If size is a compile-time constant, use fixed-size array type
    if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(sizeVal))
    {
        uint64_t numElements = constInt->getZExtValue();
        llvm::Type* arrayType = llvm::ArrayType::get(elemType, numElements);
        llvm::Value* arrAlloca = builder->CreateAlloca(arrayType, nullptr, "fixed_arr");

        // Decay to pointer to first element
        llvm::Value* indices[] = {builder->getInt32(0), builder->getInt32(0)};
        return builder->CreateGEP(arrayType, arrAlloca, indices, "fixed_arr_ptr");
    }

    // Dynamic size: use alloca with count
    llvm::Value* count = cast_value(sizeVal, builder->getInt64Ty(), true);
    return builder->CreateAlloca(elemType, count, "dynamic_arr_ptr");
}