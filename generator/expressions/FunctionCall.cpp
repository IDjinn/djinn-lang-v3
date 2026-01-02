//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_function_call(const FunctionCall &expr) {
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
