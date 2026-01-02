//
// Created by Luke on 06/12/2025.
//

#include "Generator.h"

void Generator::generate_statement(const Statement &stmt) {
    if (auto *retStmt = dynamic_cast<const ReturnStatement *>(&stmt)) {
        if (retStmt->value) {
            if (auto *braceInit = dynamic_cast<const BraceInitializer *>(retStmt->value.get())) {
                const auto returnType = currentFunction->getReturnType();
                if (auto *structType = llvm::dyn_cast<llvm::StructType>(returnType)) {
                    const auto structName = structType->getName().str();
                    if (const auto structVal = generate_brace_init_for_struct(*braceInit, structType, structName)) {
                        builder->CreateRet(structVal);
                        return;
                    }
                }
            }
            const auto val = generate_expression(*retStmt->value);
            builder->CreateRet(val);
        } else {
            builder->CreateRetVoid();
        }
    } else if (auto *exprStmt = dynamic_cast<const ExpressionStatement *>(&stmt)) {
        generate_expression(*exprStmt->expression);
    }
}

llvm::Value *Generator::generate_brace_init_for_struct(const BraceInitializer &braceInit, llvm::StructType *structType,
                                                       const std::string &structName) {
    auto *alloca = builder->CreateAlloca(structType, nullptr, "struct_init");
    builder->CreateStore(llvm::Constant::getNullValue(structType), alloca);

    const auto *fieldIndices = currentScope->lookup_field_indices(structName);
    if (!fieldIndices) {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT, "struct não encontrada: " + structName);
    }

    for (size_t i = 0; i < braceInit.elements.size(); ++i) {
        const auto &elem = braceInit.elements[i];
        llvm::Value *val = generate_expression(*elem.value);
        if (!val) return nullptr;

        unsigned fieldIdx;
        if (elem.isDesignated()) {
            auto it = fieldIndices->find(elem.fieldName);
            if (it == fieldIndices->end()) {
                throw CompileError(DiagnosticCode::UNDEFINED_FIELD, "campo não encontrado: " + elem.fieldName);
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

    return builder->CreateLoad(structType, alloca, "struct_val");
}