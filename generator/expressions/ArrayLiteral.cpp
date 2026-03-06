//
// Array literal code generation: [1, 2, 3] and i32[1, 2, 3]
// Now produces arr<T> slice structs { T* data, u32 len }
//

#include "../Generator.h"
#include "../../utils/Logger.h"

llvm::Value* Generator::generate_array_literal(const ArrayLiteral& expr)
{
    LOG_DEBUG("[generator] array literal: %zu elements", expr.elements.size());
    if (expr.elements.empty())
    {
        GENERATOR_ERROR(DiagnosticCode::INVALID_OPERATION,
                        "array literal must have at least one element",
                        expr.location);
    }

    // Generate all element values first
    std::vector<llvm::Value*> elementValues;
    elementValues.reserve(expr.elements.size());
    for (const auto& elem : expr.elements)
    {
        llvm::Value* val = generate_expression(*elem);
        if (!val)
        {
            GENERATOR_ERROR(DiagnosticCode::INVALID_OPERATION,
                            "failed to generate array element",
                            elem->location);
        }
        elementValues.push_back(val);
    }

    // Determine element type: from explicit type annotation or infer from first element
    llvm::Type* elemType;
    if (expr.elementType)
    {
        elemType = generate_type(*expr.elementType);
    }
    else
    {
        elemType = elementValues[0]->getType();
    }

    const uint64_t numElements = elementValues.size();
    llvm::Type* arrayType = llvm::ArrayType::get(elemType, numElements);

    llvm::Value* dataPtr;

    if (expr.isHeap)
    {
        // Heap allocation
        llvm::Function* mallocFunc = module->getFunction("__djinn_malloc");
        if (!mallocFunc)
        {
            GENERATOR_ERROR(
                DiagnosticCode::UNDEFINED_INTRINSIC_FUNCTION,
                "__djinn_malloc function could not be found in module for generate heap allocation!",
                expr.location
            );
        }

        const llvm::DataLayout& dataLayout = module->getDataLayout();
        uint64_t elemSize = dataLayout.getTypeAllocSize(elemType);
        uint64_t totalSize = elemSize * numElements;
        dataPtr = builder->CreateCall(mallocFunc, {builder->getInt64(totalSize)}, "arr_heap");

        for (uint64_t i = 0; i < numElements; i++)
        {
            llvm::Value* idx = builder->getInt64(i);
            llvm::Value* elemPtr = builder->CreateGEP(elemType, dataPtr, idx, "arr_elem");
            builder->CreateStore(cast_value(elementValues[i], elemType), elemPtr);
        }
    }
    else
    {
        // Stack allocation
        llvm::Value* arrAlloca = builder->CreateAlloca(arrayType, nullptr, "arr_data");
        for (uint64_t i = 0; i < numElements; i++)
        {
            llvm::Value* indices[] = {builder->getInt32(0), builder->getInt32(i)};
            llvm::Value* elemPtr = builder->CreateGEP(arrayType, arrAlloca, indices, "arr_elem");
            builder->CreateStore(cast_value(elementValues[i], elemType), elemPtr);
        }
        // Decay to pointer to first element
        llvm::Value* indices[] = {builder->getInt32(0), builder->getInt32(0)};
        dataPtr = builder->CreateGEP(arrayType, arrAlloca, indices, "arr_ptr");
    }

    // Try to build arr<T> slice struct
    const std::string arrName = currentScope->resolve_alias("arr");
    LOG_DEBUG("[generator]   resolve_alias('arr') = '%s'", arrName.c_str());
    StructDef* arrBaseDef = currentScope->lookup_struct(arrName);
    LOG_DEBUG("[generator]   lookup_struct('%s') = %p (isGeneric=%d)",
              arrName.c_str(), (void*)arrBaseDef, arrBaseDef ? arrBaseDef->isGeneric : -1);
    if (arrBaseDef && arrBaseDef->isGeneric)
    {
        // Determine the element Type for monomorphization
        Type elemDjinnType;
        if (expr.elementType)
        {
            elemDjinnType = *expr.elementType;
        }
        else
        {
            // Infer from LLVM type
            if (elemType->isIntegerTy())
            {
                elemDjinnType = Type::integer(elemType->getIntegerBitWidth(), true);
            }
            else if (elemType->isFloatTy())
            {
                elemDjinnType = Type::floating(32);
            }
            else if (elemType->isDoubleTy())
            {
                elemDjinnType = Type::floating(64);
            }
            else
            {
                // Fallback: return raw pointer
                return dataPtr;
            }
        }

        std::vector<Type> typeArgs = {elemDjinnType};
        llvm::StructType* sliceType = monomorphize_struct(arrName, typeArgs);
        const std::string mangledName = Mangler::mangle_generic_struct(arrName, typeArgs);
        StructDef* sliceDef = currentScope->lookup_struct(mangledName);

        LOG_DEBUG("[generator]   monomorphize arr<%s>: mangledName='%s' sliceDef=%p sliceType=%p",
                  mangledName.c_str(), mangledName.c_str(), (void*)sliceDef, (void*)sliceType);
        if (sliceDef && sliceType)
        {
            auto* sliceAlloca = builder->CreateAlloca(sliceType, nullptr, "arr_lit");

            // data (field 0)
            builder->CreateStore(dataPtr,
                                 builder->CreateStructGEP(sliceType, sliceAlloca, 0));

            // len (field 1)
            builder->CreateStore(
                builder->getInt32(static_cast<uint32_t>(numElements)),
                builder->CreateStructGEP(sliceType, sliceAlloca, 1));

            return sliceAlloca;
        }
    }

    // Fallback: return raw pointer
    return dataPtr;
}
