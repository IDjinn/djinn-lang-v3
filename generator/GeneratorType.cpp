//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

llvm::Type *Generator::generate_type(Type &type) {
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
            auto it = structTypes.find(type.structName);
            if (it == structTypes.end()) {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + type.structName);
            }
            return it->second;
        }
        case TypeKind::AUTO:
            throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo auto deve ser inferido antes da geração de código");
        default: throw CompileError(DiagnosticCode::INVALID_TYPE, "tipo inválido");
    }
}

llvm::Value *Generator::cast_value(llvm::Value *value, llvm::Type *targetType) {
    if (!value || !targetType) return value;

    llvm::Type *srcType = value->getType();
    if (srcType == targetType) return value;

    if (srcType->isIntegerTy() && targetType->isIntegerTy()) {
        unsigned srcBits = srcType->getIntegerBitWidth();
        unsigned dstBits = targetType->getIntegerBitWidth();

        if (srcBits < dstBits) {
            return builder->CreateSExt(value, targetType, "sext");
        } else if (srcBits > dstBits) {
            return builder->CreateTrunc(value, targetType, "trunc");
        }
    }

    if (srcType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        if (srcType->getPrimitiveSizeInBits() < targetType->getPrimitiveSizeInBits()) {
            return builder->CreateFPExt(value, targetType, "fpext");
        } else {
            return builder->CreateFPTrunc(value, targetType, "fptrunc");
        }
    }

    if (srcType->isIntegerTy() && targetType->isFloatingPointTy()) {
        return builder->CreateSIToFP(value, targetType, "sitofp");
    }

    if (srcType->isFloatingPointTy() && targetType->isIntegerTy()) {
        return builder->CreateFPToSI(value, targetType, "fptosi");
    }

    return value;
}