//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value *Generator::generate_integer_literal(const IntegerLiteral &expr) const {
    const llvm::APInt apVal(128, expr.value, 10);
    const unsigned activeBits = apVal.getActiveBits();

    unsigned bits;
    if (activeBits <= 8) {
        bits = 8;
    } else if (activeBits <= 16) {
        bits = 16;
    } else if (activeBits <= 32) {
        bits = 32;
    } else if (activeBits <= 64) {
        bits = 64;
    } else {
        bits = 128;
    }

    return llvm::ConstantInt::get(builder->getIntNTy(bits), apVal.trunc(bits));
}