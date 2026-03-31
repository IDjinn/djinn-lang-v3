//
// Compile-time value representation for constexpr/consteval evaluation
//

#ifndef DJINN_CONST_VALUE_H
#define DJINN_CONST_VALUE_H

#include <cstdint>
#include <string>

// 128-bit integer representation (high:low pair)
struct Int128
{
    int64_t high = 0;
    uint64_t low = 0;

    Int128() = default;

    Int128(int64_t h, uint64_t l) : high(h), low(l)
    {
    }

    // Construct from a 64-bit value (sign-extends)
    explicit Int128(int64_t val)
        : high(val < 0 ? -1 : 0), low(static_cast<uint64_t>(val))
    {
    }

    // Construct from unsigned 64-bit value
    static Int128 fromUnsigned(uint64_t val) { return {0, val}; }

    [[nodiscard]] bool isZero() const { return high == 0 && low == 0; }

    [[nodiscard]] int64_t toInt64() const { return static_cast<int64_t>(low); }

    // Basic arithmetic
    Int128 operator+(const Int128& r) const
    {
        uint64_t lo = low + r.low;
        int64_t hi = high + r.high + (lo < low ? 1 : 0); // carry
        return {hi, lo};
    }

    Int128 operator-(const Int128& r) const
    {
        uint64_t lo = low - r.low;
        int64_t hi = high - r.high - (low < r.low ? 1 : 0); // borrow
        return {hi, lo};
    }

    Int128 operator-() const
    {
        // Two's complement negate: ~x + 1
        uint64_t lo = ~low + 1;
        int64_t hi = ~high + (lo == 0 ? 1 : 0);
        return {hi, lo};
    }

    bool operator==(const Int128& r) const { return high == r.high && low == r.low; }
    bool operator!=(const Int128& r) const { return !(*this == r); }

    // Signed comparison
    bool operator<(const Int128& r) const
    {
        if (high != r.high) return high < r.high;
        return low < r.low;
    }

    bool operator<=(const Int128& r) const { return !(r < *this); }
    bool operator>(const Int128& r) const { return r < *this; }
    bool operator>=(const Int128& r) const { return !(*this < r); }
};

struct ConstValue
{
    enum Kind : uint8_t
    {
        Integer,
        Integer128,
        Float,
        Bool,
        Void,
        Error
    };

    Kind kind = Error;
    int64_t intVal = 0;
    Int128 int128Val;
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

    static ConstValue makeInt128(Int128 val, bool sign = true)
    {
        ConstValue v;
        v.kind = Integer128;
        v.int128Val = val;
        v.bitWidth = 128;
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
    [[nodiscard]] bool isNumeric() const { return kind == Integer || kind == Integer128 || kind == Float; }
    [[nodiscard]] bool isInteger() const { return kind == Integer || kind == Integer128; }

    [[nodiscard]] int64_t toInt() const
    {
        switch (kind)
        {
        case Integer: return intVal;
        case Integer128: return int128Val.toInt64();
        case Float: return static_cast<int64_t>(floatVal);
        case Bool: return boolVal ? 1 : 0;
        default: return 0;
        }
    }

    [[nodiscard]] Int128 toInt128() const
    {
        switch (kind)
        {
        case Integer: return Int128(intVal);
        case Integer128: return int128Val;
        case Float: return Int128(static_cast<int64_t>(floatVal));
        case Bool: return Int128(boolVal ? 1 : 0);
        default: return {};
        }
    }

    [[nodiscard]] double toFloat() const
    {
        switch (kind)
        {
        case Integer: return static_cast<double>(intVal);
        case Integer128: return static_cast<double>(int128Val.high) * 18446744073709551616.0 + static_cast<double>(
                int128Val.low);
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
        case Integer128: return !int128Val.isZero();
        case Float: return floatVal != 0.0;
        case Bool: return boolVal;
        default: return false;
        }
    }
};

#endif // DJINN_CONST_VALUE_H