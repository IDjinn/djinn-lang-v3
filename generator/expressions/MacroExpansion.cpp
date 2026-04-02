#include "../Generator.h"
#include "../../parser/ast/Expression.h"

llvm::Value* Generator::generate_macro_expansion(const MacroExpansionExpression& expr)
{
    for (const auto& local : expr.locals)
    {
        llvm::Value* initVal = generate_expression(*local.value);
        llvm::Type* type = initVal->getType();
        auto* alloca = builder->CreateAlloca(type, nullptr, local.varName);
        builder->CreateStore(initVal, alloca);
        currentScope->define_variable(local.varName, alloca);
    }

    return generate_expression(*expr.body);
}