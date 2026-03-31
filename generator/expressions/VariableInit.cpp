//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"
#include "../../utils/Logger.h"

llvm::Value* Generator::generate_variable_init(const VariableInit& expr)
{
    LOG_DEBUG("[generator] variable init: '%s' type.kind=%d type.structName='%s' isArray=%d",
              expr.name.token_name.c_str(), static_cast<int>(expr.type.kind),
              expr.type.structName.c_str(), expr.type.kind == TypeKind::ARRAY);
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
            initVal = cast_value(initVal, type, expr.type.sign);

            auto* alloca = builder->CreateAlloca(type, nullptr, expr.name.token_name);
            currentScope->define_variable(expr.name.token_name, alloca);
            builder->CreateStore(initVal, alloca);
            return alloca;
        }
    }

    // Handle ArrayLiteral: delegate to generate_array_literal which produces arr<T> slice
    LOG_DEBUG("[generator]   checking if value is ArrayLiteral...");
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
                LOG_DEBUG("[generator]   array literal -> arr<T> slice: var='%s' structType='%s'",
                          expr.name.token_name.c_str(), structTypeName.c_str());
                currentScope->define_variable(expr.name.token_name, allocaInit, structTypeName, elemType);
                return allocaInit;
            }
        }

        // Fallback: raw pointer result
        LOG_DEBUG("[generator]   array literal -> fallback raw pointer for '%s'", expr.name.token_name.c_str());
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
            std::string qualifiedName = currentScope->resolve_alias(expr.type.structName);
            // When type is 'auto', structName is empty — use LLVM struct name as fallback
            if (qualifiedName.empty())
            {
                qualifiedName = allocaInit->getAllocatedType()->getStructName().str();
            }
            LOG_DEBUG("[generator]   struct alloca reuse: var='%s' structName='%s' resolved='%s' llvmType='%s'",
                      expr.name.token_name.c_str(), expr.type.structName.c_str(), qualifiedName.c_str(),
                      allocaInit->getAllocatedType()->getStructName().str().c_str());
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
        initVal = cast_value(initVal, type, expr.type.sign);

        // Track pointee type for pointer/array variables
        if ((expr.type.kind == TypeKind::POINTER || expr.type.kind == TypeKind::ARRAY) && expr.type.elementType)
        {
            pointeeType = generate_type(*expr.type.elementType);
        }
    }

    auto* alloca = builder->CreateAlloca(type, nullptr, expr.name.token_name);

    // Resolve struct type name for method dispatch
    std::string structTypeName;
    if (expr.type.kind == TypeKind::STRUCT)
    {
        structTypeName = currentScope->resolve_alias(expr.type.structName);
        if (expr.type.hasGenericArgs())
        {
            structTypeName = Mangler::mangle_generic_struct(structTypeName, expr.type.genericArgs);
        }
    }
    else if (expr.type.kind != TypeKind::POINTER && expr.type.kind != TypeKind::ARRAY)
    {
        std::string primName = get_primitive_type_name(type);
        if (!primName.empty() && currentScope->lookup_struct(primName))
        {
            structTypeName = primName;
        }
    }

    // For auto type, infer struct type name and pointee type from the expression
    if (expr.type.kind == TypeKind::AUTO && structTypeName.empty())
    {
        if (type->isStructTy())
        {
            if (auto* st = llvm::dyn_cast<llvm::StructType>(type))
            {
                structTypeName = st->getName().str();
            }
        }
        else if (type->isPointerTy())
        {
            // Use pointee type info propagated from field access
            if (_lastFieldAccessPointeeType && !_lastFieldAccessStructName.empty())
            {
                pointeeType = _lastFieldAccessPointeeType;
                structTypeName = _lastFieldAccessStructName;
                _lastFieldAccessPointeeType = nullptr;
                _lastFieldAccessStructName.clear();
            }
        }
    }

    currentScope->define_variable(expr.name.token_name, alloca, structTypeName, pointeeType);
    if (expr.type.kind == TypeKind::INTEGER)
    {
        currentScope->set_variable_signed(expr.name.token_name, expr.type.sign);
    }
    builder->CreateStore(initVal, alloca);
    return alloca;
}