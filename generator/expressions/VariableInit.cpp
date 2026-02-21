//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value* Generator::generate_variable_init(const VariableInit& expr)
{
    if (auto* braceInit = dynamic_cast<const BraceInitializer*>(expr.value.get()))
    {
        if (expr.type.kind == TypeKind::STRUCT)
        {
            llvm::Type* llvmType = generate_type(expr.type);
            llvm::StructType* structType = llvm::dyn_cast<llvm::StructType>(llvmType);
            if (!structType)
            {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                                   "struct não encontrada: " + expr.type.structName);
            }

            const auto qualifiedName = currentScope->resolve_alias(expr.type.structName);
            const auto fieldIndicesName = expr.type.hasGenericArgs()
                                              ? Mangler::mangle_generic_struct(qualifiedName, expr.type.genericArgs)
                                              : qualifiedName;

            auto* alloca = builder->CreateAlloca(structType, nullptr, expr.name.token_name);
            currentScope->define_variable(expr.name.token_name, alloca, qualifiedName);

            // RAII: register for cleanup if struct has destroy()
            if (find_destroy_method(alloca))
            {
                currentScope->register_cleanup(expr.name.token_name, alloca);
            }

            const auto* fieldIndices = currentScope->get_field_indices(fieldIndicesName);
            if (!fieldIndices)
            {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                                   "struct não encontrada: " + expr.type.structName);
            }

            for (size_t i = 0; i < braceInit->elements.size(); ++i)
            {
                const auto& elem = braceInit->elements[i];
                llvm::Value* val = generate_expression(*elem.value);
                if (!val) return nullptr;

                unsigned fieldIdx;
                if (elem.isDesignated())
                {
                    auto it = fieldIndices->find(elem.fieldName.token_name);
                    if (it == fieldIndices->end())
                    {
                        throw CompileError(DiagnosticCode::UNDEFINED_FIELD,
                                           "campo não encontrado: " + elem.fieldName.token_name);
                    }
                    fieldIdx = it->second;
                }
                else
                {
                    fieldIdx = static_cast<unsigned>(i);
                }

                llvm::Type* fieldType = structType->getElementType(fieldIdx);
                val = cast_value(val, fieldType);

                auto* fieldPtr = builder->CreateStructGEP(structType, alloca, fieldIdx);
                builder->CreateStore(val, fieldPtr);
            }

            return alloca;
        }

        if (braceInit->elements.size() == 1 && !braceInit->elements[0].isDesignated())
        {
            llvm::Value* initVal = generate_expression(*braceInit->elements[0].value);
            if (!initVal)
            {
                throw CompileError(DiagnosticCode::INVALID_OPERATION,
                                   "não foi possível gerar valor inicial para: " + expr.name.token_name);
            }

            llvm::Type* type = generate_type(expr.type);
            initVal = cast_value(initVal, type);

            auto* alloca = builder->CreateAlloca(type, nullptr, expr.name.token_name);
            currentScope->define_variable(expr.name.token_name, alloca);
            builder->CreateStore(initVal, alloca);
            return alloca;
        }
    }

    // Handle ArrayLiteral: delegate to generate_array_literal which produces arr<T> slice
    if (auto* arrayLit = dynamic_cast<const ArrayLiteral*>(expr.value.get()))
    {
        // Set element type on the literal from the declaration type if not already set
        if (!arrayLit->elementType && expr.type.kind == TypeKind::ARRAY && expr.type.elementType)
        {
            const_cast<ArrayLiteral*>(arrayLit)->elementType = *expr.type.elementType;
        }

        llvm::Value* initVal = generate_array_literal(*arrayLit);

        // If it returned an arr<T> slice struct alloca, reuse directly
        if (auto* allocaInit = llvm::dyn_cast<llvm::AllocaInst>(initVal))
        {
            if (allocaInit->getAllocatedType()->isStructTy())
            {
                allocaInit->setName(expr.name.token_name);
                std::string structTypeName = allocaInit->getAllocatedType()->getStructName().str();
                // Track element type for index access
                llvm::Type* elemType = nullptr;
                if (expr.type.kind == TypeKind::ARRAY && expr.type.elementType)
                {
                    elemType = generate_type(*expr.type.elementType);
                }
                else if (arrayLit->elementType)
                {
                    elemType = generate_type(*arrayLit->elementType);
                }
                currentScope->define_variable(expr.name.token_name, allocaInit, structTypeName, elemType);
                return allocaInit;
            }
        }

        // Fallback: raw pointer result
        llvm::Type* elemType = nullptr;
        if (expr.type.kind == TypeKind::ARRAY && expr.type.elementType)
        {
            elemType = generate_type(*expr.type.elementType);
        }
        auto* alloca = builder->CreateAlloca(builder->getPtrTy(), nullptr, expr.name.token_name);
        currentScope->define_variable(expr.name.token_name, alloca, "", elemType);
        builder->CreateStore(initVal, alloca);
        return alloca;
    }

    llvm::Value* initVal = generate_expression(*expr.value);
    if (!initVal)
    {
        throw CompileError(DiagnosticCode::INVALID_OPERATION,
                           "não foi possível gerar valor inicial para: " + expr.name.token_name);
    }

    // If the init value is an alloca (e.g. from a constructor call), reuse it directly
    if (auto* allocaInit = llvm::dyn_cast<llvm::AllocaInst>(initVal))
    {
        if (allocaInit->getAllocatedType()->isStructTy())
        {
            allocaInit->setName(expr.name.token_name);
            const auto qualifiedName = currentScope->resolve_alias(expr.type.structName);
            currentScope->define_variable(expr.name.token_name, allocaInit, qualifiedName);

            // RAII: register for cleanup if struct has destroy()
            if (find_destroy_method(allocaInit))
            {
                currentScope->register_cleanup(expr.name.token_name, allocaInit);
            }

            return allocaInit;
        }
    }

    // If the init value is from a 'new' expression, variable holds a heap pointer
    if (const auto* newExpr = dynamic_cast<const NewExpression*>(expr.value.get()))
    {
        auto* alloca = builder->CreateAlloca(builder->getPtrTy(), nullptr, expr.name.token_name);
        std::string structTypeName;
        if (expr.type.kind == TypeKind::STRUCT)
        {
            // Use constructor call's token name (same as generate_new_expression)
            // to ensure consistent name resolution with how the struct was registered
            const auto& call = *newExpr->constructorCall;
            structTypeName = currentScope->resolve_alias(call.name.token_name);
            if (call.hasTypeArguments())
            {
                structTypeName = Mangler::mangle_generic_struct(structTypeName, call.typeArguments);
            }
        }
        currentScope->define_variable(expr.name.token_name, alloca, structTypeName);
        builder->CreateStore(initVal, alloca);
        return alloca;
    }

    llvm::Type* type;
    llvm::Type* pointeeType = nullptr;

    if (expr.type.kind == TypeKind::AUTO)
    {
        type = initVal->getType();
    }
    else
    {
        type = generate_type(expr.type);
        initVal = cast_value(initVal, type);

        // Track pointee type for pointer/array variables
        if ((expr.type.kind == TypeKind::POINTER || expr.type.kind == TypeKind::ARRAY) && expr.type.elementType)
        {
            pointeeType = generate_type(*expr.type.elementType);
        }
    }

    auto* alloca = builder->CreateAlloca(type, nullptr, expr.name.token_name);

    // Check if this primitive type has an impl block
    std::string structTypeName;
    if (expr.type.kind != TypeKind::STRUCT && expr.type.kind != TypeKind::POINTER && expr.type.kind != TypeKind::ARRAY)
    {
        std::string primName = get_primitive_type_name(type);
        if (!primName.empty() && currentScope->lookup_struct(primName))
        {
            structTypeName = primName;
        }
    }

    currentScope->define_variable(expr.name.token_name, alloca, structTypeName, pointeeType);
    builder->CreateStore(initVal, alloca);
    return alloca;
}