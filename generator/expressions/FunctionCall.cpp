//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"
#include "../Intrinsics.h"
#include "llvm/IR/Intrinsics.h"

bool Generator::is_intrinsic(const std::string &name) {
    return ::is_intrinsic(name);
}

llvm::Value *Generator::generate_intrinsic_call(const FunctionCall &call) {
    const auto intrinsic = get_intrinsic(call.name.token_name);
    if (!intrinsic) {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION, "unknown intrinsic: " + call.name.token_name);
    }

    switch (*intrinsic) {
        case Intrinsic::Sizeof: {
            if (call.arguments.empty()) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "sizeof requires 1 argument");
            }

            const llvm::Value *arg = generate_expression(*call.arguments[0]);
            const auto type = arg->getType();
            const auto &dataLayout = module->getDataLayout();
            const uint64_t size = dataLayout.getTypeAllocSize(type);
            return builder->getInt64(size);
        }

        case Intrinsic::Alignof: {
            if (call.arguments.empty()) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "alignof requires 1 argument");
            }
            const llvm::Value *arg = generate_expression(*call.arguments[0]);
            llvm::Type *type = arg->getType();
            const llvm::DataLayout &dataLayout = module->getDataLayout();
            const uint64_t align = dataLayout.getABITypeAlign(type).value();
            return builder->getInt64(align);
        }

        case Intrinsic::Bitcast: {
            if (call.arguments.size() < 2) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT,
                                   "bitcast requires 2 arguments (value, target_type_value)");
            }

            const auto value = generate_expression(*call.arguments[0]);
            const llvm::Value *targetTypeValue = generate_expression(*call.arguments[1]);
            const auto targetType = targetTypeValue->getType();
            return builder->CreateBitCast(value, targetType, "bitcast");
        }

        case Intrinsic::Trap: {
            const auto trapFunc = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::trap);
            builder->CreateCall(trapFunc);
            return builder->CreateUnreachable();
        }

        case Intrinsic::DebugTrap: {
            const auto trapFunc = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::debugtrap);
            builder->CreateCall(trapFunc);
            return builder->CreateUnreachable();
        }

        case Intrinsic::Unreachable: {
            return builder->CreateUnreachable();
        }

        case Intrinsic::Expect: {
            if (call.arguments.size() < 2) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "expect requires 2 arguments");
            }

            auto val = generate_expression(*call.arguments[0]);
            auto expected = generate_expression(*call.arguments[1]);
            expected = cast_value(expected, val->getType());
            const auto expectFunc = llvm::Intrinsic::getDeclaration(
                module.get(), llvm::Intrinsic::expect, {val->getType()});
            return builder->CreateCall(expectFunc, {val, expected}, "expect");
        }

        case Intrinsic::Likely: {
            if (call.arguments.empty()) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "likely requires 1 argument");
            }

            auto cond = generate_expression(*call.arguments[0]);
            const auto expectFunc = llvm::Intrinsic::getDeclaration(
                module.get(), llvm::Intrinsic::expect, {cond->getType()});
            return builder->CreateCall(expectFunc, {cond, builder->getTrue()}, "likely");
        }

        case Intrinsic::Unlikely: {
            if (call.arguments.empty()) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "unlikely requires 1 argument");
            }

            auto cond = generate_expression(*call.arguments[0]);
            const auto expectFunc = llvm::Intrinsic::getDeclaration(
                module.get(), llvm::Intrinsic::expect, {cond->getType()});
            return builder->CreateCall(expectFunc, {cond, builder->getFalse()}, "unlikely");
        }
    }

    return nullptr;
}

llvm::Value *Generator::generate_function_call(const FunctionCall &expr) {
    if (is_intrinsic(expr.name.token_name)) {
        return generate_intrinsic_call(expr);
    }

    if (expr.isMethodCall()) {
        return generate_method_call_internal(expr);
    }

    if (const size_t colonPos = expr.name.token_name.find("::"); colonPos != std::string::npos) {
        const auto enumName = expr.name.token_name.substr(0, colonPos);
        const auto variantName = expr.name.token_name.substr(colonPos + 2);

        const EnumDef *enumDef = currentScope->lookup_enum(enumName);
        if (expr.hasTypeArguments()) {
            if (enumDef && enumDef->isGeneric) {
                monomorphize_enum(enumName, expr.typeArguments);
                enumDef = currentScope->lookup_monomorphized_enum(enumName, expr.typeArguments);
            }
        }

        if (enumDef) {
            if (const EnumVariantDef *variant = enumDef->getVariant(variantName)) {
                return generate_enum_construction(*enumDef, *variant, expr.arguments);
            }
        }
    }

    const auto it = functions.find(expr.name.token_name);
    if (it == functions.end()) {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION, "função não encontrada: " + expr.name.token_name);
    }

    llvm::Function *func = it->second;
    const llvm::FunctionType *funcType = func->getFunctionType();

    std::vector<llvm::Value *> args;
    size_t argIdx = 0;
    for (const auto &arg: expr.arguments) {
        llvm::Value *argVal = generate_expression(*arg);

        if (argIdx < funcType->getNumParams()) {
            argVal = cast_value(argVal, funcType->getParamType(argIdx));
        } else if (funcType->isVarArg()) {
            if (argVal->getType()->isIntegerTy() &&
                argVal->getType()->getIntegerBitWidth() < 32) {
                argVal = builder->CreateSExt(argVal, builder->getInt32Ty(), "vararg_promote");
            }
        }
        args.push_back(argVal);
        argIdx++;
    }

    return builder->CreateCall(func, args);
}

llvm::Value *Generator::generate_method_call_internal(const FunctionCall &call) {
    std::string structName;
    std::string llvmStructName;

    if (const auto *ident = dynamic_cast<const Identifier *>(call.receiver.get())) {
        structName = currentScope->lookup_variable_struct_type(ident->identifier.token_name);
        if (const auto *alloca = currentScope->lookup_variable(ident->identifier.token_name)) {
            if (const auto *structType = llvm::dyn_cast<llvm::StructType>(alloca->getAllocatedType())) {
                llvmStructName = structType->getName().str();
            }
        }
    }

    llvm::Value *objectValue = generate_expression(*call.receiver);
    if (structName.empty()) {
        throw CompileError(DiagnosticCode::TYPE_MISMATCH,
                           "cannot call method on non-struct type");
    }

    const auto methodStructName = llvmStructName.empty() ? structName : llvmStructName;
    const auto mangledName = methodStructName + "__" + call.name.token_name;

    if (const auto it = functions.find(mangledName); it == functions.end()) {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                           "method not found: " + structName + "." + call.name.token_name);
    }

    llvm::Function *func = functions[mangledName];
    std::vector<llvm::Value *> args;

    if (objectValue->getType()->isPointerTy()) {
        args.push_back(objectValue);
    } else {
        const auto alloca = builder->CreateAlloca(objectValue->getType(), nullptr, "tmp");
        builder->CreateStore(objectValue, alloca);
        args.push_back(alloca);
    }

    for (const auto &arg: call.arguments) {
        args.push_back(generate_expression(*arg));
    }

    return builder->CreateCall(func, args);
}