//
// Created by Luke on 06/12/2025.
//

#include <iostream>

#include "../Generator.h"
#include "../Intrinsics.h"
#include "llvm/IR/Intrinsics.h"
#include <unordered_map>
#include <llvm/IR/InlineAsm.h>

#include "../../utils/Logger.h"

bool Generator::is_intrinsic(const std::string& name)
{
    return ::is_intrinsic(name);
}

llvm::Value* Generator::generate_intrinsic_call(const FunctionCall& call)
{
    const auto intrinsic = get_intrinsic(call.name.token_name);
    if (!intrinsic)
    {
        GENERATOR_ERROR(
            DiagnosticCode::UNDEFINED_INTRINSIC_FUNCTION,
            "unknown intrinsic: " + call.name.token_name,
            call.location
        );
    }

    switch (*intrinsic)
    {
    case Intrinsic::Sizeof:
        {
            if (call.arguments.empty())
            {
                GENERATOR_ERROR(
                    DiagnosticCode::INVALID_ARGUMENT_COUNT,
                    "sizeof requires 1 argument",
                    call.location
                );
            }

            // Check if the argument is a generic type parameter (e.g., sizeof(T))
            if (_currentGenericCtx)
            {
                if (const auto* ident = dynamic_cast<const Identifier*>(call.arguments[0].get()))
                {
                    if (const Type* resolved = _currentGenericCtx->resolve(ident->name()))
                    {
                        llvm::Type* llvmType = generate_type(*resolved);
                        const auto& dataLayout = module->getDataLayout();
                        const uint64_t size = dataLayout.getTypeAllocSize(llvmType);
                        return builder->getInt64(size);
                    }
                }
            }

            const llvm::Value* arg = generate_expression(*call.arguments[0]);
            const auto type = arg->getType();
            const auto& dataLayout = module->getDataLayout();
            const uint64_t size = dataLayout.getTypeAllocSize(type);
            return builder->getInt64(size);
        }

    case Intrinsic::Alignof:
        {
            if (call.arguments.empty())
            {
                GENERATOR_ERROR(
                    DiagnosticCode::INVALID_ARGUMENT_COUNT,
                    "alignof requires 1 argument",
                    call.location
                );
            }
            const llvm::Value* arg = generate_expression(*call.arguments[0]);
            llvm::Type* type = arg->getType();
            const llvm::DataLayout& dataLayout = module->getDataLayout();
            const uint64_t align = dataLayout.getABITypeAlign(type).value();
            return builder->getInt64(align);
        }

    case Intrinsic::Bitcast:
        {
            if (call.arguments.size() < 2)
            {
                GENERATOR_ERROR(
                    DiagnosticCode::INVALID_ARGUMENT_COUNT,
                    "bitcast requires 2 arguments (value, target_type_value)",
                    call.location
                );
            }

            const auto value = generate_expression(*call.arguments[0]);
            const llvm::Value* targetTypeValue = generate_expression(*call.arguments[1]);
            const auto targetType = targetTypeValue->getType();
            return builder->CreateBitCast(value, targetType, "bitcast");
        }

    case Intrinsic::Trap:
        {
            const auto trapFunc = llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::trap);
            if (!trapFunc)
                GENERATOR_ERROR(DiagnosticCode::UNDEFINED_INTRINSIC_FUNCTION, "missing intrinsic trap()",
                            call.location);

            builder->CreateCall(trapFunc);
            builder->CreateUnreachable();
            auto* deadBlock = llvm::BasicBlock::Create(*context, "after_trap", currentFunction);
            builder->SetInsertPoint(deadBlock);
            return nullptr;
        }

    case Intrinsic::DebugTrap:
        {
            const auto trapFunc =
                llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::debugtrap);
            if (!trapFunc)
                GENERATOR_ERROR(DiagnosticCode::UNDEFINED_INTRINSIC_FUNCTION, "missing intrinsic debugtrap()",
                            call.location);

            builder->CreateCall(trapFunc);
            builder->CreateUnreachable();
            auto* deadBlock = llvm::BasicBlock::Create(*context, "after_debugtrap", currentFunction);
            builder->SetInsertPoint(deadBlock);
            return nullptr;
        }

    // case Intrinsic::Abort:
    //     {
    //         llvm::FunctionType* abortTy =
    //             llvm::FunctionType::get(builder->getVoidTy(), false);
    //
    //         const llvm::FunctionCallee abortFunc =
    //             module->getOrInsertFunction("abort", abortTy);
    //
    //         builder->CreateCall(abortFunc);
    //         auto* unreachable = builder->CreateUnreachable();
    //         auto* deadBlock = llvm::BasicBlock::Create(*context, "after_abort", currentFunction);
    //         builder->SetInsertPoint(deadBlock);
    //         return unreachable;
    //     }

    case Intrinsic::Unreachable:
        {
            auto* unreachable = builder->CreateUnreachable();
            auto* deadBlock = llvm::BasicBlock::Create(*context, "after_unreachable", currentFunction);
            builder->SetInsertPoint(deadBlock);
            return unreachable;
        }

    case Intrinsic::Expect:
        {
            if (call.arguments.size() < 2)
            {
                GENERATOR_ERROR(
                    DiagnosticCode::INVALID_ARGUMENT_COUNT,
                    "expect requires 2 arguments",
                    call.location
                );
            }

            auto val = generate_expression(*call.arguments[0]);
            auto expected = generate_expression(*call.arguments[1]);
            expected = cast_value(expected, val->getType());
            const auto expectFunc = llvm::Intrinsic::getOrInsertDeclaration(
                module.get(), llvm::Intrinsic::expect, {val->getType()});
            return builder->CreateCall(expectFunc, {val, expected}, "expect");
        }

    case Intrinsic::Likely:
        {
            if (call.arguments.empty())
            {
                GENERATOR_ERROR(
                    DiagnosticCode::INVALID_ARGUMENT_COUNT,
                    "likely requires 1 argument",
                    call.location
                );
            }

            auto cond = generate_expression(*call.arguments[0]);
            const auto expectFunc = llvm::Intrinsic::getOrInsertDeclaration(
                module.get(), llvm::Intrinsic::expect, {cond->getType()});
            return builder->CreateCall(expectFunc, {cond, builder->getTrue()}, "likely");
        }

    case Intrinsic::Unlikely:
        {
            if (call.arguments.empty())
            {
                GENERATOR_ERROR(
                    DiagnosticCode::INVALID_ARGUMENT_COUNT,
                    "unlikely requires 1 argument",
                    call.location
                );
            }

            auto cond = generate_expression(*call.arguments[0]);
            const auto expectFunc = llvm::Intrinsic::getOrInsertDeclaration(
                module.get(), llvm::Intrinsic::expect, {cond->getType()});
            return builder->CreateCall(expectFunc, {cond, builder->getFalse()}, "unlikely");
        }

    case Intrinsic::Assume:
        {
            if (call.arguments.empty())
            {
                GENERATOR_ERROR(
                    DiagnosticCode::INVALID_ARGUMENT_COUNT,
                    "assume requires 1 argument",
                    call.location
                );
            }

            auto cond = generate_expression(*call.arguments[0]);
            cond = cast_value(cond, builder->getInt1Ty());
            const auto assumeFunc = llvm::Intrinsic::getOrInsertDeclaration(
                module.get(), llvm::Intrinsic::assume);
            builder->CreateCall(assumeFunc, {cond});
            return cond;
        }

    case Intrinsic::AwaitBlock:
        {
            if (call.arguments.empty())
            {
                GENERATOR_ERROR(
                    DiagnosticCode::INVALID_ARGUMENT_COUNT,
                    "await_block requires 1 argument",
                    call.location
                );
            }

            // Generate the operand (async function call returning coroutine handle)
            llvm::Value* handle = generate_expression(*call.arguments[0]);

            // Determine result type from the called async function
            llvm::Type* resultType = builder->getVoidTy();
            if (auto* funcCallArg = dynamic_cast<const FunctionCall*>(call.arguments[0].get()))
            {
                const std::string& funcName = funcCallArg->name.token_name;
                if (auto funcSym = symbols->lookupFunction(funcName))
                {
                    auto* fSym = dynamic_cast<FunctionSymbol*>(funcSym.get());
                    if (fSym && fSym->isAsync)
                    {
                        resultType = generate_type(fSym->returnType);
                    }
                }
                if (resultType->isVoidTy())
                {
                    const std::string resolved = currentScope->resolve_alias(funcName);
                    if (auto funcSym2 = symbols->lookupFunction(resolved))
                    {
                        auto* fSym2 = dynamic_cast<FunctionSymbol*>(funcSym2.get());
                        if (fSym2 && fSym2->isAsync)
                        {
                            resultType = generate_type(fSym2->returnType);
                        }
                    }
                }
            }

            return generate_await_loop(handle, resultType, call.location);
        }

    case Intrinsic::Typeof:
        {
            if (call.arguments.empty())
            {
                GENERATOR_ERROR(
                    DiagnosticCode::INVALID_ARGUMENT_COUNT,
                    "typeof requires 1 argument",
                    call.location
                );
            }

            std::string typeName = call.typeofResolvedName;

            // Fallback for generic type parameters (e.g., typeof(T) inside generic context)
            if (typeName.empty() && _currentGenericCtx)
            {
                if (const auto* ident = dynamic_cast<const Identifier*>(call.arguments[0].get()))
                {
                    if (const Type* resolved = _currentGenericCtx->resolve(ident->name()))
                    {
                        typeName = resolved->toHumanString();
                    }
                }
            }

            if (typeName.empty())
            {
                typeName = "unknown";
            }

            // Build a str struct: { i8* data, u32 len }
            const std::string resolvedStr = currentScope->resolve_alias("str");
            const StructDef* strDef = currentScope->lookup_struct(resolvedStr);
            if (!strDef || !strDef->llvmType)
            {
                GENERATOR_ERROR(
                    DiagnosticCode::UNDEFINED_STRUCT,
                    "tipo 'str' nao encontrado. Adicione: import std::types;",
                    call.location
                );
            }

            llvm::Value* globalPtr = builder->CreateGlobalStringPtr(typeName, ".typeof");
            auto* alloca = builder->CreateAlloca(strDef->llvmType, nullptr, "typeof_str");

            // data (field 0)
            builder->CreateStore(globalPtr,
                                 builder->CreateStructGEP(strDef->llvmType, alloca, 0));

            // len (field 1)
            builder->CreateStore(
                builder->getInt32(static_cast<uint32_t>(typeName.size())),
                builder->CreateStructGEP(strDef->llvmType, alloca, 1));

            return alloca;
        }
    }

    return nullptr;
}

