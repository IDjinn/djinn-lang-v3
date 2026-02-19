//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value* Generator::generate_variable_declaration(const VariableDeclaration& expr)
{
    llvm::Type* type = generate_type(expr.type);
    auto* alloca = builder->CreateAlloca(type, nullptr, expr.name.token_name);
    builder->CreateStore(llvm::Constant::getNullValue(type), alloca);

    std::string structTypeName;
    llvm::Type* pointeeType = nullptr;
    if (expr.type.kind == TypeKind::STRUCT)
    {
        if (expr.type.hasGenericArgs())
        {
            structTypeName = Mangler::mangle_generic_struct(expr.type.structName, expr.type.genericArgs);
        }
        else
        {
            structTypeName = expr.type.structName;
        }
    }
    else if ((expr.type.kind == TypeKind::POINTER || expr.type.kind == TypeKind::ARRAY) && expr.type.elementType)
    {
        pointeeType = generate_type(*expr.type.elementType);
    }
    else
    {
        // Check if this primitive type has an impl block (e.g., i32 with impl i32 { ... })
        std::string primName = get_primitive_type_name(type);
        if (!primName.empty() && currentScope->lookup_struct(primName))
        {
            structTypeName = primName;
        }
    }

    currentScope->define_variable(expr.name.token_name, alloca, structTypeName, pointeeType);
    return alloca;
}