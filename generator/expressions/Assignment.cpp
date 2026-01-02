//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_assignment(const Assignment &expr) {
    llvm::AllocaInst *alloca = currentScope->lookup_variable(expr.name);
    if (!alloca) {
        throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + expr.name);
    }

    llvm::Value *val = generate_expression(*expr.value);
    if (val) {
        val = cast_value(val, alloca->getAllocatedType());
        builder->CreateStore(val, alloca);
    }
    return val;
}