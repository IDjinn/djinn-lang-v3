#include "../Generator.h"

llvm::Value* Generator::generate_postfix_expression(const PostfixExpression& expr)
{
    const auto* ident = dynamic_cast<const Identifier*>(expr.operand.get());
    if (!ident)
    {
        GENERATOR_ERROR(DiagnosticCode::INVALID_OPERAND,
                        "operador '" + tokenTypeToString(expr.op) + "' requer uma variável como operando",
                        expr.location);
    }

    auto* alloca = currentScope->lookup_variable(ident->identifier.token_name);
    if (!alloca)
    {
        GENERATOR_ERROR(DiagnosticCode::UNDEFINED_VARIABLE,
                        "variável não encontrada: " + ident->identifier.token_name,
                        expr.location);
    }

    llvm::Type* type = alloca->getAllocatedType();
    llvm::Value* oldVal = builder->CreateLoad(type, alloca, ident->identifier.token_name);

    llvm::Value* newVal = nullptr;
    if (type->isIntegerTy())
    {
        llvm::Value* one = llvm::ConstantInt::get(type, 1);
        if (expr.op == TokenType::PLUS_PLUS)
            newVal = builder->CreateAdd(oldVal, one, "inc");
        else
            newVal = builder->CreateSub(oldVal, one, "dec");
    }
    else if (type->isFloatingPointTy())
    {
        llvm::Value* one = llvm::ConstantFP::get(type, 1.0);
        if (expr.op == TokenType::PLUS_PLUS)
            newVal = builder->CreateFAdd(oldVal, one, "finc");
        else
            newVal = builder->CreateFSub(oldVal, one, "fdec");
    }
    else
    {
        GENERATOR_ERROR(DiagnosticCode::INVALID_OPERAND,
                        "operador '" + tokenTypeToString(expr.op) + "' não suportado para este tipo",
                        expr.location);
    }

    builder->CreateStore(newVal, alloca);
    return oldVal;
}