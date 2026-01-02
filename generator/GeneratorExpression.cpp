//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"
#include "llvm/Support/raw_ostream.h"

llvm::Value *Generator::generate_expression(const Expression &expr) {
    if (auto *intLit = dynamic_cast<const IntegerLiteral *>(&expr)) {
        const llvm::APInt apVal(128, intLit->value, 10);
        const unsigned activeBits = apVal.getActiveBits();

        unsigned bits;
        if (activeBits <= 8) {
            bits = 8;
        } else if (activeBits <= 16) {
            bits = 16;
        } else if (activeBits <= 32) {
            bits = 32;
        } else if (activeBits <= 64) {
            bits = 64;
        } else {
            bits = 128;
        }

        return llvm::ConstantInt::get(builder->getIntNTy(bits), apVal.trunc(bits));
    }

    if (auto *strLit = dynamic_cast<const StringLiteral *>(&expr)) {
        return builder->CreateGlobalString(strLit->value);
    }

    if (auto *binExpr = dynamic_cast<const BinaryExpression *>(&expr)) {
        auto *left = generate_expression(*binExpr->left);
        auto *right = generate_expression(*binExpr->right);

        if (!left || !right) return nullptr;

        switch (binExpr->op) {
            case TokenType::PLUS:
                return builder->CreateAdd(left, right, "addtmp");
            case TokenType::MINUS:
                return builder->CreateSub(left, right, "subtmp");
            case TokenType::STAR:
                return builder->CreateMul(left, right, "multmp");
            case TokenType::SLASH:
                return builder->CreateSDiv(left, right, "divtmp");
            case TokenType::PERCENT:
                return builder->CreateSRem(left, right, "modtmp");

            case TokenType::EQUAL_EQUAL:
                return builder->CreateICmpEQ(left, right, "eqtmp");
            case TokenType::BANG_EQUAL:
                return builder->CreateICmpNE(left, right, "netmp");
            case TokenType::LESS:
                return builder->CreateICmpSLT(left, right, "lttmp");
            case TokenType::LESS_EQUAL:
                return builder->CreateICmpSLE(left, right, "letmp");
            case TokenType::GREATER:
                return builder->CreateICmpSGT(left, right, "gttmp");
            case TokenType::GREATER_EQUAL:
                return builder->CreateICmpSGE(left, right, "getmp");

            case TokenType::AND_AND:
                return builder->CreateAnd(left, right, "andtmp");
            case TokenType::OR_OR:
                return builder->CreateOr(left, right, "ortmp");

            default:
                throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador binário não suportado");
        }
    }

    if (auto *unaryExpr = dynamic_cast<const UnaryExpression *>(&expr)) {
        auto *operand = generate_expression(*unaryExpr->operand);
        if (!operand) return nullptr;

        switch (unaryExpr->op) {
            case TokenType::MINUS:
                return builder->CreateNeg(operand, "negtmp");
            case TokenType::BANG:
                return builder->CreateNot(operand, "nottmp");
            default:
                throw CompileError(DiagnosticCode::UNSUPPORTED_OPERATOR, "operador unário não suportado");
        }
    }

    if (auto *call = dynamic_cast<const FunctionCall *>(&expr)) {
        const auto it = functions.find(call->name);
        if (it == functions.end()) {
            throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION, "função não encontrada: " + call->name);
        }

        llvm::Function *func = it->second;
        llvm::FunctionType *funcType = func->getFunctionType();

        std::vector<llvm::Value *> args;
        size_t argIdx = 0;
        for (const auto &arg: call->arguments) {
            llvm::Value *argVal = generate_expression(*arg);

            if (argIdx < funcType->getNumParams()) {
                argVal = cast_value(argVal, funcType->getParamType(argIdx));
            } else if (funcType->isVarArg()) {
                if (argVal->getType()->isIntegerTy() &&
                    argVal->getType()->getIntegerBitWidth() < 32) {
                    argVal = builder->CreateSExt(argVal, builder->getInt32Ty(), "vararg_promote");
                }
            }
            args.push_back(argVal);
            argIdx++;
        }

        return builder->CreateCall(func, args);
    }

    if (auto *ident = dynamic_cast<const Identifier *>(&expr)) {
        auto it = namedValues.find(ident->name);
        if (it != namedValues.end()) {
            return builder->CreateLoad(it->second->getAllocatedType(), it->second, ident->name);
        }
        throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + ident->name);
    }

    if (auto *varDecl = dynamic_cast<const VariableDeclaration *>(&expr)) {
        llvm::Type *type = generate_type(const_cast<Type &>(varDecl->type));
        auto *alloca = builder->CreateAlloca(type, nullptr, varDecl->name);
        builder->CreateStore(llvm::Constant::getNullValue(type), alloca);
        namedValues[varDecl->name] = alloca;
        if (varDecl->type.kind == TypeKind::STRUCT) {
            variableStructTypes[varDecl->name] = varDecl->type.structName;
        }
        return alloca;
    }

    if (auto *fieldAccess = dynamic_cast<const FieldAccess *>(&expr)) {
        if (auto *ident = dynamic_cast<const Identifier *>(fieldAccess->object.get())) {
            auto allocaIt = namedValues.find(ident->name);
            if (allocaIt == namedValues.end()) {
                throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + ident->name);
            }

            std::string structName;
            llvm::StructType *structType = nullptr;

            if (auto structTypeIt = variableStructTypes.find(ident->name); structTypeIt != variableStructTypes.end()) {
                structName = structTypeIt->second;
                structType = structTypes[structName];
            } else {
                const auto allocatedType = allocaIt->second->getAllocatedType();
                if (auto *llvmStructType = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                    structType = llvmStructType;
                    structName = llvmStructType->getName().str();
                } else {
                    throw CompileError(DiagnosticCode::NOT_A_STRUCT, "variável não é uma struct: " + ident->name);
                }
            }

            const auto &fieldIndices = structFieldIndices[structName];

            auto fieldIt = fieldIndices.find(fieldAccess->fieldName);
            if (fieldIt == fieldIndices.end()) {
                throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + fieldAccess->fieldName);
            }

            unsigned fieldIdx = fieldIt->second;
            auto *fieldPtr = builder->CreateStructGEP(structType, allocaIt->second, fieldIdx,
                                                      fieldAccess->fieldName + "_ptr");
            llvm::Type *fieldType = structType->getElementType(fieldIdx);
            return builder->CreateLoad(fieldType, fieldPtr, fieldAccess->fieldName);
        }

        throw CompileError(DiagnosticCode::INVALID_OPERATION, "acesso a campo só suportado em identificadores simples");
    }

    if (auto *varInit = dynamic_cast<const VariableInit *>(&expr)) {
        if (auto *braceInit = dynamic_cast<const BraceInitializer *>(varInit->value.get())) {
            if (varInit->type.kind == TypeKind::STRUCT) {
                llvm::StructType *structType = structTypes[varInit->type.structName];
                if (!structType) {
                    throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                                       "struct não encontrada: " + varInit->type.structName);
                }

                auto *alloca = builder->CreateAlloca(structType, nullptr, varInit->name);
                namedValues[varInit->name] = alloca;
                variableStructTypes[varInit->name] = varInit->type.structName;

                const auto &fieldIndices = structFieldIndices[varInit->type.structName];

                for (size_t i = 0; i < braceInit->elements.size(); ++i) {
                    const auto &elem = braceInit->elements[i];
                    llvm::Value *val = generate_expression(*elem.value);
                    if (!val) return nullptr;

                    unsigned fieldIdx;
                    if (elem.isDesignated()) {
                        auto it = fieldIndices.find(elem.fieldName);
                        if (it == fieldIndices.end()) {
                            throw CompileError(DiagnosticCode::UNDEFINED_FIELD,
                                               "campo não encontrado: " + elem.fieldName);
                        }
                        fieldIdx = it->second;
                    } else {
                        fieldIdx = static_cast<unsigned>(i);
                    }

                    llvm::Type *fieldType = structType->getElementType(fieldIdx);
                    val = cast_value(val, fieldType);

                    auto *fieldPtr = builder->CreateStructGEP(structType, alloca, fieldIdx);
                    builder->CreateStore(val, fieldPtr);
                }

                return alloca;
            }

            if (braceInit->elements.size() == 1 && !braceInit->elements[0].isDesignated()) {
                llvm::Value *initVal = generate_expression(*braceInit->elements[0].value);
                if (!initVal) {
                    throw CompileError(DiagnosticCode::INVALID_OPERATION,
                                       "não foi possível gerar valor inicial para: " + varInit->name);
                }

                llvm::Type *type = generate_type(const_cast<Type &>(varInit->type));
                initVal = cast_value(initVal, type);

                auto *alloca = builder->CreateAlloca(type, nullptr, varInit->name);
                namedValues[varInit->name] = alloca;
                builder->CreateStore(initVal, alloca);
                return alloca;
            }
        }

        llvm::Value *initVal = generate_expression(*varInit->value);
        if (!initVal) {
            throw CompileError(DiagnosticCode::INVALID_OPERATION,
                               "não foi possível gerar valor inicial para: " + varInit->name);
        }

        llvm::Type *type;
        if (varInit->type.kind == TypeKind::AUTO) {
            type = initVal->getType();
        } else {
            type = generate_type(const_cast<Type &>(varInit->type));
            initVal = cast_value(initVal, type);
        }

        auto *alloca = builder->CreateAlloca(type, nullptr, varInit->name);
        namedValues[varInit->name] = alloca;
        builder->CreateStore(initVal, alloca);
        return alloca;
    }

    if (auto *assign = dynamic_cast<const Assignment *>(&expr)) {
        auto it = namedValues.find(assign->name);
        if (it == namedValues.end()) {
            throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE, "variável não encontrada: " + assign->name);
        }

        llvm::Value *val = generate_expression(*assign->value);
        if (val) {
            val = cast_value(val, it->second->getAllocatedType());
            builder->CreateStore(val, it->second);
        }
        return val;
    }

    if (auto *braceInit = dynamic_cast<const BraceInitializer *>(&expr)) {
        if (braceInit->elements.empty()) {
            return nullptr;
        }

        bool hasDesignated = false;
        bool hasPositional = false;
        for (const auto &elem: braceInit->elements) {
            if (elem.isDesignated()) {
                hasDesignated = true;
            } else {
                hasPositional = true;
            }
        }

        if (hasDesignated && hasPositional) {
            throw CompileError(DiagnosticCode::MIXED_INITIALIZERS,
                               "não é possível misturar inicializadores designados e posicionais");
        }

        std::vector<llvm::Value *> values;
        for (const auto &elem: braceInit->elements) {
            llvm::Value *val = generate_expression(*elem.value);
            if (!val) return nullptr;
            values.push_back(val);
        }

        if (values.size() == 1 && !hasDesignated) {
            return values[0];
        }

        return values[0];
    }

    return nullptr;
}
