//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value* Generator::generate_string_literal(const StringLiteral& expr)
{
    const std::string resolved = currentScope->resolve_alias("str");
    StructDef* strDef = currentScope->lookup_struct(resolved);
    if (!strDef || !strDef->llvmType)
    {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                           "tipo 'str' nao encontrado. Adicione: import std::types;");
    }

    llvm::Value* globalPtr = builder->CreateGlobalStringPtr(expr.value, ".str");
    auto* alloca = builder->CreateAlloca(strDef->llvmType, nullptr, "str_lit");

    // data (field 0)
    builder->CreateStore(globalPtr,
                         builder->CreateStructGEP(strDef->llvmType, alloca, 0));

    // len (field 1)
    builder->CreateStore(
        builder->getInt32(static_cast<uint32_t>(expr.value.size())),
        builder->CreateStructGEP(strDef->llvmType, alloca, 1));

    return alloca;
}