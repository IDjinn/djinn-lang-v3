//
// Compile-time value representation for constexpr/consteval evaluation
//

#ifndef DJINN_CONST_VALUE_H
#define DJINN_CONST_VALUE_H

#include <cstdint>
#include <string>
#include <variant>

struct ConstValue
{
    enum Kind : uint8_t
    {
        Integer,
        Float,
        Bool,
        Void,
        Error
    };

    Kind kind = Error;
    int64_t intVal = 0;
    double floatVal = 0.0;
    bool boolVal = false;
    unsigned bitWidth = 32;
    bool isSigned = true;

    static ConstValue makeInt(int64_t val, unsigned bits = 32, bool sign = true)
    {
        ConstValue v;
        v.kind = Integer;
        v.intVal = val;
        v.bitWidth = bits;
        v.isSigned = sign;
        return v;
    }

    static ConstValue makeFloat(double val, unsigned bits = 64)
    {
        ConstValue v;
        v.kind = Float;
        v.floatVal = val;
        v.bitWidth = bits;
        return v;
    }

    static ConstValue makeBool(bool val)
    {
        ConstValue v;
        v.kind = Bool;
        v.boolVal = val;
        v.bitWidth = 1;
        return v;
    }

    static ConstValue makeVoid()
    {
        ConstValue v;
        v.kind = Void;
        return v;
    }

    static ConstValue error()
    {
        return ConstValue{};
    }

    [[nodiscard]] bool isError() const { return kind == Error; }
    [[nodiscard]] bool isNumeric() const { return kind == Integer || kind == Float; }

    [[nodiscard]] int64_t toInt() const
    {
        switch (kind)
        {
        case Integer: return intVal;
        case Float: return static_cast<int64_t>(floatVal);
        case Bool: return boolVal ? 1 : 0;
        default: return 0;
        }
    }

    [[nodiscard]] double toFloat() const
    {
        switch (kind)
        {
        case Integer: return static_cast<double>(intVal);
        case Float: return floatVal;
        case Bool: return boolVal ? 1.0 : 0.0;
        default: return 0.0;
        }
    }

    [[nodiscard]] bool toBool() const
    {
        switch (kind)
        {
        case Integer: return intVal != 0;
        case Float: return floatVal != 0.0;
        case Bool: return boolVal;
        default: return false;
        }
    }
};

#endif // DJINN_CONST_VALUE_H
