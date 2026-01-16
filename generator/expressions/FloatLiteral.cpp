//
// Created by Claude on 06/01/2026.
//

#include "../Generator.h"

llvm::Value *Generator::generate_float_literal(const FloatLiteral &expr) const {
    const double value = std::stod(expr.value);
    return llvm::ConstantFP::get(builder->getDoubleTy(), value);
}