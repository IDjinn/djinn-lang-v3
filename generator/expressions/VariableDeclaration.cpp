//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_variable_declaration(const VariableDeclaration &expr) {
    llvm::Type *type = generate_type(const_cast<Type &>(expr.type));
    auto *alloca = builder->CreateAlloca(type, nullptr, expr.name);
    builder->CreateStore(llvm::Constant::getNullValue(type), alloca);
    std::string structTypeName = expr.type.kind == TypeKind::STRUCT ? expr.type.structName : "";
    currentScope->define_variable(expr.name, alloca, structTypeName);
    return alloca;
}
