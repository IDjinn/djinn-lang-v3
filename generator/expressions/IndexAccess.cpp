//
// Created by Claude on 10/02/2026.
//

#include "../Generator.h"

llvm::Value *Generator::generate_index_access(const IndexAccess &expr) {
    llvm::Value *ptr = generate_expression(*expr.object);
    llvm::Value *index = generate_expression(*expr.index);

    // Determine the element type from the scope's pointee type tracking
    llvm::Type *elementType = nullptr;
    if (const auto *ident = dynamic_cast<const Identifier *>(expr.object.get())) {
        elementType = currentScope->lookup_variable_pointee_type(ident->identifier.token_name);
    }

    if (!elementType) {
        // Fallback to i8 for untyped pointers (void*, i8*)
        elementType = builder->getInt8Ty();
    }

    llvm::Value *elementPtr = builder->CreateGEP(elementType, ptr, index, "idx");
    return builder->CreateLoad(elementType, elementPtr, "idx_val");
}

llvm::Value *Generator::generate_index_assignment(const IndexAssignment &expr) {
    llvm::Value *ptr = generate_expression(*expr.object);
    llvm::Value *index = generate_expression(*expr.index);

    // Determine element type from the scope's pointee type tracking
    llvm::Type *elementType = nullptr;
    if (const auto *ident = dynamic_cast<const Identifier *>(expr.object.get())) {
        elementType = currentScope->lookup_variable_pointee_type(ident->identifier.token_name);
    }

    if (!elementType) {
        elementType = builder->getInt8Ty();
    }

    llvm::Value *val = generate_expression(*expr.value);
    val = cast_value(val, elementType);

    llvm::Value *elementPtr = builder->CreateGEP(elementType, ptr, index, "idx");
    builder->CreateStore(val, elementPtr);
    return val;
}
