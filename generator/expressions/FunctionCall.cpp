//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"
#include "llvm/IR/Intrinsics.h"
#include <unordered_set>

bool Generator::is_intrinsic(const std::string &name) {
    static const std::unordered_set<std::string> intrinsics = {
        "sizeof",
        "alignof",
        "bitcast",
        "trap",
        "unreachable",
        "expect",
        "likely",
        "unlikely"
    };
    return intrinsics.contains(name);
}

llvm::Value *Generator::generate_intrinsic_call(const FunctionCall &call) {
    const std::string &name = call.name;

    if (name == "sizeof") {
        if (call.arguments.empty()) {
            throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "sizeof requires 1 argument");
        }
        llvm::Value *arg = generate_expression(*call.arguments[0]);
        llvm::Type *type = arg->getType();
        // For pointers, return pointer size (opaque pointers don't have element type info)
        const llvm::DataLayout &dataLayout = module->getDataLayout();
        uint64_t size = dataLayout.getTypeAllocSize(type);
        return builder->getInt64(size);
    }

    if (name == "alignof") {
        if (call.arguments.empty()) {
            throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "alignof requires 1 argument");
        }
        llvm::Value *arg = generate_expression(*call.arguments[0]);
        llvm::Type *type = arg->getType();
        // For pointers, return pointer alignment (opaque pointers don't have element type info)
        const llvm::DataLayout &dataLayout = module->getDataLayout();
        uint64_t align = dataLayout.getABITypeAlign(type).value();
        return builder->getInt64(align);
    }

    if (name == "bitcast") {
        if (call.arguments.size() < 2) {
            throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT,
                               "bitcast requires 2 arguments (value, target_type_value)");
        }
        llvm::Value *value = generate_expression(*call.arguments[0]);
        llvm::Value *targetTypeValue = generate_expression(*call.arguments[1]);
        llvm::Type *targetType = targetTypeValue->getType();
        return builder->CreateBitCast(value, targetType, "bitcast");
    }

    if (name == "trap") {
        llvm::Function *trapFunc = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::trap);
        builder->CreateCall(trapFunc);
        return builder->CreateUnreachable();
    }

    if (name == "unreachable") {
        return builder->CreateUnreachable();
    }

    if (name == "expect") {
        if (call.arguments.size() < 2) {
            throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "expect requires 2 arguments");
        }
        llvm::Value *val = generate_expression(*call.arguments[0]);
        llvm::Value *expected = generate_expression(*call.arguments[1]);
        // Cast expected to match val's type
        expected = cast_value(expected, val->getType());
        llvm::Function *expectFunc = llvm::Intrinsic::getDeclaration(
            module.get(), llvm::Intrinsic::expect, {val->getType()});
        return builder->CreateCall(expectFunc, {val, expected}, "expect");
    }

    if (name == "likely") {
        if (call.arguments.empty()) {
            throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "likely requires 1 argument");
        }
        llvm::Value *cond = generate_expression(*call.arguments[0]);
        llvm::Function *expectFunc = llvm::Intrinsic::getDeclaration(
            module.get(), llvm::Intrinsic::expect, {cond->getType()});
        return builder->CreateCall(expectFunc, {cond, builder->getTrue()}, "likely");
    }

    if (name == "unlikely") {
        if (call.arguments.empty()) {
            throw CompileError(DiagnosticCode::INVALID_ARGUMENT_COUNT, "unlikely requires 1 argument");
        }
        llvm::Value *cond = generate_expression(*call.arguments[0]);
        llvm::Function *expectFunc = llvm::Intrinsic::getDeclaration(
            module.get(), llvm::Intrinsic::expect, {cond->getType()});
        return builder->CreateCall(expectFunc, {cond, builder->getFalse()}, "unlikely");
    }

    throw CompileError(DiagnosticCode::UNDEFINED_FUNCTION, "unknown intrinsic: " + name);
}

llvm::Value *Generator::generate_function_call(const FunctionCall &expr) {
    // Check if it's an intrinsic call
    if (is_intrinsic(expr.name)) {
        return generate_intrinsic_call(expr);
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
