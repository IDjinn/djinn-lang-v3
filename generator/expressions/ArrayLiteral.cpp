//
// Array literal code generation: [1, 2, 3] and i32[1, 2, 3]
//

#include "../Generator.h"

llvm::Value* Generator::generate_array_literal(const ArrayLiteral& expr)
{
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

    if (expr.isHeap)
    {
        // Heap allocation: new [1, 2, 3] or new i32[1, 2, 3]
        llvm::Function* mallocFunc = module->getFunction("malloc");
        if (!mallocFunc)
        {
            llvm::FunctionType* mallocTy = llvm::FunctionType::get(
                builder->getPtrTy(), {builder->getInt64Ty()}, false);
            mallocFunc = llvm::Function::Create(
                mallocTy, llvm::Function::ExternalLinkage, "malloc", module.get());
        }

        const llvm::DataLayout& dataLayout = module->getDataLayout();
        uint64_t elemSize = dataLayout.getTypeAllocSize(elemType);
        uint64_t totalSize = elemSize * numElements;
        llvm::Value* sizeVal = builder->getInt64(totalSize);
        llvm::Value* rawPtr = builder->CreateCall(mallocFunc, {sizeVal}, "arr_heap");

        // Store elements via GEP on the raw pointer
        for (uint64_t i = 0; i < numElements; i++)
        {
            llvm::Value* idx = builder->getInt64(i);
            llvm::Value* elemPtr = builder->CreateGEP(elemType, rawPtr, idx, "arr_elem");
            llvm::Value* val = cast_value(elementValues[i], elemType);
            builder->CreateStore(val, elemPtr);
        }

        return rawPtr;
    }

    // Stack allocation: [1, 2, 3] or i32[1, 2, 3]
    llvm::Value* alloca = builder->CreateAlloca(arrayType, nullptr, "arr");

    for (uint64_t i = 0; i < numElements; i++)
    {
        llvm::Value* indices[] = {builder->getInt32(0), builder->getInt32(i)};
        llvm::Value* elemPtr = builder->CreateGEP(arrayType, alloca, indices, "arr_elem");
        llvm::Value* val = cast_value(elementValues[i], elemType);
        builder->CreateStore(val, elemPtr);
    }

    // Decay to pointer (like C arrays) — return pointer to first element
    llvm::Value* indices[] = {builder->getInt32(0), builder->getInt32(0)};
    return builder->CreateGEP(arrayType, alloca, indices, "arr_ptr");
}