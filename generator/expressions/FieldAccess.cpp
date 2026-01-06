//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_field_access(const FieldAccess &expr) const {
    if (auto *ident = dynamic_cast<const Identifier *>(expr.object.get())) {
        const auto alloca = currentScope->lookup_variable(ident->name);
        if (!alloca) {
            throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + ident->name);
        }

        std::string structName;
        llvm::StructType *structType = nullptr;
        llvm::Value *basePtr = alloca;

        if (const std::string varStructType = currentScope->lookup_variable_struct_type(ident->name); varStructType.
            empty()) {
            llvm::Type *allocatedType = alloca->getAllocatedType();
            if (allocatedType->isPointerTy()) {
                // 'this' is a pointer to struct, need to load it first
                basePtr = builder->CreateLoad(allocatedType, alloca, ident->name);
                // We can't get struct name from pointer type in opaque pointers, so this case needs handling
                throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->name);
            } else if (auto *llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                structType = llvmStructType;
                structName = llvmStructType->getName().str();
            } else {
                throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->name);
            }
        } else {
            structName = varStructType;
            structType = currentScope->lookup_struct(structName);
            // Check if the variable is a pointer to struct (like 'this')
            if (alloca->getAllocatedType()->isPointerTy()) {
                basePtr = builder->CreateLoad(alloca->getAllocatedType(), alloca, ident->name);
            }
        }

        const auto *fieldIndices = currentScope->lookup_field_indices(structName);
        if (!fieldIndices) {
            throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
        }

        const auto fieldIt = fieldIndices->find(expr.fieldName);
        if (fieldIt == fieldIndices->end()) {
            throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + expr.fieldName);
        }

        const unsigned fieldIdx = fieldIt->second;
        auto *fieldPtr = builder->CreateStructGEP(structType, basePtr, fieldIdx, expr.fieldName + "_ptr");
        llvm::Type *fieldType = structType->getElementType(fieldIdx);
        return builder->CreateLoad(fieldType, fieldPtr, expr.fieldName);
    }

    throw CompileError(DiagnosticCode::INVALID_OPERATION, "acesso a campo só suportado em identificadores simples");
}

llvm::Value *Generator::generate_field_assignment(const FieldAssignment &expr) {
    if (auto *ident = dynamic_cast<const Identifier *>(expr.object.get())) {
        const auto alloca = currentScope->lookup_variable(ident->name);
        if (!alloca) {
            throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + ident->name);
        }

        std::string structName;
        llvm::StructType *structType = nullptr;

        if (const std::string varStructType = currentScope->lookup_variable_struct_type(ident->name); varStructType.
            empty()) {
            llvm::Type *allocatedType = alloca->getAllocatedType();
            if (auto *ptrType = llvm::dyn_cast<llvm::PointerType>(allocatedType)) {
                // 'this' is a pointer to struct, need to load it first
                llvm::Value *thisPtr = builder->CreateLoad(allocatedType, alloca, "this");
                // Get the element type through the alloca's name or type
                structName = currentScope->lookup_variable_struct_type(ident->name);
                if (structName.empty()) {
                    throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->name);
                }
                structType = currentScope->lookup_struct(structName);
                if (!structType) {
                    throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
                }

                const auto *fieldIndices = currentScope->lookup_field_indices(structName);
                if (!fieldIndices) {
                    throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
                }

                const auto fieldIt = fieldIndices->find(expr.fieldName);
                if (fieldIt == fieldIndices->end()) {
                    throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + expr.fieldName);
                }

                const unsigned fieldIdx = fieldIt->second;
                auto *fieldPtr = builder->CreateStructGEP(structType, thisPtr, fieldIdx, expr.fieldName + "_ptr");
                llvm::Type *fieldType = structType->getElementType(fieldIdx);

                llvm::Value *val = generate_expression(*expr.value);
                val = cast_value(val, fieldType);
                builder->CreateStore(val, fieldPtr);
                return val;
            } else if (auto *llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                structType = llvmStructType;
                structName = llvmStructType->getName().str();
            } else {
                throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->name);
            }
        } else {
            structName = varStructType;
            structType = currentScope->lookup_struct(structName);
        }

        const auto *fieldIndices = currentScope->lookup_field_indices(structName);
        if (!fieldIndices) {
            throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
        }

        const auto fieldIt = fieldIndices->find(expr.fieldName);
        if (fieldIt == fieldIndices->end()) {
            throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + expr.fieldName);
        }

        const unsigned fieldIdx = fieldIt->second;

        // For 'this' pointer (methods), we need to load the pointer first
        llvm::Value *basePtr = alloca;
        if (alloca->getAllocatedType()->isPointerTy()) {
            basePtr = builder->CreateLoad(alloca->getAllocatedType(), alloca, ident->name);
        }

        auto *fieldPtr = builder->CreateStructGEP(structType, basePtr, fieldIdx, expr.fieldName + "_ptr");
        llvm::Type *fieldType = structType->getElementType(fieldIdx);

        llvm::Value *val = generate_expression(*expr.value);
        val = cast_value(val, fieldType);
        builder->CreateStore(val, fieldPtr);
        return val;
    }

    throw CompileError(DiagnosticCode::INVALID_OPERATION, "atribuição a campo só suportada em identificadores simples");
}