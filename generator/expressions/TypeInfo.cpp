//
// TypeInfo constant generation and object boxing for variadics
//

#include "../Generator.h"

int32_t Generator::compute_type_id(const std::string& typeName)
{
    uint32_t hash = 2166136261u; // FNV-1a offset basis
    for (const char c : typeName)
    {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u; // FNV-1a prime
    }
    return static_cast<int32_t>(hash);
}

llvm::GlobalVariable* Generator::get_or_create_typeinfo(const std::string& typeName, llvm::Type* llvmType)
{
    if (const auto it = typeInfoConstants.find(typeName); it != typeInfoConstants.end())
        return it->second;

    // Resolve TypeInfo struct type
    const std::string resolvedTypeInfo = currentScope->resolve_alias("TypeInfo");
    StructDef* typeInfoDef = currentScope->lookup_struct(resolvedTypeInfo);
    if (!typeInfoDef || !typeInfoDef->llvmType)
    {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                           "TypeInfo struct not found - ensure std::types is imported");
    }

    // Create global string for type name
    auto* nameStr = builder->CreateGlobalStringPtr(typeName, "__typeinfo_name_" + typeName);

    // Compute size via DataLayout
    const uint64_t size = module->getDataLayout().getTypeAllocSize(llvmType);

    // Compute deterministic ID (FNV-1a)
    const int32_t id = compute_type_id(typeName);

    // Create constant struct: { i32 id, i32 size, i8* name }
    auto* constStruct = llvm::ConstantStruct::get(
        typeInfoDef->llvmType,
        {
            builder->getInt32(static_cast<uint32_t>(id)),
            builder->getInt32(static_cast<uint32_t>(size)),
            nameStr
        }
    );

    auto* global = new llvm::GlobalVariable(
        *module, typeInfoDef->llvmType, /*isConstant=*/true,
        llvm::GlobalValue::PrivateLinkage, constStruct,
        "__typeinfo_" + typeName
    );

    typeInfoConstants[typeName] = global;
    return global;
}

std::string Generator::get_type_name_for_value(llvm::Value* value)
{
    llvm::Type* type = value->getType();

    // If it's a pointer to something, check if it's an alloca
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
    {
        type = alloca->getAllocatedType();
    }

    if (type->isIntegerTy())
    {
        unsigned bits = type->getIntegerBitWidth();
        return "i" + std::to_string(bits);
    }
    if (type->isFloatTy()) return "f32";
    if (type->isDoubleTy()) return "f64";
    if (type->isHalfTy()) return "f16";
    if (type->isPointerTy()) return "ptr";

    if (auto* st = llvm::dyn_cast<llvm::StructType>(type))
    {
        if (st->hasName())
            return st->getName().str();
    }

    return "unknown";
}

llvm::Value* Generator::box_value(llvm::Value* value, const std::string& typeName)
{
    // Resolve object struct
    const std::string resolvedObject = currentScope->resolve_alias("object");
    StructDef* objectDef = currentScope->lookup_struct(resolvedObject);
    if (!objectDef || !objectDef->llvmType)
    {
        throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                           "object struct not found - ensure std::types is imported");
    }

    llvm::Type* valType = value->getType();

    // If value is already a pointer (alloca), use it directly
    // Otherwise, stack-allocate and store
    llvm::Value* dataPtr;
    if (llvm::isa<llvm::AllocaInst>(value))
    {
        dataPtr = value;
    }
    else
    {
        auto* valAlloca = builder->CreateAlloca(valType, nullptr, "box_data");
        builder->CreateStore(value, valAlloca);
        dataPtr = valAlloca;
    }

    // Get or create TypeInfo constant
    llvm::GlobalVariable* typeInfo = get_or_create_typeinfo(typeName, valType);

    // Create object struct on stack: { void* data, TypeInfo* type }
    auto* objAlloca = builder->CreateAlloca(objectDef->llvmType, nullptr, "boxed");

    // Store data pointer (field 0)
    auto* dataField = builder->CreateStructGEP(objectDef->llvmType, objAlloca, 0, "obj.data");
    builder->CreateStore(dataPtr, dataField);

    // Store TypeInfo pointer (field 1)
    auto* typeField = builder->CreateStructGEP(objectDef->llvmType, objAlloca, 1, "obj.type");
    builder->CreateStore(typeInfo, typeField);

    // Load and return the object value
    return builder->CreateLoad(objectDef->llvmType, objAlloca, "boxed_val");
}