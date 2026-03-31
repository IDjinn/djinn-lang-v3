//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value* Generator::generate_identifier(const Identifier& expr) const
{
    if (llvm::AllocaInst* alloca = currentScope->lookup_variable(expr.identifier.token_name))
    {
        return builder->CreateLoad(alloca->getAllocatedType(), alloca, expr.identifier.token_name);
    }

    if (const auto thisAlloca = currentScope->lookup_variable("this"))
    {
        if (const auto structName = currentScope->lookup_variable_struct_type("this"); !structName.empty())
        {
            if (const auto* fieldIndices = currentScope->get_field_indices(structName))
            {
                if (const auto it = fieldIndices->find(expr.identifier.token_name); it != fieldIndices->end())
                {
                    if (const auto structType = currentScope->get_llvm_struct(structName))
                    {
                        llvm::Value* thisPtr = builder->CreateLoad(thisAlloca->getAllocatedType(), thisAlloca, "this");
                        auto* fieldPtr = builder->CreateStructGEP(structType, thisPtr, it->second,
                                                                  expr.identifier.token_name + "_ptr");
                        llvm::Type* fieldType = structType->getElementType(it->second);
                        return builder->CreateLoad(fieldType, fieldPtr, expr.identifier.token_name);
                    }
                }
            }
        }
    }

    // Check const fields of the current struct (for use inside static methods)
    if (!currentStructName.empty())
    {
        if (const StructDef* structDef = currentScope->lookup_struct(currentStructName))
        {
            if (llvm::Constant* constVal = structDef->getConstField(expr.identifier.token_name))
            {
                return constVal;
            }
        }
    }

    // Check constexpr constants
    if (auto constVal = constEvaluator.lookupConstant(expr.identifier.token_name))
    {
        llvm::Constant* llvmConst = const_value_to_llvm(*constVal);
        if (llvmConst) return llvmConst;
    }

    throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + expr.identifier.token_name);
}