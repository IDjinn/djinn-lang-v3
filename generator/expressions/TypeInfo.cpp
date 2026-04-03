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

uint8_t Generator::compute_type_kind(llvm::Type* type, const std::string& typeName)
{
    // Check name-based kinds first (handles fully qualified names like std::types::str)
    if (typeName == "str" || typeName.ends_with("::str")) return 5;
    if (typeName == "string" || typeName.ends_with("::string")) return 6;
    if (type->isIntegerTy())
    {
        if (typeName.starts_with("u")) return 4; // unsigned int
        return 0; // signed int
    }
    if (type->isFloatingPointTy()) return 1;
    if (type->isPointerTy()) return 2;
    if (type->isStructTy()) return 3;
    return 0; // default to int
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

    // Compute kind
    const uint8_t kind = compute_type_kind(llvmType, typeName);

    // Create constant struct: { i32 id, i32 size, i8* name, u8 kind }
    auto* constStruct = llvm::ConstantStruct::get(
        typeInfoDef->llvmType,
        {
            builder->getInt32(static_cast<uint32_t>(id)),
            builder->getInt32(static_cast<uint32_t>(size)),
            nameStr,
            builder->getInt8(kind)
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

std::string Generator::get_djinn_type_name(const Expression& expr, llvm::Value* value)
{
    if (const auto* intLit = dynamic_cast<const IntegerLiteral*>(&expr))
    {
        unsigned bits = value->getType()->isIntegerTy()
                            ? value->getType()->getIntegerBitWidth()
                            : 32;
        return (intLit->sign ? "i" : "u") + std::to_string(bits);
    }

    if (const auto* ident = dynamic_cast<const Identifier*>(&expr))
    {
        llvm::Type* valType = value->getType();
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(value))
            valType = alloca->getAllocatedType();

        if (valType->isIntegerTy())
        {
            unsigned bits = valType->getIntegerBitWidth();
            auto signOpt = currentScope->lookup_variable_signed(ident->identifier.token_name);
            bool isSigned = signOpt.value_or(true);
            return (isSigned ? "i" : "u") + std::to_string(bits);
        }

        auto structType = currentScope->lookup_variable_struct_type(ident->identifier.token_name);
        if (!structType.empty())
            return structType;
    }

    if (dynamic_cast<const StringLiteral*>(&expr))
        return "str";

    if (const auto* varInit = dynamic_cast<const VariableInit*>(&expr))
        return varInit->type.toHumanString();

    if (const auto* varDecl = dynamic_cast<const VariableDeclaration*>(&expr))
        return varDecl->type.toHumanString();

    return get_type_name_for_value(value);
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

    // Create object struct on stack: { TypeInfo* type, void* data }
    auto* objAlloca = builder->CreateAlloca(objectDef->llvmType, nullptr, "boxed");

    // Store TypeInfo pointer (field 0)
    auto* typeField = builder->CreateStructGEP(objectDef->llvmType, objAlloca, 0, "obj.type");
    builder->CreateStore(typeInfo, typeField);

    // Store data pointer (field 1)
    auto* dataField = builder->CreateStructGEP(objectDef->llvmType, objAlloca, 1, "obj.data");
    builder->CreateStore(dataPtr, dataField);

    // Load and return the object value
    return builder->CreateLoad(objectDef->llvmType, objAlloca, "boxed_val");
}