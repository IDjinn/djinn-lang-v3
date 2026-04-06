//
// 'is' expression — runtime type check for boxed object values
//

#include "../Generator.h"

llvm::Value* Generator::generate_is_expression(const IsExpression& expr)
{
    auto* operand = generate_expression(*expr.operand);
    if (!operand)
        return nullptr;

    // Resolve object struct type
    const std::string resolvedObject = currentScope->resolve_alias("object");
    StructDef* objectDef = currentScope->lookup_struct(resolvedObject);
    if (!objectDef || !objectDef->llvmType)
    {
        GENERATOR_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                        "object struct not found - ensure std::types is imported",
                        expr.location);
    }

    // Resolve TypeInfo struct type
    const std::string resolvedTypeInfo = currentScope->resolve_alias("TypeInfo");
    StructDef* typeInfoDef = currentScope->lookup_struct(resolvedTypeInfo);
    if (!typeInfoDef || !typeInfoDef->llvmType)
    {
        GENERATOR_ERROR(DiagnosticCode::UNDEFINED_STRUCT,
                        "TypeInfo struct not found - ensure std::types is imported",
                        expr.location);
    }

    // Store object to stack so we can GEP into it
    auto* objAlloca = builder->CreateAlloca(objectDef->llvmType, nullptr, "is_obj");
    builder->CreateStore(operand, objAlloca);

    // Extract TypeInfo* (field 0 of object)
    auto* typeInfoPtrField = builder->CreateStructGEP(objectDef->llvmType, objAlloca, 0, "is_typeinfo_ptr");
    auto* typeInfoPtr = builder->CreateLoad(builder->getPtrTy(), typeInfoPtrField, "is_typeinfo");

    // Extract id (field 0 of TypeInfo: i32)
    auto* idField = builder->CreateStructGEP(typeInfoDef->llvmType, typeInfoPtr, 0, "is_id_ptr");
    auto* loadedId = builder->CreateLoad(builder->getInt32Ty(), idField, "is_id");

    // Compute expected type id at compile time
    const std::string targetTypeName = expr.targetType.toHumanString();
    const int32_t expectedId = compute_type_id(targetTypeName);

    // If binding name is present, extract data pointer and cast to target type
    if (expr.bindingName)
    {
        // Extract void* data (field 1 of object)
        auto* dataPtrField = builder->CreateStructGEP(objectDef->llvmType, objAlloca, 1, "is_data_ptr");
        auto* dataPtr = builder->CreateLoad(builder->getPtrTy(), dataPtrField, "is_data");

        // Resolve target LLVM type and load the value from data pointer
        llvm::Type* targetLLVMType = generate_type(expr.targetType);
        auto* bindingAlloca = builder->CreateAlloca(targetLLVMType, nullptr, *expr.bindingName);
        auto* loadedValue = builder->CreateLoad(targetLLVMType, dataPtr, *expr.bindingName + "_val");
        builder->CreateStore(loadedValue, bindingAlloca);
        currentScope->define_variable(*expr.bindingName, bindingAlloca);
    }

    return builder->CreateICmpEQ(loadedId, builder->getInt32(static_cast<uint32_t>(expectedId)), "is_check");
}
