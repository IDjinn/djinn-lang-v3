### Error Handling

Errors | Exceptions is a stack based allocated value, completely traced by compiler and checked by default. Stacktrace
by default when debug build mode

```djinn
import std::types;

struct DivisionByZeroException : ArgumentException;

constexpr i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
    if (unlikely(divisor == 0)) {
        throw DivisionByZeroException("Division {value}/0 is not allowed");
    }
    return value / divisor;
}

void main() throws {
    i32 result = try division(1, 0) ?: -1;
}
```

The compiler will figure out exception based on signature, unlike c++, type return is classical `T`, but `throws(T)`
give type of exception throwable in that method. If throws is present, `try` instruction is required and enforced by
compiler, in case of failures. You can also re-throw it with method `throws` signature.

That translates to c++ pseudocode

```djinn
void main() throws {
    i32 result;
    try {
        result = division(1, 0);
    } catch {
        result = -1;
    }
}
```

Exceptions being a tagged enum, similar to excepted c++ code

```djinn
constexpr expected<i32, DivisionByZeroException> division(i32 value, i32 divisor) {
    if (unlikely(divisor == 0)) {
        retrun DivisionByZeroException("Division {value}/0 is not allowed");
    }
    return value / divisor;
}
```