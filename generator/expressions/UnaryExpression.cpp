//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value* Generator::generate_unary_expression(const UnaryExpression& expr)
{
    switch (expr.op)
    {
    case TokenType::AMPERSAND:
        {
            // Address-of operator: return the pointer to the variable without loading
            if (const auto* ident = dynamic_cast<const Identifier*>(expr.operand.get()))
            {
                if (llvm::AllocaInst* alloca = currentScope->lookup_variable(ident->identifier.token_name))
                {
                    return alloca;
                }
                throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE,
                                   "variável não encontrada: " + ident->identifier.token_name);
            }
            throw CompileError(DiagnosticCode::INVALID_OPERAND,
                               "operador '&' requer uma variável como operando");
        }

    case TokenType::STAR:
        {
            // Dereference operator: load the value from the pointer
            // For a variable like `i32* p`, the alloca stores a pointer (i32*)
            // We first load the pointer value, then dereference it

            if (const auto* ident = dynamic_cast<const Identifier*>(expr.operand.get()))
            {
                if (llvm::AllocaInst* alloca = currentScope->lookup_variable(ident->identifier.token_name))
                {
                    llvm::Type* allocatedType = alloca->getAllocatedType();

                    // The variable is a pointer type (e.g., i32*)
                    // Load the pointer value first
                    llvm::Value* ptrValue = builder->CreateLoad(allocatedType, alloca, ident->identifier.token_name);

                    if (!ptrValue->getType()->isPointerTy())
                    {
                        throw CompileError(DiagnosticCode::INVALID_OPERAND,
                                           "operador '*' requer um ponteiro como operando");
                    }

                    // Look up the tracked pointee type for this pointer variable
                    llvm::Type* elementType = currentScope->lookup_variable_pointee_type(ident->identifier.token_name);
                    if (!elementType)
                    {
                        // Fallback to i32 if not tracked
                        elementType = builder->getInt32Ty();
                    }

                    return builder->CreateLoad(elementType, ptrValue, "deref");
                }
            }

            // For other expressions (not simple identifiers), generate normally
            auto* operand = generate_expression(*expr.operand);
            if (!operand) return nullptr;

            if (!operand->getType()->isPointerTy())
            {
                throw CompileError(DiagnosticCode::INVALID_OPERAND,
                                   "operador '*' requer um ponteiro como operando");
            }

            // Default to i32 for element type
            return builder->CreateLoad(builder->getInt32Ty(), operand, "deref");
        }

    case TokenType::MINUS:
        {
            auto* operand = generate_expression(*expr.operand);
            if (!operand) return nullptr;

            // For floating-point constants, fold at compile time
            if (auto* constFP = llvm::dyn_cast<llvm::ConstantFP>(operand))
            {
                llvm::APFloat negVal = constFP->getValueAPF();
                negVal.changeSign();
                return llvm::ConstantFP::get(operand->getType(), negVal);
            }

            // For floating-point runtime values, use fneg
            if (operand->getType()->isFloatingPointTy())
            {
                return builder->CreateFNeg(operand, "negtmp");
            }

            // For integer constants, fold at compile time
            if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(operand))
            {
                return llvm::ConstantInt::get(operand->getType(), -constInt->getValue());
            }

            // For integer runtime values
            return builder->CreateNeg(operand, "negtmp");
        }

    case TokenType::BANG:
        {
            auto* operand = generate_expression(*expr.operand);
            if (!operand) return nullptr;
            return builder->CreateNot(operand, "nottmp");
        }

    case TokenType::TILDE:
        {
            auto* operand = generate_expression(*expr.operand);
            if (!operand) return nullptr;
            // Bitwise NOT: XOR with all ones
            return builder->CreateXor(operand, llvm::Constant::getAllOnesValue(operand->getType()), "bitnottmp");
        }

    default:
        throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador unário não suportado");
    }
}