llvm::Value* Generator::generate_function_call(const FunctionCall& expr)
{
    if (is_intrinsic(expr.name.token_name))
    {
        return generate_intrinsic_call(expr);
    }

    if (expr.isMethodCall())
    {
        return generate_method_call_internal(expr);
    }

    if (const size_t colonPos = expr.name.token_name.find("::"); colonPos != std::string::npos)
    {
        const auto enumName = expr.name.token_name.substr(0, colonPos);
        const auto variantName = expr.name.token_name.substr(colonPos + 2);

        // Resolve alias first (e.g., "optional" -> "std::types::optional")
        const auto resolvedEnumName = currentScope->resolve_alias(enumName);
        const EnumDef* enumDef = currentScope->lookup_enum(resolvedEnumName);

        if (expr.hasTypeArguments())
        {
            if (enumDef && enumDef->isGeneric)
            {
                monomorphize_enum(resolvedEnumName, expr.typeArguments);
                enumDef = currentScope->lookup_monomorphized_enum(resolvedEnumName, expr.typeArguments);
            }
        }

        if (enumDef)
        {
            if (const EnumVariantDef* variant = enumDef->getVariant(variantName))
            {
                return generate_enum_construction(*enumDef, *variant, expr.arguments);
            }
        }

        // Check for [intrinsic] struct static methods (e.g., coro::handle())
        const std::string resolvedStructName = currentScope->resolve_alias(enumName);
        if (const StructDef* intrinsicDef = currentScope->lookup_struct(resolvedStructName))
        {
            if (intrinsicDef->hasAttribute("intrinsic"))
            {
                return generate_intrinsic_method(expr, intrinsicDef, variantName);
            }
        }
    }

    // Error construction: MyError("message") — builtin-style implicit constructor
    if (resolve_error_struct(expr.name.token_name))
    {
        return generate_error_construction(expr);
    }

    // Check if this is a constructor call: Type(args) or Type<T>(args)
    // Resolve alias first (e.g., "Point" -> "mymodule::Point")
    const std::string resolvedTypeName = currentScope->resolve_alias(expr.name.token_name);

    // Extract simple name for constructor lookup (e.g., "Point" from "mymodule::Point")
    std::string simpleTypeName = expr.name.token_name;
    if (const size_t lastColon = simpleTypeName.rfind("::"); lastColon != std::string::npos)
    {
        simpleTypeName = simpleTypeName.substr(lastColon + 2);
    }

    if (const StructDef* structDef = currentScope->lookup_struct(resolvedTypeName))
    {
        const StructDef* targetDef = structDef;
        std::string targetStructName = structDef->name;

        // Handle generic struct constructor: e.g., array<i32>()
        if (expr.hasTypeArguments() && structDef->isGeneric)
        {
            monomorphize_struct(resolvedTypeName, expr.typeArguments);
            targetStructName = Mangler::mangle_generic_struct(resolvedTypeName, expr.typeArguments);
            targetDef = currentScope->lookup_struct(targetStructName);
        }

        if (targetDef)
        {
            // Constructor is mangled as StructName__SimpleName
            const std::string ctorName = targetStructName + "__" + simpleTypeName;
            if (const auto ctorIt = functions.find(ctorName); ctorIt != functions.end())
            {
                // This is a constructor call!
                llvm::Function* ctorFunc = ctorIt->second;

                // Allocate space for the struct
                llvm::AllocaInst* structAlloca = builder->CreateAlloca(targetDef->llvmType, nullptr, "ctor_tmp");

                // Prepare arguments: first arg is 'this' pointer
                std::vector<llvm::Value*> ctorArgs;
                ctorArgs.push_back(structAlloca);

                // Add the rest of the arguments
                const llvm::FunctionType* ctorType = ctorFunc->getFunctionType();
                size_t argIdx = 1; // Start from 1 because 0 is 'this'
                for (const auto& arg : expr.arguments)
                {
                    llvm::Value* argVal = generate_expression(*arg);
                    if (argIdx < ctorType->getNumParams() && ctorType->getParamType(argIdx)->isPointerTy())
                    {
                        argVal = coerce_str_to_ptr(argVal);
                    }
                    if (argIdx < ctorType->getNumParams())
                    {
                        llvm::Type* expectedType = ctorType->getParamType(argIdx);
                        if (is_object_type(expectedType) && argVal->getType() != expectedType)
                        {
                            argVal = box_value(argVal, get_djinn_type_name(*arg, argVal));
                        }
                        argVal = cast_value(argVal, expectedType);
                    }
                    ctorArgs.push_back(argVal);
                    argIdx++;
                }

                // Call the constructor (void return - initializes struct via 'this' pointer)
                builder->CreateCall(ctorFunc, ctorArgs);

                // Return the pointer to the initialized struct
                return structAlloca;
            }
        }
    }

    // Handle variadic forwarding: func(args, ...)
    // This requires using va_list and redirecting to v* functions (e.g., printf -> vprintf)
    // if (expr.hasVariadicForward) {
    //     return generate_variadic_forward_call(expr);
    // }

    // Resolve short-name aliases (namespace / file namespace) to the qualified
    // name used as the functions-map key (e.g. "division" -> "test::division")
    std::string calleeName = expr.name.token_name;
    if (!functions.contains(calleeName))
    {
        calleeName = currentScope->resolve_alias(calleeName);
    }

    const auto it = functions.find(calleeName);
    if (it == functions.end())
    {
        GENERATOR_ERROR(DiagnosticCode::UNDEFINED_FUNCTION,
                        "função não encontrada: " + expr.name.token_name,
                        expr.name.location);
    }

    llvm::Function* func = it->second;
    const llvm::FunctionType* funcType = func->getFunctionType();
    std::vector<llvm::Value*> args;

    // Check if function has [Location] parameter attributes
    const auto funcSym = symbols->lookupFunction(expr.name.token_name);
    bool hasTransparentParams = funcSym && funcSym->callerArity() != funcSym->arity();

    if (hasTransparentParams)
    {
        size_t userArgIdx = 0;
        for (size_t paramIdx = 0; paramIdx < funcSym->arity(); paramIdx++)
        {
            if (funcSym->paramHasAttribute(paramIdx, "Location"))
            {
                inject_location_argument(args, expr.name.location);
            }
            else if (userArgIdx < expr.arguments.size())
            {
                llvm::Value* argVal = generate_expression(*expr.arguments[userArgIdx]);
                const bool targetIsPtr = paramIdx < funcType->getNumParams()
                                             ? funcType->getParamType(paramIdx)->isPointerTy()
                                             : false;
                if (targetIsPtr)
                    argVal = coerce_str_to_ptr(argVal);
                if (paramIdx < funcType->getNumParams())
                {
                    llvm::Type* expectedType = funcType->getParamType(paramIdx);
                    if (is_object_type(expectedType) && argVal->getType() != expectedType)
                        argVal = box_value(argVal, get_djinn_type_name(*expr.arguments[userArgIdx], argVal));
                    argVal = cast_value(argVal, expectedType);
                }
                args.push_back(argVal);
                userArgIdx++;
            }
        }
    }
    else
    {
        size_t argIdx = 0;
        for (const auto& arg : expr.arguments)
        {
            llvm::Value* argVal = generate_expression(*arg);

            const bool targetIsPtr = argIdx < funcType->getNumParams()
                                         ? funcType->getParamType(argIdx)->isPointerTy()
                                         : funcType->isVarArg();
            if (targetIsPtr)
            {
                argVal = coerce_str_to_ptr(argVal);
            }

            if (argIdx < funcType->getNumParams())
            {
                llvm::Type* expectedType = funcType->getParamType(argIdx);
                if (is_object_type(expectedType) && argVal->getType() != expectedType)
                {
                    argVal = box_value(argVal, get_djinn_type_name(*arg, argVal));
                }
                argVal = cast_value(argVal, expectedType);
            }
            else if (funcType->isVarArg())
            {
                if (argVal->getType()->isIntegerTy() &&
                    argVal->getType()->getIntegerBitWidth() < 32)
                {
                    argVal = argVal->getType()->getIntegerBitWidth() == 1
                                 ? builder->CreateZExt(argVal, builder->getInt32Ty(), "vararg_promote")
                                 : builder->CreateSExt(argVal, builder->getInt32Ty(), "vararg_promote");
                }
            }
            args.push_back(argVal);
            argIdx++;
        }
    }

    const bool calleeThrows = funcSym && funcSym->isThrowing();
    auto call = emit_call_or_invoke(func, args, nativeExceptions && calleeThrows);
    auto isExternFunctionDeclaration = func->isDeclaration() && !func->isIntrinsic();
    if (isExternFunctionDeclaration)
    // if we are calling a extern function, we flag this call as no-optimize, otherwise llvm could wipe out this entire call.
    {
        func->setMemoryEffects(llvm::MemoryEffects::unknown());
        call->setCannotDuplicate();
        if (auto* callInst = llvm::dyn_cast<llvm::CallInst>(call))
            callInst->setTailCallKind(llvm::CallInst::TCK_None);
    }

    // Unchecked call to a throwing function inside another throwing function:
    // re-throw when the callee failed (error propagation; native mode
    // propagates by unwinding instead — the call above is already an invoke)
    if (currentFunctionThrows && !insideTryOperand_ && calleeThrows)
    {
        emit_error_propagation_check(expr.name.location);
    }

    return call;
}

