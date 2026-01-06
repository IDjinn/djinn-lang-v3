//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

llvm::Type *Generator::generate_type(const Type &type) {
    switch (type.kind) {
        case TypeKind::INTEGER: return builder->getIntNTy(type.size);
        case TypeKind::STRING: return builder->getPtrTy();
        case TypeKind::VOID: return builder->getVoidTy();
        case TypeKind::F16: return builder->getHalfTy();
        case TypeKind::F32: return builder->getFloatTy();
        case TypeKind::F64: return builder->getDoubleTy();
        case TypeKind::F128: return llvm::Type::getFP128Ty(*context);
        case TypeKind::ARRAY: {
            if (!type.elementType) {
                throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo array deve ter tipo de elemento");
            }
            llvm::Type *elemType = generate_type(*type.elementType);
            return llvm::PointerType::get(elemType, 0);
        }
        case TypeKind::POINTER: {
            if (!type.elementType) {
                throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo ponteiro deve ter tipo de elemento");
            }
            llvm::Type *pointeeType = generate_type(*type.elementType);
            return llvm::PointerType::get(pointeeType, 0);
        }
        case TypeKind::STRUCT: {
            if (llvm::Type *transparentType = currentScope->lookup_transparent_type(type.structName)) {
                return transparentType;
            }

            if (type.hasGenericArgs()) {
                llvm::StructType *existingType = currentScope->lookup_monomorphized_struct(
                    type.structName, type.genericArgs);
                if (existingType) {
                    return existingType;
                }

                // Lookup generic definition
                const GenericStructDef *genericDef = currentScope->lookup_generic_struct(type.structName);
                if (!genericDef) {
                    throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                                       "struct genérica não encontrada: " + type.structName);
                }

                GenericArgs args;
                for (const auto &argType: type.genericArgs) {
                    args.add(argType);
                }
                const GenericContext ctx = GenericContext::create(genericDef->params, args);

                std::vector<llvm::Type *> fieldTypes;
                std::unordered_map<std::string, unsigned> fieldIndices;
                unsigned idx = 0;
                for (const auto &[fieldName, fieldType]: genericDef->fields) {
                    Type substituted = ctx.substitute(fieldType);
                    fieldTypes.push_back(generate_type(substituted));
                    fieldIndices[fieldName] = idx++;
                }

                const std::string mangledName = Mangler::mangle_generic_struct(type.structName, type.genericArgs);
                llvm::StructType *structType = llvm::StructType::create(*context, fieldTypes, mangledName);

                currentScope->define_monomorphized_struct(type.structName, type.genericArgs,
                                                          structType, std::move(fieldIndices));

                return structType;
            }

            llvm::StructType *structType = currentScope->lookup_struct(type.structName);
            if (!structType) {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + type.structName);
            }
            return structType;
        }
        case TypeKind::AUTO:
            throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo auto deve ser inferido antes da geração de código");
        default: throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo inválido");
    }
}

llvm::Value *Generator::cast_value(llvm::Value *value, llvm::Type *targetType) const {
    if (!value || !targetType) return value;

    const llvm::Type *srcType = value->getType();
    if (srcType == targetType) return value;

    if (srcType->isIntegerTy() && targetType->isIntegerTy()) {
        const unsigned srcBits = srcType->getIntegerBitWidth();
        const unsigned dstBits = targetType->getIntegerBitWidth();

        if (srcBits < dstBits) {
            return builder->CreateSExt(value, targetType, "sext");
        }
        if (srcBits > dstBits) {
            return builder->CreateTrunc(value, targetType, "trunc");
        }
    }

    if (srcType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        if (srcType->getPrimitiveSizeInBits() < targetType->getPrimitiveSizeInBits()) {
            return builder->CreateFPExt(value, targetType, "fpext");
        }
        return builder->CreateFPTrunc(value, targetType, "fptrunc");
    }

    if (srcType->isIntegerTy() && targetType->isFloatingPointTy()) {
        return builder->CreateSIToFP(value, targetType, "sitofp");
    }

    if (srcType->isFloatingPointTy() && targetType->isIntegerTy()) {
        return builder->CreateFPToSI(value, targetType, "fptosi");
    }

    return value;
}