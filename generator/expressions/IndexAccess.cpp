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

llvm::Value *Generator::generate_index_access(const IndexAccess &expr) {
    llvm::Value* ptr = generate_expression(*expr.object);
    llvm::Value* index = generate_expression(*expr.index);

    llvm::Type *elementType = resolve_index_element_type(*expr.object);

    if (!elementType) {
        // Fallback to i8 for untyped pointers (void*, i8*)
        elementType = builder->getInt8Ty();
    }

    llvm::Value *elementPtr = builder->CreateGEP(elementType, ptr, index, "idx");
    return builder->CreateLoad(elementType, elementPtr, "idx_val");
}

llvm::Value *Generator::generate_index_assignment(const IndexAssignment &expr) {
    llvm::Value* ptr = generate_expression(*expr.object);
    llvm::Value* index = generate_expression(*expr.index);

    llvm::Type *elementType = resolve_index_element_type(*expr.object);

    if (!elementType) {
        elementType = builder->getInt8Ty();
    }

    llvm::Value *val = generate_expression(*expr.value);
    val = cast_value(val, elementType);

    llvm::Value *elementPtr = builder->CreateGEP(elementType, ptr, index, "idx");
    builder->CreateStore(val, elementPtr);
    return val;
}
