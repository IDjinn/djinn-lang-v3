//
// Created by Luke on 06/12/2025.
//

#include "../Generator.h"

llvm::Value* Generator::generate_integer_literal(const IntegerLiteral& expr) const
{
    // Strip separators (_ and ') from value
    std::string cleaned;
    cleaned.reserve(expr.value.size());
    for (char c : expr.value)
    {
        if (c != '_' && c != '\'') cleaned += c;
    }

    // Determine radix from prefix
    unsigned radix = 10;
    std::string digits = cleaned;
    if (cleaned.size() > 2 && cleaned[0] == '0')
    {
        if (cleaned[1] == 'x' || cleaned[1] == 'X')
        {
            radix = 16;
            digits = cleaned.substr(2);
        }
        else if (cleaned[1] == 'b' || cleaned[1] == 'B')
        {
            radix = 2;
            digits = cleaned.substr(2);
        }
    }

    const llvm::APInt apVal(128, digits, radix);
    const unsigned activeBits = apVal.getActiveBits();

    unsigned bits;
    if (activeBits <= 32)
    {
        bits = 32;
    }
    else if (activeBits <= 64)
    {
        bits = 64;
    }
    else
    {
        bits = 128;
    }

    return llvm::ConstantInt::get(builder->getIntNTy(bits), apVal.trunc(bits));
}