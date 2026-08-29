
# Djinn Lang

## Philosophy

Djinn is a compiled language that uses LLVM IR as its backend with a C runtime. Strongly inspired by C, C++ and C#,
the main focus is a language with great Developer Experience, simplicity, and high performance.

The compiler generates async code by default.

## Code Guidelines

- Generated code should be as simple as possible to be easily extensible later.
- Do NOT write line-by-line comments, unless explaining complex expressions (e.g. math) or something that only
  meaningful variable names could explain. Do NOT document code sections with comments.
- Do NOT run tests or builds — the user will run them manually.
- Error reporting uses macros that take a `DiagnosticCode`, message, and source location, then call
  `_diagnostics.emitAndPrint()` which prints the error with file/line info for the user:
  - **Parser**: `PARSER_ERROR(code, msg, location)` / `PARSER_WARNING(code, msg, location)`
  - **Binder**: `BINDER_ERROR(code, msg, token, location)` / `BINDER_WARNING(code, msg, location)`
  - **Generator**: `GENERATOR_ERROR(code, msg, location)` / `GENERATOR_WARN(code, msg, location)`
  - All error macros throw `CompileError` after emitting. Warnings do not throw (except PARSER_WARNING).


## Standard Library

Files in `std/` directory

- `types/types.djinn` — core types (size, bool, str, string, arr\<T\>, optional\<T\>, result\<T,E\>, TypeInfo, object),
  primitive impl blocks (Hashable, constants), string operators
- `types/constraints.djinn` — interfaces (Hashable, Comparable, Equatable, Addition, Serializable)
- `collections/array.djinn` — generic `array<T>` with push, get, set, reserve, destroy
- `collections/map.djinn` — generic `map<Key, Value>` hash map
- `collections/range.djinn` — `range` struct (start, end, step, bounds) with length(), contains(), is_empty()
- `sys/console.djinn`, `sys/io.djinn`, `sys/debug.djinn` — I/O, assertions
- `sys/libc.djinn` — FFI bindings (malloc, free, printf, memcpy, etc.)
- `builtin/coro.djinn` — coroutine utilities