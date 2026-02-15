//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_variable_init(const VariableInit &expr) {
    if (auto *braceInit = dynamic_cast<const BraceInitializer *>(expr.value.get())) {
        if (expr.type.kind == TypeKind::STRUCT) {
            llvm::Type *llvmType = generate_type(expr.type);
            llvm::StructType *structType = llvm::dyn_cast<llvm::StructType>(llvmType);
            if (!structType) {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                                   "struct não encontrada: " + expr.type.structName);
            }

            const auto qualifiedName = currentScope->resolve_alias(expr.type.structName);
            const auto fieldIndicesName = expr.type.hasGenericArgs()
                                              ? Mangler::mangle_generic_struct(qualifiedName, expr.type.genericArgs)
                                              : qualifiedName;

            auto *alloca = builder->CreateAlloca(structType, nullptr, expr.name.token_name);
            currentScope->define_variable(expr.name.token_name, alloca, qualifiedName);

            const auto *fieldIndices = currentScope->get_field_indices(fieldIndicesName);
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
                    auto it = fieldIndices->find(elem.fieldName.token_name);
                    if (it == fieldIndices->end()) {
                        throw CompileError(DiagnosticCode::UNDEFINED_FIELD,
                                           "campo não encontrado: " + elem.fieldName.token_name);
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
                                   "não foi possível gerar valor inicial para: " + expr.name.token_name);
            }

            llvm::Type *type = generate_type(expr.type);
            initVal = cast_value(initVal, type);

            auto *alloca = builder->CreateAlloca(type, nullptr, expr.name.token_name);
            currentScope->define_variable(expr.name.token_name, alloca);
            builder->CreateStore(initVal, alloca);
            return alloca;
        }
    }

    // Handle ArrayLiteral: use declared type to determine element type
    if (auto *arrayLit = dynamic_cast<const ArrayLiteral *>(expr.value.get())) {
        // Determine element type from declaration (i32[]) or explicit annotation (i32[1,2,3])
        llvm::Type *elemType = nullptr;
        if (expr.type.kind == TypeKind::ARRAY && expr.type.elementType) {
            elemType = generate_type(*expr.type.elementType);
        } else if (arrayLit->elementType) {
            elemType = generate_type(*arrayLit->elementType);
        }

        // Generate element values
        std::vector<llvm::Value *> elementValues;
        for (const auto &elem : arrayLit->elements) {
            llvm::Value *val = generate_expression(*elem);
            if (!val) {
                throw CompileError(DiagnosticCode::INVALID_OPERATION,
                                   "failed to generate array element");
            }
            elementValues.push_back(val);
        }

        // Fallback: infer from first element if no type info available
        if (!elemType && !elementValues.empty()) {
            elemType = elementValues[0]->getType();
        }

        const uint64_t numElements = elementValues.size();
        llvm::Type *arrayType = llvm::ArrayType::get(elemType, numElements);
        llvm::Value *arrPtr;

        if (arrayLit->isHeap) {
            // Heap allocation via malloc
            llvm::Function *mallocFunc = module->getFunction("malloc");
            if (!mallocFunc) {
                llvm::FunctionType *mallocTy = llvm::FunctionType::get(
                    builder->getPtrTy(), {builder->getInt64Ty()}, false);
                mallocFunc = llvm::Function::Create(
                    mallocTy, llvm::Function::ExternalLinkage, "malloc", module.get());
            }
            const llvm::DataLayout &dataLayout = module->getDataLayout();
            uint64_t totalSize = dataLayout.getTypeAllocSize(elemType) * numElements;
            arrPtr = builder->CreateCall(mallocFunc, {builder->getInt64(totalSize)}, "arr_heap");

            for (uint64_t i = 0; i < numElements; i++) {
                llvm::Value *idx = builder->getInt64(i);
                llvm::Value *ep = builder->CreateGEP(elemType, arrPtr, idx, "arr_elem");
                builder->CreateStore(cast_value(elementValues[i], elemType), ep);
            }
        } else {
            // Stack allocation
            llvm::Value *arrAlloca = builder->CreateAlloca(arrayType, nullptr, "arr");
            for (uint64_t i = 0; i < numElements; i++) {
                llvm::Value *indices[] = {builder->getInt32(0), builder->getInt32(i)};
                llvm::Value *ep = builder->CreateGEP(arrayType, arrAlloca, indices, "arr_elem");
                builder->CreateStore(cast_value(elementValues[i], elemType), ep);
            }
            // Decay to pointer to first element
            llvm::Value *indices[] = {builder->getInt32(0), builder->getInt32(0)};
            arrPtr = builder->CreateGEP(arrayType, arrAlloca, indices, "arr_ptr");
        }

        auto *alloca = builder->CreateAlloca(builder->getPtrTy(), nullptr, expr.name.token_name);
        currentScope->define_variable(expr.name.token_name, alloca, "", elemType);
        builder->CreateStore(arrPtr, alloca);
        return alloca;
    }

    llvm::Value *initVal = generate_expression(*expr.value);
    if (!initVal) {
        throw CompileError(DiagnosticCode::INVALID_OPERATION,
                           "não foi possível gerar valor inicial para: " + expr.name.token_name);
    }

    // If the init value is an alloca (e.g. from a constructor call), reuse it directly
    if (auto *allocaInit = llvm::dyn_cast<llvm::AllocaInst>(initVal)) {
        if (allocaInit->getAllocatedType()->isStructTy()) {
            allocaInit->setName(expr.name.token_name);
            const auto qualifiedName = currentScope->resolve_alias(expr.type.structName);
            currentScope->define_variable(expr.name.token_name, allocaInit, qualifiedName);
            return allocaInit;
        }
    }

    // If the init value is from a 'new' expression, variable holds a heap pointer
    if (dynamic_cast<const NewExpression *>(expr.value.get())) {
        auto *alloca = builder->CreateAlloca(builder->getPtrTy(), nullptr, expr.name.token_name);
        std::string structTypeName;
        if (expr.type.kind == TypeKind::STRUCT) {
            structTypeName = currentScope->resolve_alias(expr.type.structName);
        }
        currentScope->define_variable(expr.name.token_name, alloca, structTypeName);
        builder->CreateStore(initVal, alloca);
        return alloca;
    }

    llvm::Type *type;
    llvm::Type *pointeeType = nullptr;

    if (expr.type.kind == TypeKind::AUTO) {
        type = initVal->getType();
    } else {
        type = generate_type(expr.type);
        initVal = cast_value(initVal, type);

        // Track pointee type for pointer/array variables
        if ((expr.type.kind == TypeKind::POINTER || expr.type.kind == TypeKind::ARRAY) && expr.type.elementType) {
            pointeeType = generate_type(*expr.type.elementType);
        }
    }

    auto *alloca = builder->CreateAlloca(type, nullptr, expr.name.token_name);
    currentScope->define_variable(expr.name.token_name, alloca, "", pointeeType);
    builder->CreateStore(initVal, alloca);
    return alloca;
}