llvm::Value* Generator::generate_new_expression(const NewExpression& expr)
{
    const auto& call = *expr.constructorCall;

    // Resolve the struct
    const std::string resolvedTypeName = currentScope->resolve_alias(call.name.token_name);
    const StructDef* structDef = currentScope->lookup_struct(resolvedTypeName);
    if (!structDef)
    {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                           "unknown type for new expression: " + call.name.token_name);
    }

    // Extract simple name for constructor lookup
    std::string simpleTypeName = call.name.token_name;
    if (const size_t lastColon = simpleTypeName.rfind("::"); lastColon != std::string::npos)
    {
        simpleTypeName = simpleTypeName.substr(lastColon + 2);
    }

    // Handle generic struct: e.g., new array<i32>()
    std::string targetStructName = structDef->name;
    if (call.hasTypeArguments() && structDef->isGeneric)
    {
        monomorphize_struct(resolvedTypeName, call.typeArguments);
        targetStructName = Mangler::mangle_generic_struct(resolvedTypeName, call.typeArguments);
        structDef = currentScope->lookup_struct(targetStructName);
        if (!structDef)
        {
            throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                               "failed to monomorphize type: " + call.name.token_name);
        }
    }

    // Find the constructor function
    const std::string ctorName = targetStructName + "__" + simpleTypeName;
    const auto ctorIt = functions.find(ctorName);
    if (ctorIt == functions.end())
    {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                           "no constructor found for type: " + call.name.token_name);
    }
    llvm::Function* ctorFunc = ctorIt->second;

    // Use __djinn_malloc from runtime for memory tracing
    llvm::Function* mallocFunc = module->getFunction("__djinn_malloc");
    if (!mallocFunc)
    {
        auto* mallocType = llvm::FunctionType::get(
            builder->getPtrTy(),
            {builder->getInt64Ty()},
            false);
        mallocFunc = llvm::Function::Create(
            mallocType, llvm::Function::ExternalLinkage, "__djinn_malloc", module.get());
    }

    // Calculate size of struct and call __djinn_malloc
    const auto& dataLayout = module->getDataLayout();
    const uint64_t structSize = dataLayout.getTypeAllocSize(structDef->llvmType);
    llvm::Value* sizeVal = builder->getInt64(structSize);
    llvm::Value* rawPtr = builder->CreateCall(mallocFunc, {sizeVal}, "new_raw");

    // Prepare constructor arguments: first arg is 'this' pointer
    std::vector<llvm::Value*> ctorArgs;
    ctorArgs.push_back(rawPtr);

    // Add the rest of the arguments
    const llvm::FunctionType* ctorType = ctorFunc->getFunctionType();
    size_t argIdx = 1; // Start from 1 because 0 is 'this'
    for (const auto& arg : call.arguments)
    {
        llvm::Value* argVal = generate_expression(*arg);
        if (argIdx < ctorType->getNumParams())
        {
            argVal = cast_value(argVal, ctorType->getParamType(argIdx));
        }
        ctorArgs.push_back(argVal);
        argIdx++;
    }

    // Call the constructor
    builder->CreateCall(ctorFunc, ctorArgs);

    // Return the pointer (the constructor modifies the struct through 'this')
    return rawPtr;
}

