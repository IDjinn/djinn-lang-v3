//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_assignment(const Assignment &expr) {
    if (const auto alloca = currentScope->lookup_variable(expr.name.token_name)) {
        auto val = generate_expression(*expr.value);
        if (val) {
            val = cast_value(val, alloca->getAllocatedType());
            builder->CreateStore(val, alloca);
        }
        return val;
    }

    if (const auto thisAlloca = currentScope->lookup_variable("this")) {
        if (const auto structName = currentScope->lookup_variable_struct_type("this"); !structName.empty()) {
            if (const auto *fieldIndices = currentScope->get_field_indices(structName)) {
                if (const auto it = fieldIndices->find(expr.name.token_name); it != fieldIndices->end()) {
                    if (const auto structType = currentScope->get_llvm_struct(structName)) {
                        auto val = generate_expression(*expr.value);
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