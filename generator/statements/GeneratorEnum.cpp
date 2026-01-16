//
// Created by Claude on 09/01/2026.
//
#include "../Generator.h"

// ============================================================================
// Enum generation - Tagged unions
// ============================================================================
//
// For an enum like:
//   enum Result { Ok(i32), Error(string*) }
//
// We generate a struct with:
//   - A tag field (i8/i16/i32 depending on variant count)
//   - A payload array sized to fit the largest variant
//
// LLVM representation:
//   %Result = type { i8, [8 x i8] }  ; tag + max payload size
//
// Each variant gets a unique tag value (0, 1, 2, ...)
// ============================================================================

void Generator::generate_enum(const EnumDeclaration &enum_declaration, const std::string &prefix) {
    const std::string qualifiedName = prefix.empty()
                                          ? enum_declaration.name.token_name
                                          : prefix + "::" + enum_declaration.name.token_name;

    EnumDef def(qualifiedName, enum_declaration.isGeneric());
    def.genericParams = enum_declaration.genericParams;

    for (const auto &variant: enum_declaration.values) {
        def.addVariant(variant.name.token_name, variant.types);
    }

    if (enum_declaration.isGeneric()) {
        currentScope->define_enum(qualifiedName, std::move(def));
        return;
    }

    const auto variantCount = enum_declaration.values.size();
    llvm::Type *tagType;
    if (variantCount <= 256) {
        tagType = builder->getInt8Ty();
    } else if (variantCount <= 65536) {
        tagType = builder->getInt16Ty();
    } else {
        tagType = builder->getInt32Ty();
    }
    def.tagType = tagType;

    size_t maxPayloadSize = 0;
    for (const auto &variant: enum_declaration.values) {
        size_t variantPayloadSize = 0;

        for (const auto &type: variant.types) {
            if (llvm::Type *llvmType = generate_type(type)) {
                const auto &dataLayout = module->getDataLayout();
                variantPayloadSize += dataLayout.getTypeAllocSize(llvmType);
            }
        }

        maxPayloadSize = std::max(maxPayloadSize, variantPayloadSize);
    }

    def.maxPayloadSize = maxPayloadSize;
    std::vector<llvm::Type *> enumFields;
    enumFields.push_back(tagType);

    if (maxPayloadSize > 0) {
        enumFields.push_back(llvm::ArrayType::get(builder->getInt8Ty(), maxPayloadSize));
    }

    def.llvmType = llvm::StructType::create(*context, enumFields, qualifiedName);
    declaredTypes.push_back(def.llvmType);
    currentScope->define_enum(qualifiedName, std::move(def));
}

// ============================================================================
// Enum construction - Create a value of an enum variant
// ============================================================================
//
// For Result::Ok(42):
//   1. Allocate space for the enum struct
//   2. Set the tag to the variant's tag value
//   3. Store the payload value(s) in the payload area
//   4. Return the enum value
// ============================================================================

llvm::Value *Generator::generate_enum_construction(
    const EnumDef &enumDef,
    const EnumVariantDef &variant,
    const std::vector<std::unique_ptr<Expression> > &args
) {
    const auto enumAlloca = builder->CreateAlloca(enumDef.llvmType, nullptr, "enum_tmp");
    const auto tagPtr = builder->CreateStructGEP(enumDef.llvmType, enumAlloca, 0, "tag_ptr");
    llvm::Value *tagValue = llvm::ConstantInt::get(enumDef.tagType, variant.tag);
    builder->CreateStore(tagValue, tagPtr);

    if (!variant.associatedTypes.empty() && enumDef.maxPayloadSize > 0) {
        llvm::Value *payloadPtr = builder->CreateStructGEP(enumDef.llvmType, enumAlloca, 1, "payload_ptr");
        llvm::Value *payloadBytes = builder->CreateBitCast(
            payloadPtr,
            llvm::PointerType::get(builder->getInt8Ty(), 0),
            "payload_bytes"
        );

        size_t offset = 0;
        for (size_t i = 0; i < variant.associatedTypes.size() && i < args.size(); ++i) {
            llvm::Value *argValue = generate_expression(*args[i]);
            llvm::Type *argType = argValue->getType();
            llvm::Value *fieldPtr = builder->CreateGEP(
                builder->getInt8Ty(),
                payloadBytes,
                builder->getInt64(offset),
                "field_ptr"
            );

            llvm::Value *typedPtr = builder->CreateBitCast(
                fieldPtr,
                llvm::PointerType::get(argType, 0),
                "typed_ptr"
            );
            builder->CreateStore(argValue, typedPtr);

            const auto &dataLayout = module->getDataLayout();
            offset += dataLayout.getTypeAllocSize(argType);
        }
    }

    return builder->CreateLoad(enumDef.llvmType, enumAlloca, "enum_val");
}