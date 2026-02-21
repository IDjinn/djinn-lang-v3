//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"
#include "../Intrinsics.h"
#include "llvm/IR/Intrinsics.h"
#include <unordered_map>
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
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION, "unknown intrinsic: " + call.name.token_name);
    }

    switch (*intrinsic)
    {
    case Intrinsic::Sizeof:
        {
            if (call.arguments.empty())
            {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "sizeof requires 1 argument");
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
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "alignof requires 1 argument");
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
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT,
                                   "bitcast requires 2 arguments (value, target_type_value)");
            }

            const auto value = generate_expression(*call.arguments[0]);
            const llvm::Value* targetTypeValue = generate_expression(*call.arguments[1]);
            const auto targetType = targetTypeValue->getType();
            return builder->CreateBitCast(value, targetType, "bitcast");
        }

    case Intrinsic::Trap:
        {
            const auto trapFunc =
                llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::trap);

            builder->CreateCall(trapFunc);
            builder->CreateUnreachable();
            return nullptr;
        }

    case Intrinsic::DebugTrap:
        {
            const auto trapFunc =
                llvm::Intrinsic::getOrInsertDeclaration(module.get(), llvm::Intrinsic::debugtrap);

            builder->CreateCall(trapFunc);
            builder->CreateUnreachable();
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
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "expect requires 2 arguments");
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
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "likely requires 1 argument");
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
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "unlikely requires 1 argument");
            }

            auto cond = generate_expression(*call.arguments[0]);
            const auto expectFunc = llvm::Intrinsic::getOrInsertDeclaration(
                module.get(), llvm::Intrinsic::expect, {cond->getType()});
            return builder->CreateCall(expectFunc, {cond, builder->getFalse()}, "unlikely");
        }

    case Intrinsic::Typeof:
        {
            if (call.arguments.empty())
            {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "typeof requires 1 argument");
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
            StructDef* strDef = currentScope->lookup_struct(resolvedStr);
            if (!strDef || !strDef->llvmType)
            {
                throw CompileError(DiagnosticCode::UNDEFINED_STRUCT,
                                   "tipo 'str' nao encontrado. Adicione: import std::types;");
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
                        argVal = cast_value(argVal, ctorType->getParamType(argIdx));
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

    const auto it = functions.find(expr.name.token_name);
    if (it == functions.end())
    {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION, "função não encontrada: " + expr.name.token_name);
    }

    llvm::Function* func = it->second;
    const llvm::FunctionType* funcType = func->getFunctionType();

    std::vector<llvm::Value*> args;
    size_t argIdx = 0;
    for (const auto& arg : expr.arguments)
    {
        llvm::Value* argVal = generate_expression(*arg);

        // Only coerce str/slice -> ptr when the target parameter expects a pointer
        const bool targetIsPtr = argIdx < funcType->getNumParams()
                                     ? funcType->getParamType(argIdx)->isPointerTy()
                                     : funcType->isVarArg(); // variadic extra args always need raw ptrs
        if (targetIsPtr)
        {
            argVal = coerce_str_to_ptr(argVal);
        }

        if (argIdx < funcType->getNumParams())
        {
            argVal = cast_value(argVal, funcType->getParamType(argIdx));
        }
        else if (funcType->isVarArg())
        {
            if (argVal->getType()->isIntegerTy() &&
                argVal->getType()->getIntegerBitWidth() < 32)
            {
                argVal = builder->CreateSExt(argVal, builder->getInt32Ty(), "vararg_promote");
            }
        }
        args.push_back(argVal);
        argIdx++;
    }

    return builder->CreateCall(func, args);
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

    // Declare malloc if not already declared
    llvm::Function* mallocFunc = module->getFunction("malloc");
    if (!mallocFunc)
    {
        auto* mallocType = llvm::FunctionType::get(
            builder->getPtrTy(),
            {builder->getInt64Ty()},
            false);
        mallocFunc = llvm::Function::Create(
            mallocType, llvm::Function::ExternalLinkage, "malloc", module.get());
    }

    // Calculate size of struct and call malloc
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
        structDef = currentScope->lookup_struct(ident->identifier.token_name);
        if (structDef)
        {
            // Receiver is a struct name (e.g., Console.printf) - this is a static call
            structName = structDef->name;
            isStaticCall = true;
            LOG_DEBUG("[generator]   found as static struct: '%s'", structName.c_str());
        }
        else
        {
            // Receiver is a variable - instance method call
            structName = currentScope->lookup_variable_struct_type(ident->identifier.token_name);
            LOG_DEBUG("[generator]   lookup_variable_struct_type('%s') = '%s'",
                      ident->identifier.token_name.c_str(), structName.c_str());
            if (const auto* alloca = currentScope->lookup_variable(ident->identifier.token_name))
            {
                LOG_DEBUG("[generator]   alloca type: '%s'",
                          alloca->getAllocatedType()->isStructTy()
                          ? llvm::dyn_cast<llvm::StructType>(alloca->getAllocatedType())->getName().str().c_str()
                          : alloca->getAllocatedType()->isPointerTy() ? "pointer"
                          : alloca->getAllocatedType()->isIntegerTy() ? "integer"
                          : "other");
                if (const auto* structType = llvm::dyn_cast<llvm::StructType>(alloca->getAllocatedType()))
                {
                    llvmStructName = structType->getName().str();
                    LOG_DEBUG("[generator]   llvmStructName: '%s'", llvmStructName.c_str());
                }
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
    }

    if (structName.empty())
    {
        LOG_DEBUG("[generator]   FAILED: structName empty, throwing 'cannot call method on non-struct type'");
        throw CompileError(DiagnosticCode::TYPE_MISMATCH,
                           "cannot call method on non-struct type");
    }

    const auto methodStructName = llvmStructName.empty() ? structName : llvmStructName;
    const auto mangledName = methodStructName + "__" + call.name.token_name;

    if (const auto it = functions.find(mangledName); it == functions.end())
    {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                           "method not found: " + structName + "." + call.name.token_name);
    }

    llvm::Function* func = functions[mangledName];
    std::vector<llvm::Value*> args;

    if (isStaticCall)
    {
        // Static method call - verify the method is actually static
        if (structDef)
        {
            bool methodIsStatic = false;
            std::string variadicForwardTarget;

            for (const auto& method : structDef->methods)
            {
                if (method->name == call.name.token_name)
                {
                    methodIsStatic = method->isStatic;
                    variadicForwardTarget = method->variadicForwardTarget;
                    break;
                }
            }

            if (!methodIsStatic)
            {
                throw CompileError(DiagnosticCode::TYPE_MISMATCH,
                                   "cannot call instance method '" + call.name.token_name +
                                   "' without an instance of " + structName);
            }

            // Handle variadic forwarding: Console.printf(fmt, a, b) -> printf(fmt, a, b)
            if (!variadicForwardTarget.empty())
            {
                // Find the target function
                auto targetIt = functions.find(variadicForwardTarget);
                if (targetIt == functions.end())
                {
                    throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                                       "variadic forward target not found: " + variadicForwardTarget);
                }

                llvm::Function* targetFunc = targetIt->second;
                const llvm::FunctionType* targetFuncType = targetFunc->getFunctionType();

                // Generate all arguments and call target function directly
                std::vector<llvm::Value*> targetArgs;
                size_t targetArgIdx = 0;
                for (const auto& arg : call.arguments)
                {
                    llvm::Value* argVal = generate_expression(*arg);

                    if (targetArgIdx < targetFuncType->getNumParams())
                    {
                        argVal = cast_value(argVal, targetFuncType->getParamType(targetArgIdx));
                    }
                    else if (targetFuncType->isVarArg())
                    {
                        // Promote small integers for variadic functions (C ABI requirement)
                        if (argVal->getType()->isIntegerTy() &&
                            argVal->getType()->getIntegerBitWidth() < 32)
                        {
                            argVal = builder->CreateSExt(argVal, builder->getInt32Ty(), "vararg_promote");
                        }
                    }
                    targetArgs.push_back(argVal);
                    targetArgIdx++;
                }

                return builder->CreateCall(targetFunc, targetArgs);
            }
        }
        // No self argument for static methods
    }
    else
    {
        // Instance method call - pass pointer to receiver as self
        // Must pass the original alloca, NOT a copy, so mutations are visible to the caller
        if (const auto* ident = dynamic_cast<const Identifier*>(call.receiver.get()))
        {
            if (llvm::AllocaInst* alloca = currentScope->lookup_variable(ident->identifier.token_name))
            {
                if (alloca->getAllocatedType()->isPointerTy())
                {
                    // 'this' or pointer variable: alloca holds a ptr to the struct, load it
                    args.push_back(builder->CreateLoad(alloca->getAllocatedType(), alloca, "self"));
                }
                else if (!alloca->getAllocatedType()->isStructTy())
                {
                    // Primitive type impl: pass 'this' by value (load the value)
                    args.push_back(builder->CreateLoad(alloca->getAllocatedType(), alloca, "this_val"));
                }
                else
                {
                    // Local struct variable: alloca IS the ptr to the struct
                    args.push_back(alloca);
                }
            }
            else
            {
                throw CompileError(DiagnosticCode::UNDEFINED_VARIABLE,
                                   "variable not found: " + ident->identifier.token_name);
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
    size_t argIdx = isStaticCall ? 0 : 1; // Account for 'this' parameter in instance methods
    for (const auto& arg : call.arguments)
    {
        llvm::Value* argVal = generate_expression(*arg);

        // Only coerce str/slice -> ptr when the target parameter expects a pointer
        const bool targetIsPtr = argIdx < funcType->getNumParams()
                                     ? funcType->getParamType(argIdx)->isPointerTy()
                                     : funcType->isVarArg();
        if (targetIsPtr)
        {
            argVal = coerce_str_to_ptr(argVal);
        }

        if (argIdx < funcType->getNumParams())
        {
            argVal = cast_value(argVal, funcType->getParamType(argIdx));
        }
        else if (funcType->isVarArg())
        {
            // Promote small integers for variadic functions (C ABI requirement)
            if (argVal->getType()->isIntegerTy() &&
                argVal->getType()->getIntegerBitWidth() < 32)
            {
                argVal = builder->CreateSExt(argVal, builder->getInt32Ty(), "vararg_promote");
            }
        }
        args.push_back(argVal);
        argIdx++;
    }

    // Inject caller file and line for Debug::assert calls
    if (isStaticCall && call.name.token_name == "assert" && structName.find("Debug") != std::string::npos)
    {
        std::string fileStr = call.name.location.fileId.empty() ? "<unknown>" : call.name.location.fileId;
        auto* fileConst = builder->CreateGlobalStringPtr(fileStr, "assert_file");
        args.push_back(fileConst);
        args.push_back(builder->getInt32(call.name.location.line));
    }

    return builder->CreateCall(func, args);
}