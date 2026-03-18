//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"
#include "../../utils/Logger.h"

llvm::Value* Generator::generate_field_access(const FieldAccess& expr)
{
    if (auto* ident = dynamic_cast<const Identifier*>(expr.object.get()))
    {
        LOG_DEBUG("[generator] field access: '%s.%s'", ident->identifier.token_name.c_str(),
                  expr.fieldName.token_name.c_str());
        const auto alloca = currentScope->lookup_variable(ident->identifier.token_name);
        if (!alloca)
        {
            LOG_DEBUG("[generator]   variable '%s' NOT found in scope", ident->identifier.token_name.c_str());
            throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE,
                               "variável não encontrada: " + ident->identifier.token_name);
        }

        std::string structName;
        llvm::StructType* structType = nullptr;
        llvm::Value* basePtr = alloca;

        if (const std::string varStructType = currentScope->lookup_variable_struct_type(ident->identifier.token_name);
            varStructType.
            empty())
        {
            LOG_DEBUG("[generator]   lookup_variable_struct_type('%s') = EMPTY", ident->identifier.token_name.c_str());
            LOG_DEBUG("[generator]   alloca type: isPtr=%d isStruct=%d isInt=%d",
                      alloca->getAllocatedType()->isPointerTy(),
                      alloca->getAllocatedType()->isStructTy(),
                      alloca->getAllocatedType()->isIntegerTy());
            if (alloca->getAllocatedType()->isStructTy())
            {
                if (auto* st = llvm::dyn_cast<llvm::StructType>(alloca->getAllocatedType()))
                {
                    LOG_DEBUG("[generator]   LLVM struct type name: '%s'", st->getName().str().c_str());
                }
            }
            if (const auto allocatedType = alloca->getAllocatedType(); allocatedType->isPointerTy())
            {
                // Pointer to struct: load the pointer and try to resolve the pointee struct type
                basePtr = builder->CreateLoad(allocatedType, alloca, ident->identifier.token_name);

                // Check if we have a tracked pointee type for this variable
                if (llvm::Type* pointeeType = currentScope->lookup_variable_pointee_type(ident->identifier.token_name))
                {
                    if (auto* st = llvm::dyn_cast<llvm::StructType>(pointeeType))
                    {
                        structType = st;
                        structName = st->getName().str();
                        LOG_DEBUG("[generator]   resolved pointer-to-struct: '%s'", structName.c_str());
                    }
                }

                if (!structType)
                {
                    LOG_DEBUG("[generator]   FAILED: pointer type, throwing NOT_A_STRUCT for '%s'",
                              ident->identifier.token_name.c_str());
                    throw CompileError(DiagnosticCode::NOT_A_STRUCT,
                                       "variável não é uma struct: " + ident->identifier.token_name);
                }
            }
            else if (auto* llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType))
            {
                structType = llvmStructType;
                structName = llvmStructType->getName().str();
            }
            else
            {
                throw CompileError(DiagnosticCode::NOT_A_STRUCT,
                                   "variável não é uma struct: " + ident->identifier.token_name);
            }
        }
        else
        {
            structName = varStructType;
            if (const auto allocatedType = alloca->getAllocatedType(); allocatedType->isPointerTy())
            {
                basePtr = builder->CreateLoad(allocatedType, alloca, ident->identifier.token_name);
                structType = currentScope->get_llvm_struct(structName);
            }
            else if (auto* llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType))
            {
                structType = llvmStructType;
            }
            else
            {
                structType = currentScope->get_llvm_struct(structName);
            }
        }

        const auto fieldIndicesName = structType ? structType->getName().str() : structName;
        if (const auto propInfo = currentScope->get_property(fieldIndicesName, expr.fieldName.token_name))
        {
            if (!propInfo->hasGetter)
            {
                throw CompileError(DiagnosticCode::INVALID_OPERATION,
                                   "property '" + expr.fieldName.token_name + "' does not have a getter (write-only)");
            }

            const std::string getterName = fieldIndicesName + "__get_" + expr.fieldName.token_name;
            if (const auto it = functions.find(getterName); it != functions.end())
            {
                const auto getter = it->second;
                auto thisPtr = basePtr;
                if (!basePtr->getType()->isPointerTy())
                {
                    thisPtr = alloca;
                }

                return builder->CreateCall(getter, {thisPtr}, expr.fieldName.token_name);
                // TODO: GETTERS REALLY NEED BE FUNCTIONS?
            }
        }

        const auto* fieldIndices = currentScope->get_field_indices(fieldIndicesName);
        if (!fieldIndices)
        {
            throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
        }

        const auto fieldIt = fieldIndices->find(expr.fieldName.token_name);
        if (fieldIt == fieldIndices->end())
        {
            throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + expr.fieldName.token_name);
        }

        const unsigned fieldIdx = fieldIt->second;
        auto* fieldPtr = builder->CreateStructGEP(structType, basePtr, fieldIdx, expr.fieldName.token_name + "_ptr");
        llvm::Type* fieldType = structType->getElementType(fieldIdx);
        llvm::Value* result = builder->CreateLoad(fieldType, fieldPtr, expr.fieldName.token_name);

        // Track pointee type for pointer fields (e.g., TypeInfo* type in object struct)
        _lastFieldAccessPointeeType = nullptr;
        _lastFieldAccessStructName.clear();
        if (fieldType->isPointerTy())
        {
            if (const StructDef* srcDef = currentScope->lookup_struct(fieldIndicesName))
            {
                if (fieldIdx < srcDef->fields.size())
                {
                    const Type& djinnFieldType = srcDef->fields[fieldIdx].second;
                    if (djinnFieldType.kind == TypeKind::POINTER && djinnFieldType.elementType &&
                        djinnFieldType.elementType->kind == TypeKind::STRUCT)
                    {
                        std::string resolved = currentScope->resolve_alias(djinnFieldType.elementType->structName);
                        llvm::StructType* pointeeSt = currentScope->get_llvm_struct(resolved);
                        if (pointeeSt)
                        {
                            _lastFieldAccessPointeeType = pointeeSt;
                            _lastFieldAccessStructName = resolved;
                        }
                    }
                }
            }
        }

        return result;
    }

    throw CompileError(DiagnosticCode::INVALID_OPERATION, "acesso a campo só suportado em identificadores simples");
}

