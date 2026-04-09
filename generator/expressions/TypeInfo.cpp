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

    // Create global string for type name (LinkOnceODR for cross-module dedup)
    const std::string nameGlobalName = "__typeinfo_name_" + typeName;
    auto* nameArray = llvm::ConstantDataArray::getString(*context, typeName, true);
    auto* nameGlobal = new llvm::GlobalVariable(
        *module, nameArray->getType(), /*isConstant=*/true,
        llvm::GlobalValue::LinkOnceODRLinkage, nameArray,
        nameGlobalName
    );
    nameGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    auto* nameComdat = module->getOrInsertComdat(nameGlobalName);
    nameComdat->setSelectionKind(llvm::Comdat::Any);
    nameGlobal->setComdat(nameComdat);
    auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
    auto* nameStr = llvm::ConstantExpr::getInBoundsGetElementPtr(
        nameArray->getType(), nameGlobal, llvm::ArrayRef<llvm::Constant*>{zero, zero});

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

    const std::string globalName = "__typeinfo_" + typeName;
    auto* global = new llvm::GlobalVariable(
        *module, typeInfoDef->llvmType, /*isConstant=*/true,
        llvm::GlobalValue::LinkOnceODRLinkage, constStruct,
        globalName
    );
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    auto* comdat = module->getOrInsertComdat(globalName);
    comdat->setSelectionKind(llvm::Comdat::Any);
    global->setComdat(comdat);

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

