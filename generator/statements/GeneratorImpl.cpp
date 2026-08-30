//
// Generator for impl block methods on primitive types
//

#include "../Generator.h"

bool Generator::is_primitive_impl(const StructSymbol& struc) const
{
    // A primitive impl is a synthetic struct created by the binder
    // for impl blocks on primitive types (i32, f64, etc.)
    // It has no non-const fields, and its baseType is a primitive type.
    // Const fields (e.g. MAX_VALUE from `impl i32 { const i32 MAX_VALUE = ...; }`)
    // don't occupy struct layout and should not disqualify primitive impls.
    const bool hasNonConstFields = std::ranges::any_of(struc.fields,
                                                       [](const auto& f) { return !f.isConstant; });
    return !hasNonConstFields && struc.baseType &&
        struc.baseType->kind != TypeKind::STRUCT;
}

std::string Generator::get_primitive_type_name(llvm::Type* type)
{
    if (type->isIntegerTy())
    {
        unsigned bits = type->getIntegerBitWidth();
        return "i" + std::to_string(bits);
    }
    if (type->isFloatTy()) return "f32";
    if (type->isDoubleTy()) return "f64";
    if (type->isHalfTy()) return "f16";
    if (type->isFP128Ty()) return "f128";
    return "";
}

void Generator::generate_primitive_impl_method(const StructSymbol& struc, const MethodSymbol& method)
{
    push_scope();

    const std::string mangledName = struc.name + "__" + method.name;

    llvm::Type* returnType = generate_type(method.returnType);
    llvm::Type* thisType = generate_type(*struc.baseType);

    std::vector<llvm::Type*> paramTypes;
    const bool isStatic = method.isStatic;
    if (!isStatic)
    {
        // For primitive impl, 'this' is passed by value (not pointer)
        paramTypes.push_back(thisType);
    }

    for (const auto& paramType : method.paramTypes)
    {
        paramTypes.push_back(generate_type(paramType));
    }

    auto* funcType = llvm::FunctionType::get(returnType, paramTypes, false);
    auto* llvmFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, mangledName, *module);

    apply_attributes(llvmFunc, method.attributes);
    apply_implicit_attributes(llvmFunc);

    functions[mangledName] = llvmFunc;

    // Also register in StructDef if it exists
    if (StructDef* def = currentScope->lookup_struct(struc.name))
    {
        def->methodFunctions[method.name] = llvmFunc;
    }

    currentFunction = llvmFunc;
    currentFunctionThrows = false;
    currentContracts_.clear();
    contractReturnAlloca = nullptr;

    auto* entry = llvm::BasicBlock::Create(*context, "entry", llvmFunc);
    builder->SetInsertPoint(entry);

    emit_frame_push(struc.name + "." + method.name, method.location);

    auto argIt = llvmFunc->arg_begin();

    if (!isStatic)
    {
        argIt->setName("this");
        // Alloca for 'this' so we can use load/store consistently
        auto* thisAlloca = builder->CreateAlloca(thisType, nullptr, "this");
        builder->CreateStore(&*argIt, thisAlloca);
        currentScope->define_variable("this", thisAlloca, struc.name);
        currentScope->set_variable_signed("this", struc.baseType->sign);
        ++argIt;
    }

    size_t paramIdx = 0;
    while (argIt != llvmFunc->arg_end())
    {
        const auto& paramName = method.paramNames[paramIdx];
        const auto& paramType = method.paramTypes[paramIdx];
        argIt->setName(paramName);

        auto* alloca = builder->CreateAlloca(argIt->getType(), nullptr, paramName);
        builder->CreateStore(&*argIt, alloca);
        std::string paramStructType = paramType.kind == TypeKind::STRUCT ? paramType.structName : "";
        currentScope->define_variable(paramName, alloca, paramStructType);
        if (paramType.kind == TypeKind::INTEGER)
        {
            currentScope->set_variable_signed(paramName, paramType.sign);
            currentScope->set_variable_non_zero(paramName, paramType.nonZero);
        }
        emit_non_zero_param_check(paramName, paramType);

        ++argIt;
        ++paramIdx;
    }

    if (method.body)
    {
        for (const auto& stmt : method.body->statements)
        {
            generate_statement(*stmt);
        }
    }
    else if (method.expressionBody)
    {
        llvm::Value* result = generate_expression(*method.expressionBody);
        if (!returnType->isVoidTy())
        {
            builder->CreateRet(result);
        }
        else
        {
            builder->CreateRetVoid();
        }
    }

    if (!builder->GetInsertBlock()->getTerminator())
    {
        if (returnType->isVoidTy())
        {
            builder->CreateRetVoid();
        }
        else
        {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    pop_scope();
}