llvm::Value* Generator::generate_method_call_internal(const FunctionCall& call)
{
    LOG_DEBUG("[generator] method call: receiver->method='%s'", call.name.token_name.c_str());
    if (const auto* identDbg = dynamic_cast<const Identifier*>(call.receiver.get()))
    {
        LOG_DEBUG("[generator]   receiver identifier: '%s'", identDbg->identifier.token_name.c_str());
    }

    std::string structName;
    std::string llvmStructName;
    bool isStaticCall = false;
    const StructDef* structDef = nullptr;

    // Check if this is an enum method call first
    if (const auto* ident = dynamic_cast<const Identifier*>(call.receiver.get()))
    {
        // Check if the variable holds an enum value
        if (llvm::AllocaInst* alloca = currentScope->lookup_variable(ident->identifier.token_name))
        {
            if (auto* enumStructType = llvm::dyn_cast<llvm::StructType>(alloca->getAllocatedType()))
            {
                std::string enumTypeName = enumStructType->getName().str();
                if (EnumDef* enumDef = currentScope->lookup_enum(enumTypeName))
                {
                    // This is an enum method call
                    llvm::Value* enumValue = builder->CreateLoad(enumStructType, alloca, "enum_load");

                    // Handle built-in enum methods
                    if (call.name.token_name == "value")
                    {
                        // .value() extracts the payload from the first variant with payload
                        // Find the first variant with a payload
                        for (size_t i = 0; i < enumDef->variants.size(); ++i)
                        {
                            if (!enumDef->variants[i].associatedTypes.empty())
                            {
                                return extract_enum_payload(enumValue, *enumDef, i);
                            }
                        }
                        throw CompileError(DiagnosticCode::TYPE_MISMATCH,
                                           "enum has no variant with payload");
                    }
                    if (call.name.token_name == "tag")
                    {
                        // .tag() returns the discriminant
                        return extract_enum_tag(enumValue, *enumDef);
                    }
                    throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                                       "unknown enum method: " + call.name.token_name);
                }
            }
        }
    }

    if (const auto* ident = dynamic_cast<const Identifier*>(call.receiver.get()))
    {
        // Check variable first: if a variable exists with this name, it's an instance method call,
        // even if the name also matches a struct (e.g., `array<i32> array = ...; array.push(42)`)
        if (const auto* alloca = currentScope->lookup_variable(ident->identifier.token_name))
        {
            // Receiver is a variable - instance method call
            structName = currentScope->lookup_variable_struct_type(ident->identifier.token_name);
            LOG_DEBUG("[generator]   lookup_variable_struct_type('%s') = '%s'",
                      ident->identifier.token_name.c_str(), structName.c_str());
            LOG_DEBUG("[generator]   alloca type: '%s'",
                      alloca->getAllocatedType()->isStructTy()
                      ? llvm::dyn_cast<llvm::StructType>(alloca->getAllocatedType())->getName().str().c_str()
                      : alloca->getAllocatedType()->isPointerTy()
                      ? "pointer"
                      : alloca->getAllocatedType()->isIntegerTy()
                      ? "integer"
                      : "other");
            if (const auto* structType = llvm::dyn_cast<llvm::StructType>(alloca->getAllocatedType()))
            {
                llvmStructName = structType->getName().str();
                LOG_DEBUG("[generator]   llvmStructName: '%s'", llvmStructName.c_str());
            }
        }
        else
        {
            // No variable with this name — check if it's a struct name for a static call
            structDef = currentScope->lookup_struct(ident->identifier.token_name);
            if (structDef)
            {
                structName = structDef->name;
                isStaticCall = true;
                LOG_DEBUG("[generator]   found as static struct: '%s'", structName.c_str());
            }
            else
            {
                LOG_DEBUG("[generator]   variable '%s' NOT found in scope",
                          ident->identifier.token_name.c_str());
            }
        }
    }

    // If no struct type found, check if it's a primitive type with an impl
    if (structName.empty())
    {
        LOG_DEBUG("[generator]   structName still empty, trying primitive type impl lookup");
        if (const auto* ident = dynamic_cast<const Identifier*>(call.receiver.get()))
        {
            if (llvm::AllocaInst* alloca = currentScope->lookup_variable(ident->identifier.token_name))
            {
                structName = get_primitive_type_name(alloca->getAllocatedType());
                LOG_DEBUG("[generator]   primitive type name: '%s'", structName.c_str());
            }
        }
        else
        {
            // For complex receivers (e.g., this.type.hash()), generate the expression
            // and infer the type from the resulting LLVM value
            llvm::Value* receiverVal = generate_expression(*call.receiver);
            if (receiverVal)
            {
                llvm::Type* recvType = receiverVal->getType();
                // If it's a pointer, check tracked pointee type from field access
                if (recvType->isPointerTy())
                {
                    // Use _lastFieldAccessPointeeType set by generate_field_access
                    if (_lastFieldAccessPointeeType)
                    {
                        if (auto* structTy = llvm::dyn_cast<llvm::StructType>(_lastFieldAccessPointeeType))
                        {
                            structName = _lastFieldAccessStructName.empty()
                                             ? structTy->getName().str()
                                             : _lastFieldAccessStructName;
                        }
                    }
                    // Fallback: check if it's a loaded struct type
                    else if (auto* loadInst = llvm::dyn_cast<llvm::LoadInst>(receiverVal))
                    {
                        if (auto* structTy = llvm::dyn_cast<llvm::StructType>(loadInst->getType()))
                        {
                            structName = structTy->getName().str();
                        }
                    }
                }
                else if (auto* structTy = llvm::dyn_cast<llvm::StructType>(recvType))
                {
                    structName = structTy->getName().str();
                }
                else
                {
                    structName = get_primitive_type_name(recvType);
                }
                LOG_DEBUG("[generator]   complex receiver type resolved: '%s'", structName.c_str());
            }
        }
    }

    if (structName.empty())
    {
        LOG_DEBUG("[generator]   FAILED: structName empty, throwing 'cannot call method on non-struct type': %s",
                  call.name.token_name.c_str());
        GENERATOR_ERROR(DiagnosticCode::TYPE_MISMATCH, "cannot call method on non-struct type", call.name.location);
    }

    const auto methodStructName = llvmStructName.empty() ? structName : llvmStructName;

    // Check for task<T> intrinsic methods via [no_mangle] attribute
    if (!isStaticCall)
    {
        const StructDef* taskDef = currentScope->lookup_struct(methodStructName);
        if (!taskDef) taskDef = currentScope->lookup_struct(structName);
        if (taskDef && taskDef->hasAttribute("no_mangle"))
        {
            // Extract the base name from the struct (e.g., "std::sys::task" -> "task")
            std::string baseName = taskDef->name;
            if (auto pos = baseName.rfind("::"); pos != std::string::npos)
                baseName = baseName.substr(pos + 2);
            // Monomorphized names contain the base name before 'I'
            if (auto pos = baseName.find("I"); pos != std::string::npos)
                baseName = baseName.substr(0, pos);
            // Also strip mangling prefix _ZN...
            if (baseName.starts_with("_ZN"))
            {
                // Extract name from Itanium mangling: _ZN<len><name>...
                size_t i = 3;
                size_t len = 0;
                while (i < baseName.size() && std::isdigit(baseName[i]))
                {
                    len = len * 10 + (baseName[i] - '0');
                    i++;
                }
                if (len > 0 && i + len <= baseName.size())
                    baseName = baseName.substr(i, len);
            }
            // Strip qualified prefix if present
            if (auto pos2 = baseName.rfind("::"); pos2 != std::string::npos)
                baseName = baseName.substr(pos2 + 2);

            if (baseName == "task")
            {
                if (const auto* ident = dynamic_cast<const Identifier*>(call.receiver.get()))
                {
                    if (llvm::AllocaInst* alloca = currentScope->lookup_variable(ident->identifier.token_name))
                    {
                        return generate_task_intrinsic_method(call, alloca, taskDef);
                    }
                }
            }
        }
    }

    // Check for [intrinsic] struct static methods via dot syntax (e.g., coro.handle())
    if (isStaticCall)
    {
        const StructDef* intrinsicDef = currentScope->lookup_struct(structName);
        if (intrinsicDef && intrinsicDef->hasAttribute("intrinsic"))
        {
            LOG_TRACE("[generator] generate intrinsic call for %s", intrinsicDef->name.c_str());
            return generate_intrinsic_method(call, intrinsicDef, call.name.token_name);
        }
    }

    const auto mangledName = methodStructName + "__" + call.name.token_name;
    llvm::Function* func = nullptr;

    // First try the global functions map
    if (const auto it = functions.find(mangledName); it != functions.end())
    {
        func = it->second;
    }
    // Fallback: check struct's methodFunctions (handles ordering when method not yet in global map)
    if (!func)
    {
        if (StructDef* def = currentScope->lookup_struct(methodStructName))
        {
            // First check already-generated methods
            if (const auto it = def->methodFunctions.find(call.name.token_name); it != def->methodFunctions.end())
            {
                func = it->second;
            }
            // If method exists in symbol but not yet generated, forward-declare it on demand
            if (!func)
            {
                func = module->getFunction(mangledName);
                if (func)
                {
                    functions[mangledName] = func;
                    def->methodFunctions[call.name.token_name] = func;
                }
            }
            if (!func && def->llvmType && !def->fields.empty())
            {
                for (const auto& method : def->methods)
                {
                    if (method->name == call.name.token_name)
                    {
                        llvm::Type* retType = method->isAsync
                                                  ? llvm::PointerType::getUnqual(*context)
                                                  : generate_type(method->returnType);

                        std::vector<llvm::Type*> paramTypes;
                        if (!method->isStatic)
                        {
                            paramTypes.push_back(llvm::PointerType::get(def->llvmType, 0));
                        }
                        for (const auto& pt : method->paramTypes)
                        {
                            paramTypes.push_back(generate_type(pt));
                        }

                        auto* funcType = llvm::FunctionType::get(retType, paramTypes, false);
                        func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, mangledName, *module);

                        apply_attributes(func, method->attributes);
                        apply_implicit_attributes(func);

                        functions[mangledName] = func;
                        def->methodFunctions[call.name.token_name] = func;
                        break;
                    }
                }
            }
        }
    }
    if (!func)
    {
        GENERATOR_ERROR(
            DiagnosticCode::UNDEFINED_FUNCTION,
            "method not found: " + structName + "." + call.name.token_name,
            call.name.location
        );
    }
    std::vector<llvm::Value*> args;

    if (isStaticCall)
    {
        // Static method call - verify the method is actually static
        if (structDef)
        {
            bool methodIsStatic = false;

            for (const auto& method : structDef->methods)
            {
                if (method->name == call.name.token_name)
                {
                    methodIsStatic = method->isStatic;
                    break;
                }
            }

            if (!methodIsStatic)
            {
                throw CompileError(DiagnosticCode::TYPE_MISMATCH,
                                   "cannot call instance method '" + call.name.token_name +
                                   "' without an instance of " + structName);
            }
        }
        // No self argument for static methods
    }
    else
    {
        // const auto a = structDef->getMethod(call.name.token_name);
        // Instance method call - pass pointer to receiver as self
        // Must pass the original alloca, NOT a copy, so mutations are visible to the caller
        if (const auto* ident = dynamic_cast<const Identifier*>(call.receiver.get()))
        {
            if (llvm::AllocaInst* alloca = currentScope->lookup_variable(ident->identifier.token_name))
            {
                if (alloca->getAllocatedType()->isPointerTy())
                {
                    args.push_back(builder->CreateLoad(alloca->getAllocatedType(), alloca, "self"));
                }
                else if (alloca->getAllocatedType()->isStructTy())
                {
                    args.push_back(alloca);
                }
                else
                {
                    args.push_back(builder->CreateLoad(alloca->getAllocatedType(), alloca, "this_val"));
                }
            }
            else
            {
                GENERATOR_ERROR(
                    DiagnosticCode::UNDEFINED_VARIABLE,
                    "variable not found: " + ident->identifier.token_name,
                    ident->identifier.location
                );
            }
        }
        else
        {
            // Complex receiver (field access, etc.) - evaluate and store in temp
            llvm::Value* objectValue = generate_expression(*call.receiver);
            if (objectValue->getType()->isPointerTy())
            {
                args.push_back(objectValue);
            }
            else
            {
                // Check if the method expects a value parameter (primitive impl)
                const llvm::FunctionType* fType = func->getFunctionType();
                if (fType->getNumParams() > 0 && !fType->getParamType(0)->isPointerTy())
                {
                    args.push_back(objectValue);
                }
                else
                {
                    const auto alloca = builder->CreateAlloca(objectValue->getType(), nullptr, "tmp");
                    builder->CreateStore(objectValue, alloca);
                    args.push_back(alloca);
                }
            }
        }
    }

    const llvm::FunctionType* funcType = func->getFunctionType();

    // Check if this method is a Djinn variadic (uses ...args auto-boxing)
    const MethodSymbol* variadicMethod = nullptr;
    if (structDef)
    {
        for (const auto& method : structDef->methods)
        {
            if (method->name == call.name.token_name && method->isVariadic())
            {
                variadicMethod = method.get();
                break;
            }
        }
    }

    if (variadicMethod)
    {
        // Djinn variadic: auto-box extra arguments into arr<object>
        // Non-variadic params come first (excluding the synthesized arr<object> param)
        const size_t normalParamCount = variadicMethod->paramTypes.size() - 1; // last param is arr<object>
        size_t argIdx = isStaticCall ? 0 : 1;

        // Generate normal (non-variadic) arguments
        for (size_t i = 0; i < normalParamCount && i < call.arguments.size(); ++i)
        {
            llvm::Value* argVal = generate_expression(*call.arguments[i]);
            if (argIdx < funcType->getNumParams())
            {
                llvm::Type* expectedType = funcType->getParamType(argIdx);
                // Load struct value if we have a pointer (alloca) but function expects value
                if (argVal->getType()->isPointerTy() && expectedType->isStructTy())
                {
                    argVal = builder->CreateLoad(expectedType, argVal, "arg_load");
                }
                else
                {
                    argVal = cast_value(argVal, expectedType);
                }
            }
            args.push_back(argVal);
            argIdx++;
        }

        // Box remaining arguments into arr<object>
        args.push_back(emit_boxed_varargs_array(call.arguments, normalParamCount));
    }
    else
    {
        // Normal (non-variadic) argument generation
        size_t argIdx = isStaticCall ? 0 : 1;
        for (const auto& arg : call.arguments)
        {
            llvm::Value* argVal = generate_expression(*arg);

            const bool targetIsPtr = argIdx < funcType->getNumParams()
                                         ? funcType->getParamType(argIdx)->isPointerTy()
                                         : funcType->isVarArg();
            if (targetIsPtr)
            {
                argVal = coerce_str_to_ptr(argVal);
            }

            if (argIdx < funcType->getNumParams())
            {
                llvm::Type* expectedType = funcType->getParamType(argIdx);
                if (is_object_type(expectedType) && argVal->getType() != expectedType)
                {
                    argVal = box_value(argVal, get_djinn_type_name(*arg, argVal));
                }
                argVal = cast_value(argVal, expectedType);
            }
            else if (funcType->isVarArg())
            {
                if (argVal->getType()->isIntegerTy() &&
                    argVal->getType()->getIntegerBitWidth() < 32)
                {
                    argVal = argVal->getType()->getIntegerBitWidth() == 1
                                 ? builder->CreateZExt(argVal, builder->getInt32Ty(), "vararg_promote")
                                 : builder->CreateSExt(argVal, builder->getInt32Ty(), "vararg_promote");
                }
            }
            args.push_back(argVal);
            argIdx++;
        }
    }

    // Inject compiler-provided arguments for [Location] parameter attributes
    if (structDef)
    {
        if (const auto method = structDef->getMethod(call.name.token_name))
        {
            for (size_t i = 0; i < method->paramAttributes.size(); i++)
            {
                if (method->paramHasAttribute(i, "Location"))
                {
                    inject_location_argument(args, call.name.location);
                }
            }
        }
    }

    const auto structSym2 = symbols->lookupStruct(structName);
    const auto methodSym2 = structSym2 ? structSym2->getMethod(call.name.token_name) : nullptr;
    auto* methodCall = emit_call_or_invoke(func, args, nativeExceptions && methodSym2 && methodSym2->isThrowing());

    // Unchecked call to a throwing method inside another throwing function:
    // re-throw when the callee failed (error propagation; native mode
    // propagates by unwinding instead)
    if (currentFunctionThrows && !insideTryOperand_)
    {
        if (methodSym2 && methodSym2->isThrowing())
        {
            emit_error_propagation_check(call.name.location);
        }
    }

    return methodCall;
}