void Generator::generate_reflection_data()
{
    const bool reflectAll = (reflectionMode == "all");

    auto* attrInfoType = currentScope->lookup_struct(
        currentScope->resolve_alias("AttributeInfo"));
    auto* fieldInfoType = currentScope->lookup_struct(
        currentScope->resolve_alias("FieldInfo"));
    auto* methodInfoType = currentScope->lookup_struct(
        currentScope->resolve_alias("MethodInfo"));
    auto* typeInfoExtType = currentScope->lookup_struct(
        currentScope->resolve_alias("TypeInfoExt"));
    auto* typeInfoType = currentScope->lookup_struct(
        currentScope->resolve_alias("TypeInfo"));

    if (!attrInfoType || !fieldInfoType || !methodInfoType || !typeInfoExtType || !typeInfoType)
    {
        LOG_ERROR("Reflection structs not found in std::types — cannot generate reflection data");
        return;
    }

    auto makeStr = [&](const std::string& str, const std::string& globalName) -> llvm::Constant*
    {
        auto* arr = llvm::ConstantDataArray::getString(*context, str, true);
        auto* global = new llvm::GlobalVariable(
            *module, arr->getType(), true,
            llvm::GlobalValue::LinkOnceODRLinkage, arr, globalName);
        global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        auto* comdat = module->getOrInsertComdat(globalName);
        comdat->setSelectionKind(llvm::Comdat::Any);
        global->setComdat(comdat);
        auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        return llvm::ConstantExpr::getInBoundsGetElementPtr(
            arr->getType(), global, llvm::ArrayRef<llvm::Constant*>{zero, zero});
    };

    auto makeAttrInfo = [&](const AttributeSymbol& attr, const std::string& prefix) -> llvm::Constant*
    {
        auto* nameStr = makeStr(attr.name, prefix + "_name_" + attr.name);
        std::string valStr;
        for (size_t i = 0; i < attr.args.size(); i++)
        {
            if (i > 0) valStr += ",";
            std::visit([&](const auto& v)
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int64_t>) valStr += std::to_string(v);
                else if constexpr (std::is_same_v<T, double>) valStr += std::to_string(v);
                else if constexpr (std::is_same_v<T, std::string>) valStr += v;
                else if constexpr (std::is_same_v<T, bool>) valStr += v ? "true" : "false";
            }, attr.args[i].value);
        }
        auto* valueStr = makeStr(valStr, prefix + "_val_" + attr.name);
        return llvm::ConstantStruct::get(attrInfoType->llvmType, {nameStr, valueStr});
    };

    auto makeAttrArray = [&](const std::vector<AttributeSymbol>& attrs,
                             const std::string& prefix) -> std::pair<llvm::Constant*, uint8_t>
    {
        if (attrs.empty())
            return {
                llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0)),
                static_cast<uint8_t>(0)
            };

        std::vector<llvm::Constant*> attrConstants;
        for (size_t i = 0; i < attrs.size(); i++)
            attrConstants.push_back(makeAttrInfo(attrs[i], prefix + "_a" + std::to_string(i)));

        auto* arrType = llvm::ArrayType::get(attrInfoType->llvmType, attrs.size());
        auto* arrConst = llvm::ConstantArray::get(arrType, attrConstants);
        auto* arrGlobal = new llvm::GlobalVariable(
            *module, arrType, true,
            llvm::GlobalValue::LinkOnceODRLinkage, arrConst, prefix + "_attrs");
        arrGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        auto* comdat = module->getOrInsertComdat(prefix + "_attrs");
        comdat->setSelectionKind(llvm::Comdat::Any);
        arrGlobal->setComdat(comdat);
        auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        auto* ptr = llvm::ConstantExpr::getInBoundsGetElementPtr(
            arrType, arrGlobal, llvm::ArrayRef<llvm::Constant*>{zero, zero});
        return {ptr, static_cast<uint8_t>(attrs.size())};
    };

    for (const auto& sym : symbols->get_all_structs())
    {
        const auto& structSym = *std::dynamic_pointer_cast<StructSymbol>(sym);

        if (!reflectAll && !structSym.hasAttribute("Reflect"))
            continue;

        if (structSym.isGeneric())
            continue;

        StructDef* def = currentScope->lookup_struct(structSym.name);
        if (!def || !def->llvmType)
            continue;

        const std::string prefix = "__reflect_" + structSym.name;
        const auto& dl = module->getDataLayout();
        const auto* structLayout = dl.getStructLayout(def->llvmType);

        // Generate FieldInfo array
        std::vector<llvm::Constant*> fieldConstants;
        for (const auto& [fieldName, fieldType] : def->fields)
        {
            unsigned idx = def->getFieldIndex(fieldName);
            uint64_t offset = structLayout->getElementOffset(idx);
            int32_t typeId = compute_type_id(fieldType.toHumanString());

            uint8_t flags = 0;
            for (const auto& f : structSym.fields)
            {
                if (f.name == fieldName)
                {
                    if (f.isMutable) flags |= 1;
                    break;
                }
            }

            auto* nameStr = makeStr(fieldName, prefix + "_f_" + fieldName);
            auto fieldConst = llvm::ConstantStruct::get(fieldInfoType->llvmType, {
                                                            nameStr,
                                                            builder->getInt32(static_cast<uint32_t>(typeId)),
                                                            llvm::ConstantInt::get(
                                                                llvm::Type::getInt16Ty(*context), offset),
                                                            builder->getInt8(flags),
                                                            builder->getInt8(0),
                                                            llvm::ConstantPointerNull::get(
                                                                llvm::PointerType::get(*context, 0))
                                                        });
            fieldConstants.push_back(fieldConst);
        }

        llvm::Constant* fieldsPtr;
        if (fieldConstants.empty())
        {
            fieldsPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
        }
        else
        {
            auto* arrType = llvm::ArrayType::get(fieldInfoType->llvmType, fieldConstants.size());
            auto* arrConst = llvm::ConstantArray::get(arrType, fieldConstants);
            auto* arrGlobal = new llvm::GlobalVariable(
                *module, arrType, true,
                llvm::GlobalValue::LinkOnceODRLinkage, arrConst, prefix + "_fields");
            arrGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
            auto* comdat = module->getOrInsertComdat(prefix + "_fields");
            comdat->setSelectionKind(llvm::Comdat::Any);
            arrGlobal->setComdat(comdat);
            auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            fieldsPtr = llvm::ConstantExpr::getInBoundsGetElementPtr(
                arrType, arrGlobal, llvm::ArrayRef<llvm::Constant*>{zero, zero});
        }

        // Generate MethodInfo array
        std::vector<llvm::Constant*> methodConstants;
        for (const auto& method : structSym.methods)
        {
            auto funcIt = def->methodFunctions.find(method->name);
            llvm::Constant* funcPtr = funcIt != def->methodFunctions.end()
                                          ? static_cast<llvm::Constant*>(funcIt->second)
                                          : llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));

            int32_t retTypeId = compute_type_id(method->returnType.toHumanString());
            uint8_t paramCount = static_cast<uint8_t>(method->paramTypes.size());
            uint8_t flags = 0;
            if (method->isStatic) flags |= 2;
            if (method->isAsync) flags |= 4;

            auto [mAttrsPtr, mAttrCount] = makeAttrArray(method->attributes,
                                                         prefix + "_m_" + method->name);

            auto* nameStr = makeStr(method->name, prefix + "_m_name_" + method->name);
            auto methodConst = llvm::ConstantStruct::get(methodInfoType->llvmType, {
                                                             nameStr,
                                                             funcPtr,
                                                             builder->getInt32(static_cast<uint32_t>(retTypeId)),
                                                             builder->getInt8(paramCount),
                                                             builder->getInt8(flags),
                                                             builder->getInt8(mAttrCount),
                                                             mAttrsPtr
                                                         });
            methodConstants.push_back(methodConst);
        }

        llvm::Constant* methodsPtr;
        if (methodConstants.empty())
        {
            methodsPtr = llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
        }
        else
        {
            auto* arrType = llvm::ArrayType::get(methodInfoType->llvmType, methodConstants.size());
            auto* arrConst = llvm::ConstantArray::get(arrType, methodConstants);
            auto* arrGlobal = new llvm::GlobalVariable(
                *module, arrType, true,
                llvm::GlobalValue::LinkOnceODRLinkage, arrConst, prefix + "_methods");
            arrGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
            auto* comdat = module->getOrInsertComdat(prefix + "_methods");
            comdat->setSelectionKind(llvm::Comdat::Any);
            arrGlobal->setComdat(comdat);
            auto* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            methodsPtr = llvm::ConstantExpr::getInBoundsGetElementPtr(
                arrType, arrGlobal, llvm::ArrayRef<llvm::Constant*>{zero, zero});
        }

        // Struct-level attributes
        auto [structAttrsPtr, structAttrCount] = makeAttrArray(structSym.attributes, prefix);

        // Create TypeInfo base
        auto* typeInfoGlobal = get_or_create_typeinfo(structSym.name, def->llvmType);
        auto* typeInfoConst = typeInfoGlobal->getInitializer();

        // Create TypeInfoExt constant
        auto* extConst = llvm::ConstantStruct::get(typeInfoExtType->llvmType, {
                                                       typeInfoConst,
                                                       llvm::ConstantInt::get(
                                                           llvm::Type::getInt16Ty(*context), fieldConstants.size()),
                                                       llvm::ConstantInt::get(
                                                           llvm::Type::getInt16Ty(*context), methodConstants.size()),
                                                       builder->getInt8(structAttrCount),
                                                       fieldsPtr,
                                                       methodsPtr,
                                                       structAttrsPtr
                                                   });

        const std::string extName = "__typeinfo_ext_" + structSym.name;
        auto* extGlobal = new llvm::GlobalVariable(
            *module, typeInfoExtType->llvmType, true,
            llvm::GlobalValue::LinkOnceODRLinkage, extConst, extName);
        extGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        auto* comdat = module->getOrInsertComdat(extName);
        comdat->setSelectionKind(llvm::Comdat::Any);
        extGlobal->setComdat(comdat);

        LOG_DEBUG("[reflect] Generated TypeInfoExt for %s: %zu fields, %zu methods",
                  structSym.name.c_str(), fieldConstants.size(), methodConstants.size());
    }
}