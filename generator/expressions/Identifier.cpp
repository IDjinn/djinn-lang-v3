//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_identifier(const Identifier &expr) const {
    if (llvm::AllocaInst *alloca = currentScope->lookup_variable(expr.name)) {
        return builder->CreateLoad(alloca->getAllocatedType(), alloca, expr.name);
    }

    // If not found as a variable, check if we're in a method and the identifier is a struct field
    if (llvm::AllocaInst *thisAlloca = currentScope->lookup_variable("this")) {
        const std::string structName = currentScope->lookup_variable_struct_type("this");
        if (!structName.empty()) {
            if (const auto *fieldIndices = currentScope->get_field_indices(structName)) {
                if (const auto it = fieldIndices->find(expr.name); it != fieldIndices->end()) {
                    // Access field through 'this'
                    llvm::StructType *structType = currentScope->get_llvm_struct(structName);
                    if (structType) {
                        llvm::Value *thisPtr = builder->CreateLoad(thisAlloca->getAllocatedType(), thisAlloca, "this");
                        auto *fieldPtr = builder->CreateStructGEP(structType, thisPtr, it->second, expr.name + "_ptr");
                        llvm::Type *fieldType = structType->getElementType(it->second);
                        return builder->CreateLoad(fieldType, fieldPtr, expr.name);
                    }
                }
            }
        }
    }

    throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + expr.name);
}