void Generator::inject_location_argument(std::vector<llvm::Value*>& args, const SourceLocation& callSite)
{
    std::string fileStr = callSite.fileId.empty() ? "<unknown>" : callSite.fileId;
    auto* fileConst = builder->CreateGlobalStringPtr(fileStr, "loc_file");
    auto* lineConst = builder->getInt32(callSite.line);
    auto* colConst = builder->getInt32(callSite.column);

    const std::string resolvedLocation = currentScope->resolve_alias("Location");
    StructDef* locationDef = currentScope->lookup_struct(resolvedLocation);
    if (locationDef && locationDef->llvmType)
    {
        auto* locAlloca = builder->CreateAlloca(locationDef->llvmType, nullptr, "caller_loc");
        builder->CreateStore(fileConst, builder->CreateStructGEP(locationDef->llvmType, locAlloca, 0));
        builder->CreateStore(lineConst, builder->CreateStructGEP(locationDef->llvmType, locAlloca, 1));
        builder->CreateStore(colConst, builder->CreateStructGEP(locationDef->llvmType, locAlloca, 2));
        args.push_back(builder->CreateLoad(locationDef->llvmType, locAlloca, "loc"));
    }
    else
    {
        args.push_back(fileConst);
        args.push_back(lineConst);
        args.push_back(colConst);
    }
}

