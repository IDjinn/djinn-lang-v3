//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_field_access(const FieldAccess &expr) {
    if (auto *ident = dynamic_cast<const Identifier *>(expr.object.get())) {
        const auto alloca = currentScope->lookup_variable(ident->identifier.token_name);
        if (!alloca) {
            throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE,
                               "variável não encontrada: " + ident->identifier.token_name);
        }

        std::string structName;
        llvm::StructType *structType = nullptr;
        llvm::Value *basePtr = alloca;

        if (const std::string varStructType = currentScope->lookup_variable_struct_type(ident->identifier.token_name); varStructType.
            empty()) {
            llvm::Type *allocatedType = alloca->getAllocatedType();
            if (allocatedType->isPointerTy()) {
                // 'this' is a pointer to struct, need to load it first
                basePtr = builder->CreateLoad(allocatedType, alloca, ident->identifier.token_name);
                // We can't get struct name from pointer type in opaque pointers, so this case needs handling
                throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->identifier.token_name);
            } else if (auto *llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                structType = llvmStructType;
                structName = llvmStructType->getName().str();
            } else {
                throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->identifier.token_name);
            }
        } else {
            structName = varStructType;
            // Get struct type from alloca to handle monomorphized generics correctly
            llvm::Type *allocatedType = alloca->getAllocatedType();
            if (allocatedType->isPointerTy()) {
                // 'this' is a pointer to struct, need to load it first
                basePtr = builder->CreateLoad(allocatedType, alloca, ident->identifier.token_name);
                structType = currentScope->get_llvm_struct(structName);
            } else if (auto *llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                structType = llvmStructType;
            } else {
                structType = currentScope->get_llvm_struct(structName);
            }
        }

        // Use the LLVM struct name for field indices lookup (handles mangled generic names)
        std::string fieldIndicesName = structType ? structType->getName().str() : structName;

        // Check if this is a property access
        if (const PropertyInfo *propInfo = currentScope->get_property(fieldIndicesName, expr.fieldName.token_name)) {
            if (!propInfo->hasGetter) {
                throw CompileError(DiagnosticCode::INVALID_OPERATION,
                                   "property '" + expr.fieldName.token_name + "' does not have a getter (write-only)");
            }

            // Try to call getter (for computed properties)
            const std::string getterName = fieldIndicesName + "__get_" + expr.fieldName.token_name;
            if (const auto it = functions.find(getterName); it != functions.end()) {
                llvm::Function *getter = it->second;

                // Get pointer to the object for 'this'
                llvm::Value *thisPtr = basePtr;
                if (!basePtr->getType()->isPointerTy()) {
                    thisPtr = alloca;
                }

                return builder->CreateCall(getter, {thisPtr}, expr.fieldName.token_name);
            }

            // Auto-property: no getter function, access field directly
            // Fall through to regular field access
        }

        // Regular field access (including auto-properties)
        const auto *fieldIndices = currentScope->get_field_indices(fieldIndicesName);
        if (!fieldIndices) {
            throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
        }

        const auto fieldIt = fieldIndices->find(expr.fieldName.token_name);
        if (fieldIt == fieldIndices->end()) {
            throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + expr.fieldName.token_name);
        }

        const unsigned fieldIdx = fieldIt->second;
        auto *fieldPtr = builder->CreateStructGEP(structType, basePtr, fieldIdx, expr.fieldName.token_name + "_ptr");
        llvm::Type *fieldType = structType->getElementType(fieldIdx);
        return builder->CreateLoad(fieldType, fieldPtr, expr.fieldName.token_name);
    }

    throw CompileError(DiagnosticCode::INVALID_OPERATION, "acesso a campo só suportado em identificadores simples");
}

