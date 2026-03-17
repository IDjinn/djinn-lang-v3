//
// Created by Claude on 10/02/2026.
//

#include "../Generator.h"

llvm::Type* Generator::resolve_index_element_type(const Expression& objectExpr)
{
    // Case 1: Simple identifier — lookup from scope's pointee type tracking
    if (const auto* ident = dynamic_cast<const Identifier*>(&objectExpr))
    {
        return currentScope->lookup_variable_pointee_type(ident->identifier.token_name);
    }

    // Case 2: Field access (e.g., this.data) — lookup from struct definition
    if (const auto* fieldAccess = dynamic_cast<const FieldAccess*>(&objectExpr))
    {
        if (const auto* ident = dynamic_cast<const Identifier*>(fieldAccess->object.get()))
        {
            std::string structName = currentScope->lookup_variable_struct_type(ident->identifier.token_name);
            if (!structName.empty())
            {
                if (const StructDef* structDef = currentScope->lookup_struct(structName))
                {
                    for (const auto& [fieldName, fieldType] : structDef->fields)
                    {
                        if (fieldName == fieldAccess->fieldName.token_name)
                        {
                            if ((fieldType.kind == TypeKind::POINTER || fieldType.kind == TypeKind::ARRAY)
                                && fieldType.elementType)
                            {
                                return generate_type(*fieldType.elementType);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    return nullptr;
}

// Extract data pointer from an arr<T> or str slice struct value/alloca
llvm::Value* Generator::extract_slice_data_ptr(llvm::Value* value)
{
    // Case 1: alloca to slice struct
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
    {
        if (auto* st = llvm::dyn_cast<llvm::StructType>(alloca->getAllocatedType()))
        {
            if (is_slice_struct(st))
            {
                auto* gep = builder->CreateStructGEP(st, alloca, 0, "slice.data");
                return builder->CreateLoad(builder->getPtrTy(), gep, "slice_ptr");
            }
        }
    }
    // Case 2: loaded struct value
    if (auto* st = llvm::dyn_cast<llvm::StructType>(value->getType()))
    {
        if (is_slice_struct(st))
        {
            return builder->CreateExtractValue(value, 0, "slice.data");
        }
    }
    return value;
}

llvm::Value* Generator::generate_index_access(const IndexAccess& expr)
{
    llvm::Value* obj = generate_expression(*expr.object);
    llvm::Value* index = generate_expression(*expr.index);

    // Check if object is a struct with operator get[]
    if (obj->getType()->isStructTy() || (llvm::isa<llvm::AllocaInst>(obj) &&
        llvm::cast<llvm::AllocaInst>(obj)->getAllocatedType()->isStructTy()))
    {
        llvm::Type* structTy = obj->getType()->isStructTy()
                                   ? obj->getType()
                                   : llvm::cast<llvm::AllocaInst>(obj)->getAllocatedType();
        auto* st = llvm::dyn_cast<llvm::StructType>(structTy);
        if (st && !is_slice_struct(st))
        {
            const std::string mangledName = st->getName().str() + "____op_index_get";
            auto it = functions.find(mangledName);
            if (it != functions.end())
            {
                // Call operator get[](collection, index)
                std::vector<llvm::Value*> args = {obj, index};
                return builder->CreateCall(it->second, args, "idx_get");
            }
        }
    }

    // Default: pointer/slice indexing
    llvm::Value* ptr = extract_slice_data_ptr(obj);

    llvm::Type* elementType = resolve_index_element_type(*expr.object);

    if (!elementType)
    {
        elementType = builder->getInt8Ty();
    }

    llvm::Value* elementPtr = builder->CreateGEP(elementType, ptr, index, "idx");
    return builder->CreateLoad(elementType, elementPtr, "idx_val");
}

llvm::Value* Generator::generate_index_assignment(const IndexAssignment& expr)
{
    llvm::Value* obj = generate_expression(*expr.object);
    llvm::Value* index = generate_expression(*expr.index);

    // Check if object is a struct with operator set[]
    if (obj->getType()->isStructTy() || (llvm::isa<llvm::AllocaInst>(obj) &&
        llvm::cast<llvm::AllocaInst>(obj)->getAllocatedType()->isStructTy()))
    {
        llvm::Type* structTy = obj->getType()->isStructTy()
                                   ? obj->getType()
                                   : llvm::cast<llvm::AllocaInst>(obj)->getAllocatedType();
        auto* st = llvm::dyn_cast<llvm::StructType>(structTy);
        if (st && !is_slice_struct(st))
        {
            const std::string mangledName = st->getName().str() + "____op_index_set";
            auto it = functions.find(mangledName);
            if (it != functions.end())
            {
                llvm::Value* val = generate_expression(*expr.value);
                // Call operator set[](collection*, index, value)
                std::vector<llvm::Value*> args = {obj, index, val};
                return builder->CreateCall(it->second, args, "idx_set");
            }
        }
    }

    // Default: pointer/slice indexing
    llvm::Value* ptr = extract_slice_data_ptr(obj);

    llvm::Type* elementType = resolve_index_element_type(*expr.object);

    if (!elementType)
    {
        elementType = builder->getInt8Ty();
    }

    llvm::Value* val = generate_expression(*expr.value);
    val = cast_value(val, elementType);

    llvm::Value* elementPtr = builder->CreateGEP(elementType, ptr, index, "idx");
    builder->CreateStore(val, elementPtr);
    return val;
}