llvm::Function* Generator::resolve_static_method_function(const std::string& structName,
                                                          const std::string& methodName)
{
    StructDef* def = currentScope->lookup_struct(structName);
    const std::string resolvedName = def ? def->name : structName;
    const std::string mangledName = resolvedName + "__" + methodName;

    if (const auto it = functions.find(mangledName); it != functions.end())
    {
        return it->second;
    }

    if (def)
    {
        if (const auto it = def->methodFunctions.find(methodName); it != def->methodFunctions.end())
        {
            return it->second;
        }

        if (llvm::Function* fn = module->getFunction(mangledName))
        {
            functions[mangledName] = fn;
            def->methodFunctions[methodName] = fn;
            return fn;
        }

        // Method exists in the symbol table but was not generated yet:
        // forward-declare it on demand
        for (const auto& method : def->methods)
        {
            if (method->name != methodName) continue;

            llvm::Type* retType = method->isAsync
                                      ? llvm::PointerType::getUnqual(*context)
                                      : generate_type(method->returnType);

            std::vector<llvm::Type*> paramTypes;
            if (!method->isStatic)
            {
                paramTypes.push_back(llvm::PointerType::get(def->llvmType, 0));
            }
            for (const auto& pt : method->paramTypes)
            {
                paramTypes.push_back(generate_type(pt));
            }

            auto* funcType = llvm::FunctionType::get(retType, paramTypes, false);
            auto* fn = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, mangledName, *module);

            apply_attributes(fn, method->attributes);
            apply_implicit_attributes(fn);

            functions[mangledName] = fn;
            def->methodFunctions[methodName] = fn;
            return fn;
        }
    }

    return nullptr;
}

