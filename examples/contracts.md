```djinn
import std::types;

constexpr i32 abs(i32 value)
    require(value != i32::MIN_VALUE)               // allow as block as well { return ... }
    ensure(return == (value < 0 ? -value : value))
{
    return value < 0 ? -value : value;
}
```

```djinn

struct DivisionByZeroException : ArgumentException;

constexpr i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
    if (unlikely(divisor == 0)) {
        throw DivisionByZeroException("Division {value}/0 is not allowed");
    }
    return value / divisor;
}
```

```djinn
void main() throws {
    i32 result = try division(1, 0) ?: -1;
    i32 result = division(1, 0); // compiler error
    i32 result = abs(1, -1);
}
```

