//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_assignment(const Assignment &expr) {
    llvm::AllocaInst *alloca = currentScope->lookup_variable(expr.name.token_name);

    // If not found as a variable, check if we're in a method and the identifier is a struct field
    if (!alloca) {
        if (llvm::AllocaInst *thisAlloca = currentScope->lookup_variable("this")) {
            const std::string structName = currentScope->lookup_variable_struct_type("this");
            if (!structName.empty()) {
                if (const auto *fieldIndices = currentScope->get_field_indices(structName)) {
                    if (const auto it = fieldIndices->find(expr.name.token_name); it != fieldIndices->end()) {
                        // Assign to field through 'this'
                        llvm::StructType *structType = currentScope->get_llvm_struct(structName);
                        if (structType) {
                            llvm::Value *val = generate_expression(*expr.value);
                            if (val) {
                                llvm::Value *thisPtr = builder->CreateLoad(
                                    thisAlloca->getAllocatedType(), thisAlloca, "this");
                                auto *fieldPtr = builder->CreateStructGEP(
                                    structType, thisPtr, it->second, expr.name.token_name + "_ptr");
                                llvm::Type *fieldType = structType->getElementType(it->second);
                                val = cast_value(val, fieldType);
                                builder->CreateStore(val, fieldPtr);
                            }
                            return val;
                        }
                    }
                }
            }
        }
        throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + expr.name.token_name);
    }

    llvm::Value *val = generate_expression(*expr.value);
    if (val) {
        val = cast_value(val, alloca->getAllocatedType());
        builder->CreateStore(val, alloca);
    }
    return val;
}