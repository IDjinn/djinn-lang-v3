// LLVM lowers 128-bit division and float conversions to these compiler-rt calls, which neither the JIT nor the link provide.
// The compiler checks the divisor and the float range before each call, so none of them handles invalid input.
#ifdef __SIZEOF_INT128__
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef unsigned __int128 djinn_u128;
typedef __int128 djinn_i128;

static djinn_u128 divide_u128(djinn_u128 dividend, djinn_u128 divisor, djinn_u128* remainder)
{
    djinn_u128 quotient = 0;
    djinn_u128 rest = 0;
    for (int bit = 127; bit >= 0; bit--)
    {
        int carried = (int)(rest >> 127);
        rest = (rest << 1) | ((dividend >> bit) & 1);
        if (carried || rest >= divisor)
        {
            rest -= divisor;
            quotient |= (djinn_u128)1 << bit;
        }
    }
    *remainder = rest;
    return quotient;
}

static djinn_u128 magnitude(djinn_i128 value)
{
    return value < 0 ? (djinn_u128)0 - (djinn_u128)value : (djinn_u128)value;
}

djinn_u128 __udivti3(djinn_u128 left, djinn_u128 right)
{
    djinn_u128 rest;
    return divide_u128(left, right, &rest);
}

djinn_u128 __umodti3(djinn_u128 left, djinn_u128 right)
{
    djinn_u128 rest;
    divide_u128(left, right, &rest);
    return rest;
}

djinn_i128 __divti3(djinn_i128 left, djinn_i128 right)
{
    djinn_u128 rest;
    djinn_u128 quotient = divide_u128(magnitude(left), magnitude(right), &rest);
    return (left < 0) != (right < 0) ? (djinn_i128)((djinn_u128)0 - quotient) : (djinn_i128)quotient;
}

djinn_i128 __modti3(djinn_i128 left, djinn_i128 right)
{
    djinn_u128 rest;
    divide_u128(magnitude(left), magnitude(right), &rest);
    return left < 0 ? (djinn_i128)((djinn_u128)0 - rest) : (djinn_i128)rest;
}

static djinn_u128 truncate_positive(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    int exponent = (int)((bits >> 52) & 0x7FF) - 1023;
    uint64_t mantissa = (bits & (((uint64_t)1 << 52) - 1)) | ((uint64_t)1 << 52);
    if (exponent < 0)
    {
        return 0;
    }
    if (exponent <= 52)
    {
        return mantissa >> (52 - exponent);
    }
    return (djinn_u128)mantissa << (exponent - 52);
}

static djinn_i128 truncate_signed(double value)
{
    if (value < 0)
    {
        return (djinn_i128)((djinn_u128)0 - truncate_positive(-value));
    }
    return (djinn_i128)truncate_positive(value);
}

djinn_u128 __fixunsdfti(double value) { return truncate_positive(value); }
djinn_u128 __fixunssfti(float value) { return truncate_positive(value); }
djinn_i128 __fixdfti(double value) { return truncate_signed(value); }
djinn_i128 __fixsfti(float value) { return truncate_signed(value); }

// Keeping the top 64 bits with the dropped ones folded into the last bit leaves one rounding, done by the 64-bit conversion.
static uint64_t top_bits(djinn_u128 value, int* shift)
{
    uint64_t high = (uint64_t)(value >> 64);
    *shift = high == 0 ? 0 : 64 - __builtin_clzll(high);
    if (*shift == 0)
    {
        return (uint64_t)value;
    }
    djinn_u128 dropped = value << (128 - *shift);
    return (uint64_t)(value >> *shift) | (dropped != 0);
}

double __floatuntidf(djinn_u128 value)
{
    int shift;
    uint64_t top = top_bits(value, &shift);
    return ldexp((double)top, shift);
}

float __floatuntisf(djinn_u128 value)
{
    int shift;
    uint64_t top = top_bits(value, &shift);
    return ldexpf((float)top, shift);
}

double __floattidf(djinn_i128 value)
{
    double result = __floatuntidf(magnitude(value));
    return value < 0 ? -result : result;
}

float __floattisf(djinn_i128 value)
{
    float result = __floatuntisf(magnitude(value));
    return value < 0 ? -result : result;
}
#endif
