//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_identifier(const Identifier &expr) const {
    if (llvm::AllocaInst *alloca = currentScope->lookup_variable(expr.name)) {
        return builder->CreateLoad(alloca->getAllocatedType(), alloca, expr.name);
    }
    throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + expr.name);
}
