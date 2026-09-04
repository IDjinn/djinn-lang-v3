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

The default lowering is zero-cost error codes: `throw` records the error in the
thread-local error state and returns; call sites check it with one branch. No
tables, no unwinder — same happy-path cost as manual error codes, with the
raise site's stack trace captured natively (symbolized from debug info in
debug builds).

You can also match the outcome like an enum:

```djinn
i32 result = switch division(1, 0) {
    Result v -> v,
    DivisionByZeroException e -> -1,
    Error e -> 0,
};
```

### Native exceptions (opt-in: `--exceptions`)

With `--exceptions` (or `compiler.exceptions: true` in `djinn.proj`) the whole
build switches to LLVM zero-cost unwinding tables and enables the classic
block form, on top of the expression forms above:

```djinn
import std::types;

struct DivisionByZeroException : ArgumentException;

i32 division(i32 value, i32 divisor) throws(DivisionByZeroException) {
    if (divisor == 0) {
        throw DivisionByZeroException("Division {value}/0 is not allowed");
    }
    return value / divisor;
}

i32 main() {
    try {
        i32 result = division(1, 0);
        Console.write(result);
    } catch (DivisionByZeroException e) {
        Console.write("failed: {e}");
        return 1;
    } catch (Error e) {
        return 2;
    } finally {
        Console.write("done");
    }
    return 0;
}
```

- `catch (T e)` matches by error type (derived types match too), `catch (Error e)`
  or `catch (_)` catch everything; the binding holds `{ tag, message, type_name }`.
- `finally` runs on every non-unwinding path and once more before a re-throw.
- Unmatched errors re-throw; an error escaping `main` renders the uncaught
  report (type, message, origin, stack trace) and aborts.
- C++ code linked into the binary can `catch (const djinn::error&)` (it is a
  `std::exception`; see `runtime/djinn_error.h`), and foreign C++ exceptions
  caught by djinn handlers surface as `ForeignError`.
- Errors crossing `await` travel in the coroutine's promise error slot and
  re-throw at the resume point.
- Native exceptions require an AOT build — the JIT falls back to it with a
  message.