llvm::Value* Generator::generate_field_assignment(const FieldAssignment& expr)
{
    if (auto* ident = dynamic_cast<const Identifier*>(expr.object.get()))
    {
        const auto alloca = currentScope->lookup_variable(ident->identifier.token_name);
        if (!alloca)
        {
            throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE,
                               "variável não encontrada: " + ident->identifier.token_name);
        }

        std::string structName;
        llvm::StructType* structType = nullptr;
        llvm::Value* basePtr = alloca;

        if (const std::string varStructType = currentScope->lookup_variable_struct_type(ident->identifier.token_name);
            varStructType.
            empty())
        {
            if (const auto allocatedType = alloca->getAllocatedType(); allocatedType->isPointerTy())
            {
                basePtr = builder->CreateLoad(allocatedType, alloca, "this");
                structName = currentScope->lookup_variable_struct_type(ident->identifier.token_name);
                if (structName.empty())
                {
                    throw CompileError(DiagnosticCode::NOT_A_STRUCT,
                                       "variável não é uma struct: " + ident->identifier.token_name);
                }
                structType = currentScope->get_llvm_struct(structName);
            }
            else if (auto* llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType))
            {
                structType = llvmStructType;
                structName = llvmStructType->getName().str();
            }
            else
            {
                throw CompileError(DiagnosticCode::NOT_A_STRUCT,
                                   "variável não é uma struct: " + ident->identifier.token_name);
            }
        }
        else
        {
            structName = varStructType;
            if (const auto allocatedType = alloca->getAllocatedType(); allocatedType->isPointerTy())
            {
                basePtr = builder->CreateLoad(allocatedType, alloca, ident->identifier.token_name);
                structType = currentScope->get_llvm_struct(structName);
            }
            else if (auto* llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType))
            {
                structType = llvmStructType;
            }
            else
            {
                structType = currentScope->get_llvm_struct(structName);
            }
        }

        const auto fieldIndicesName = structType ? structType->getName().str() : structName;
        if (const PropertyInfo* propInfo = currentScope->get_property(fieldIndicesName, expr.fieldName.token_name))
        {
            if (!propInfo->hasSetter)
            {
                throw CompileError(DiagnosticCode::INVALID_OPERATION,
                                   "property '" + expr.fieldName.token_name + "' does not have a setter (read-only)");
            }

            const auto setterName = fieldIndicesName + "__set_" + expr.fieldName.token_name;
            if (const auto it = functions.find(setterName); it != functions.end())
            {
                llvm::Function* setter = it->second;
                auto val = generate_expression(*expr.value);

                llvm::Value* thisPtr = basePtr;
                if (!basePtr->getType()->isPointerTy())
                {
                    thisPtr = alloca;
                }

                const auto expectedType = setter->getFunctionType()->getParamType(1);
                val = cast_value(val, expectedType);

                builder->CreateCall(setter, {thisPtr, val}); // TODO: SETTERS REALLY NEED BE FUNCTIONS?
                return val;
            }
        }

        const auto* fieldIndices = currentScope->get_field_indices(fieldIndicesName);
        if (!fieldIndices)
        {
            throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
        }

        const auto fieldIt = fieldIndices->find(expr.fieldName.token_name);
        if (fieldIt == fieldIndices->end())
        {
            throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + expr.fieldName.token_name);
        }

        const unsigned fieldIdx = fieldIt->second;
        if (!basePtr->getType()->isPointerTy() || basePtr == alloca)
        {
            if (alloca->getAllocatedType()->isPointerTy())
            {
                basePtr = builder->CreateLoad(alloca->getAllocatedType(), alloca, ident->identifier.token_name);
            }
        }

        auto* fieldPtr = builder->CreateStructGEP(structType, basePtr, fieldIdx, expr.fieldName.token_name + "_ptr");
        llvm::Type* fieldType = structType->getElementType(fieldIdx);

        auto val = generate_expression(*expr.value);
        val = cast_value(val, fieldType);
        builder->CreateStore(val, fieldPtr);
        return val;
    }

    throw CompileError(DiagnosticCode::INVALID_OPERATION, "atribuição a campo só suportada em identificadores simples");
}