llvm::Value *Generator::generate_field_assignment(const FieldAssignment &expr) {
    if (auto *ident = dynamic_cast<const Identifier *>(expr.object.get())) {
        const auto alloca = currentScope->lookup_variable(ident->identifier.token_name);
        if (!alloca) {
            throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + ident->identifier.token_name);
        }

        std::string structName;
        llvm::StructType *structType = nullptr;
        llvm::Value *basePtr = alloca;

        if (const std::string varStructType = currentScope->lookup_variable_struct_type(ident->identifier.token_name); varStructType.
            empty()) {
            llvm::Type *allocatedType = alloca->getAllocatedType();
            if (allocatedType->isPointerTy()) {
                basePtr = builder->CreateLoad(allocatedType, alloca, "this");
                structName = currentScope->lookup_variable_struct_type(ident->identifier.token_name);
                if (structName.empty()) {
                    throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->identifier.token_name);
                }
                structType = currentScope->get_llvm_struct(structName);
            } else if (auto *llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                structType = llvmStructType;
                structName = llvmStructType->getName().str();
            } else {
                throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->identifier.token_name);
            }
        } else {
            structName = varStructType;
            llvm::Type *allocatedType = alloca->getAllocatedType();
            if (allocatedType->isPointerTy()) {
                basePtr = builder->CreateLoad(allocatedType, alloca, ident->identifier.token_name);
                structType = currentScope->get_llvm_struct(structName);
            } else if (auto *llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                structType = llvmStructType;
            } else {
                structType = currentScope->get_llvm_struct(structName);
            }
        }

        // Use the LLVM struct name for lookups (handles mangled generic names)
        std::string fieldIndicesName = structType ? structType->getName().str() : structName;

        // Check if this is a property assignment
        if (const PropertyInfo *propInfo = currentScope->get_property(fieldIndicesName, expr.fieldName.token_name)) {
            if (!propInfo->hasSetter) {
                throw CompileError(DiagnosticCode::INVALID_OPERATION,
                                   "property '" + expr.fieldName.token_name + "' does not have a setter (read-only)");
            }

            // Try to call setter (for computed properties)
            const std::string setterName = fieldIndicesName + "__set_" + expr.fieldName.token_name;
            if (const auto it = functions.find(setterName); it != functions.end()) {
                llvm::Function *setter = it->second;

                // Generate the value to assign
                llvm::Value *val = generate_expression(*expr.value);

                // Get pointer to the object for 'this'
                llvm::Value *thisPtr = basePtr;
                if (!basePtr->getType()->isPointerTy()) {
                    thisPtr = alloca;
                }

                // Cast value if needed
                llvm::Type *expectedType = setter->getFunctionType()->getParamType(1);
                val = cast_value(val, expectedType);

                builder->CreateCall(setter, {thisPtr, val});
                return val;
            }

            // Auto-property: no setter function, access field directly
            // Fall through to regular field assignment
        }

        // Regular field assignment (including auto-properties)
        const auto *fieldIndices = currentScope->get_field_indices(fieldIndicesName);
        if (!fieldIndices) {
            throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
        }

        const auto fieldIt = fieldIndices->find(expr.fieldName.token_name);
        if (fieldIt == fieldIndices->end()) {
            throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + expr.fieldName.token_name);
        }

        const unsigned fieldIdx = fieldIt->second;

        // For 'this' pointer (methods), we need to load the pointer first
        if (!basePtr->getType()->isPointerTy() || basePtr == alloca) {
            if (alloca->getAllocatedType()->isPointerTy()) {
                basePtr = builder->CreateLoad(alloca->getAllocatedType(), alloca, ident->identifier.token_name);
            }
        }

        auto *fieldPtr = builder->CreateStructGEP(structType, basePtr, fieldIdx, expr.fieldName.token_name + "_ptr");
        llvm::Type *fieldType = structType->getElementType(fieldIdx);

        llvm::Value *val = generate_expression(*expr.value);
        val = cast_value(val, fieldType);
        builder->CreateStore(val, fieldPtr);
        return val;
    }

    throw CompileError(DiagnosticCode::INVALID_OPERATION, "atribuição a campo só suportada em identificadores simples");
}