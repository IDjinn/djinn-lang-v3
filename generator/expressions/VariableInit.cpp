//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_variable_init(const VariableInit &expr) {
    if (auto *braceInit = dynamic_cast<const BraceInitializer *>(expr.value.get())) {
        if (expr.type.kind == TypeKind::STRUCT) {
            // Use generate_type to handle generic structs (monomorphization)
            llvm::Type *llvmType = generate_type(const_cast<Type &>(expr.type));
            llvm::StructType *structType = llvm::dyn_cast<llvm::StructType>(llvmType);
            if (!structType) {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                                   "struct não encontrada: " + expr.type.structName);
            }

            // Resolve qualified name base for method lookup
            std::string qualifiedName = currentScope->resolve_struct_alias(expr.type.structName);

            // Get the correct name for field indices lookup (mangled for generics)
            std::string fieldIndicesName = expr.type.hasGenericArgs()
                                               ? Mangler::mangle_generic_struct(qualifiedName, expr.type.genericArgs)
                                               : qualifiedName;

            auto *alloca = builder->CreateAlloca(structType, nullptr, expr.name);
            currentScope->define_variable(expr.name, alloca, qualifiedName);

            const auto *fieldIndices = currentScope->lookup_field_indices(fieldIndicesName);
            if (!fieldIndices) {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                                   "struct não encontrada: " + expr.type.structName);
            }

            for (size_t i = 0; i < braceInit->elements.size(); ++i) {
                const auto &elem = braceInit->elements[i];
                llvm::Value *val = generate_expression(*elem.value);
                if (!val) return nullptr;

                unsigned fieldIdx;
                if (elem.isDesignated()) {
                    auto it = fieldIndices->find(elem.fieldName);
                    if (it == fieldIndices->end()) {
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
                                   "não foi possível gerar valor inicial para: " + expr.name);
            }

            llvm::Type *type = generate_type(const_cast<Type &>(expr.type));
            initVal = cast_value(initVal, type);

            auto *alloca = builder->CreateAlloca(type, nullptr, expr.name);
            currentScope->define_variable(expr.name, alloca);
            builder->CreateStore(initVal, alloca);
            return alloca;
        }
    }

    llvm::Value *initVal = generate_expression(*expr.value);
    if (!initVal) {
        throw CompileError(DiagnosticCode::INVALID_OPERATION,
                           "não foi possível gerar valor inicial para: " + expr.name);
    }

    llvm::Type *type;
    if (expr.type.kind == TypeKind::AUTO) {
        type = initVal->getType();
    } else {
        type = generate_type(const_cast<Type &>(expr.type));
        initVal = cast_value(initVal, type);
    }

    auto *alloca = builder->CreateAlloca(type, nullptr, expr.name);
    currentScope->define_variable(expr.name, alloca);
    builder->CreateStore(initVal, alloca);
    return alloca;
}