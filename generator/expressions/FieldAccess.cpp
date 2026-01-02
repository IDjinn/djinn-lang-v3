//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_field_access(const FieldAccess &expr) {
    if (auto *ident = dynamic_cast<const Identifier *>(expr.object.get())) {
        llvm::AllocaInst *alloca = currentScope->lookup_variable(ident->name);
        if (!alloca) {
            throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + ident->name);
        }

        std::string structName;
        llvm::StructType *structType = nullptr;

        std::string varStructType = currentScope->lookup_variable_struct_type(ident->name);
        if (!varStructType.empty()) {
            structName = varStructType;
            structType = currentScope->lookup_struct(structName);
        } else {
            llvm::Type *allocatedType = alloca->getAllocatedType();
            if (auto *llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                structType = llvmStructType;
                structName = llvmStructType->getName().str();
            } else {
                throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->name);
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
        auto *fieldPtr = builder->CreateStructGEP(structType, alloca, fieldIdx, expr.fieldName + "_ptr");
        llvm::Type *fieldType = structType->getElementType(fieldIdx);
        return builder->CreateLoad(fieldType, fieldPtr, expr.fieldName);
    }

    throw CompileError(DiagnosticCode::INVALID_OPERATION, "acesso a campo só suportado em identificadores simples");
}