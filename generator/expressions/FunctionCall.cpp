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
    const auto intrinsic = get_intrinsic(call.name);
    if (!intrinsic) {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION, "unknown intrinsic: " + call.name);
    }

    switch (*intrinsic) {
        case Intrinsic::Sizeof: {
            if (call.arguments.empty()) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "sizeof requires 1 argument");
            }
            llvm::Value *arg = generate_expression(*call.arguments[0]);
            llvm::Type *type = arg->getType();
            const llvm::DataLayout &dataLayout = module->getDataLayout();
            uint64_t size = dataLayout.getTypeAllocSize(type);
            return builder->getInt64(size);
        }

        case Intrinsic::Alignof: {
            if (call.arguments.empty()) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "alignof requires 1 argument");
            }
            llvm::Value *arg = generate_expression(*call.arguments[0]);
            llvm::Type *type = arg->getType();
            const llvm::DataLayout &dataLayout = module->getDataLayout();
            uint64_t align = dataLayout.getABITypeAlign(type).value();
            return builder->getInt64(align);
        }

        case Intrinsic::Bitcast: {
            if (call.arguments.size() < 2) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT,
                                   "bitcast requires 2 arguments (value, target_type_value)");
            }
            llvm::Value *value = generate_expression(*call.arguments[0]);
            llvm::Value *targetTypeValue = generate_expression(*call.arguments[1]);
            llvm::Type *targetType = targetTypeValue->getType();
            return builder->CreateBitCast(value, targetType, "bitcast");
        }

        case Intrinsic::Trap: {
            llvm::Function *trapFunc = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::trap);
            builder->CreateCall(trapFunc);
            return builder->CreateUnreachable();
        }

        case Intrinsic::DebugTrap: {
            llvm::Function *trapFunc = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::debugtrap);
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
            llvm::Value *val = generate_expression(*call.arguments[0]);
            llvm::Value *expected = generate_expression(*call.arguments[1]);
            expected = cast_value(expected, val->getType());
            llvm::Function *expectFunc = llvm::Intrinsic::getDeclaration(
                module.get(), llvm::Intrinsic::expect, {val->getType()});
            return builder->CreateCall(expectFunc, {val, expected}, "expect");
        }

        case Intrinsic::Likely: {
            if (call.arguments.empty()) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "likely requires 1 argument");
            }
            llvm::Value *cond = generate_expression(*call.arguments[0]);
            llvm::Function *expectFunc = llvm::Intrinsic::getDeclaration(
                module.get(), llvm::Intrinsic::expect, {cond->getType()});
            return builder->CreateCall(expectFunc, {cond, builder->getTrue()}, "likely");
        }

        case Intrinsic::Unlikely: {
            if (call.arguments.empty()) {
                throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "unlikely requires 1 argument");
            }
            llvm::Value *cond = generate_expression(*call.arguments[0]);
            llvm::Function *expectFunc = llvm::Intrinsic::getDeclaration(
                module.get(), llvm::Intrinsic::expect, {cond->getType()});
            return builder->CreateCall(expectFunc, {cond, builder->getFalse()}, "unlikely");
        }
    }

    return nullptr;
}

llvm::Value *Generator::generate_function_call(const FunctionCall &expr) {
    if (is_intrinsic(expr.name)) {
        return generate_intrinsic_call(expr);
    }

    // Handle method calls (receiver.method())
    if (expr.isMethodCall()) {
        return generate_method_call_internal(expr);
    }

    const auto it = functions.find(expr.name);
    if (it == functions.end()) {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION, "função não encontrada: " + expr.name);
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

    // First check if receiver is an Identifier (could be struct name for static method)
    if (const auto *ident = dynamic_cast<const Identifier *>(call.receiver.get())) {
        // Check if this is a struct type (static method call: StructName.method())
        if (currentScope->lookup_struct(ident->name)) {
            structName = ident->name;
            // Static method call - look up method directly
            const std::string mangledName = structName + "__" + call.name;

            if (const auto it = functions.find(mangledName); it != functions.end()) {
                llvm::Function *func = it->second;

                std::vector<llvm::Value *> args;
                for (const auto &arg: call.arguments) {
                    args.push_back(generate_expression(*arg));
                }

                return builder->CreateCall(func, args);
            }
            throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                               "static method not found: " + structName + "." + call.name);
        }

        // Otherwise it's a variable - get its struct type
        structName = currentScope->lookup_variable_struct_type(ident->name);
    }

    // For instance methods, generate the object expression
    llvm::Value *objectValue = generate_expression(*call.receiver);

    if (structName.empty()) {
        throw CompileError(DiagnosticCode::TYPE_MISMATCH,
                           "cannot call method on non-struct type");
    }

    // Instance method call - look up method
    const std::string mangledName = structName + "__" + call.name;

    if (const auto it = functions.find(mangledName); it == functions.end()) {
        throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION,
                           "method not found: " + structName + "." + call.name);
    }

    llvm::Function *func = functions[mangledName];

    // Build arguments: first is 'this' pointer, then other args
    std::vector<llvm::Value *> args;

    // Get pointer to the object for 'this'
    if (objectValue->getType()->isPointerTy()) {
        args.push_back(objectValue);
    } else {
        // Need to create an alloca and get its address
        llvm::AllocaInst *alloca = builder->CreateAlloca(objectValue->getType(), nullptr, "tmp");
        builder->CreateStore(objectValue, alloca);
        args.push_back(alloca);
    }

    // Add other arguments
    for (const auto &arg: call.arguments) {
        args.push_back(generate_expression(*arg));
    }

    return builder->CreateCall(func, args);
}