llvm::Value* Generator::emit_boxed_varargs_array(const std::vector<std::unique_ptr<Expression>>& args,
                                                 const size_t normalParamCount)
{
    const std::string resolvedObject = currentScope->resolve_alias("object");
    StructDef* objectDef = currentScope->lookup_struct(resolvedObject);

    const size_t numVariadicArgs = args.size() > normalParamCount
                                       ? args.size() - normalParamCount
                                       : 0;

    // Monomorphize arr<object>
    Type objectType = Type::struct_type("object");
    std::vector<Type> typeArgs = {objectType};
    llvm::StructType* arrObjectType = monomorphize_struct("arr", typeArgs);

    if (numVariadicArgs == 0)
    {
        // Empty varargs: pass empty arr<object>
        auto* arrAlloca = builder->CreateAlloca(arrObjectType, nullptr, "varargs_arr");
        auto* dataField = builder->CreateStructGEP(arrObjectType, arrAlloca, 0, "arr.data");
        builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), dataField);
        auto* lenField = builder->CreateStructGEP(arrObjectType, arrAlloca, 1, "arr.len");
        builder->CreateStore(builder->getInt32(0), lenField);

        return builder->CreateLoad(arrObjectType, arrAlloca, "varargs");
    }

    // Stack-allocate array of objects
    auto* arrayData = builder->CreateAlloca(
        objectDef->llvmType,
        builder->getInt32(static_cast<uint32_t>(numVariadicArgs)),
        "varargs_data"
    );

    // Box each variadic argument
    for (size_t i = 0; i < numVariadicArgs; ++i)
    {
        const auto& argExpr = *args[normalParamCount + i];
        llvm::Value* argVal = generate_expression(argExpr);
        std::string typeName = get_djinn_type_name(argExpr, argVal);
        llvm::Value* boxed = box_value(argVal, typeName);

        auto* gep = builder->CreateGEP(objectDef->llvmType, arrayData,
                                       builder->getInt32(static_cast<uint32_t>(i)), "vararg_slot");
        builder->CreateStore(boxed, gep);
    }

    // Build arr<object> struct: { object* data, size length }
    auto* arrAlloca = builder->CreateAlloca(arrObjectType, nullptr, "varargs_arr");
    auto* dataField = builder->CreateStructGEP(arrObjectType, arrAlloca, 0, "arr.data");
    builder->CreateStore(arrayData, dataField);
    auto* lenField = builder->CreateStructGEP(arrObjectType, arrAlloca, 1, "arr.len");
    builder->CreateStore(builder->getInt32(static_cast<uint32_t>(numVariadicArgs)), lenField);

    return builder->CreateLoad(arrObjectType, arrAlloca, "varargs");
}
