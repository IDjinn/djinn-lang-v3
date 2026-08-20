# Djinn

[![Build](https://github.com/IDjinn/djinn-lang-v3/actions/workflows/build.yml/badge.svg)](https://github.com/IDjinn/djinn-lang-v3/actions/workflows/build.yml)

A compiled, statically-typed systems language that targets LLVM IR with a C runtime. Inspired by C, C++ and C#, Djinn
focuses on developer experience, simplicity, and high performance — with async-by-default codegen.

```djinn
void main() {
    printf("hello world!");
}
```

## Why Djinn

- **Native performance** — direct LLVM IR codegen, no VM, no GC.
- **Async by default** — LLVM coroutines, event loop, `spawn` / `await` built into the language.
- **Modern type system** — generics with `where` constraints, tagged-union enums (ADTs), interfaces, pattern matching
  via `is`, ownership/move semantics.
- **Compile-time programming** — `constexpr` / `consteval` via a stack-based bytecode VM, hygienic macros, compile-time
  `if`, top-level compile-time blocks.
- **Libraries that just work** — `.djlib` binary format bundling metadata + LLVM bitcode, separate compilation, generic
  dedup across modules, full RTTI/reflection opt-in.
- **Great DX** — LSP server, rich diagnostics with suggestions and error codes, attribute system,
  `--inspect` for library introspection.

## A Quick Tour

Generics, enums, and pattern matching:

```djinn
enum optional<T> {
    Some(T),
    None()
}

result<i32, MathError> div(i32 a, i32 b) {
    if (b == 0) { return result::Err(MathError::DivByZero()); }
    return result::Ok(a / b);
}
```

Structs with methods, properties, and ownership:

```djinn
struct User {
    string name;
    i32 age;

    string display { get => name; }

    static User new(string name, i32 age) => User { .name = name, .age = age };
}
```

Async and parallelism:

```djinn
async i32 fetch_count() {
    i32 n = await load_from_disk();
    return n;
}

void main() {
    i32 result = await fetch_count();
}
```

Macros (hygienic, multi-rule, pattern-matched):

```djinn
macro square {
    (local expression v) => { v * v }
}

i32 x = square(foo());   // foo() evaluated exactly once
```

Compile-time evaluation:

```djinn
consteval i32 factorial(i32 n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

constexpr i32 BUFFER = 64 * 1024;
```

## Features

- **Types**: i8–i64, u8–u64, f32/f64, `nint`/`nfloat`/`ndouble` (native-width), integer overflow modes (`w`rapped / `t`
  rapped / `c`hecked / `s`aturating), bool, str, string, arrays, slices, pointers, `object`,
  `auto` inference
- **Control flow**: if/else, while, do-while, for (classic + range), switch/case, break/continue
- **Range-for**: `for (i32 i in 0..10)`, `..=` inclusive, bracket bounds `[0..10)`
- **Structs**: brace init (positional + designated), methods (block / `=>`), static methods, properties, transparent
  types
- **Generics**: structs, enums, functions, constructors; monomorphization; `where` constraints
- **Enums**: tagged unions / ADTs with variant payloads, generic
- **Interfaces**: definition, multi-implementation, generic interfaces
- **Pattern matching**: `obj is i32`, `obj is i32 value` (binds and casts)
- **Ownership**: move semantics, use-after-move detection, reinitialize after move
- **Mutability**: immutable by default, `mut` to opt in
- **Async/Await**: LLVM coroutines, `yield`, `spawn`, event loop runtime
- **FFI**: `extern "C"` functions and blocks, variadics
- **Macros**: `expression` / `identifier` / `literal` / `type` / `block` fragments, `local` modifier for
  single-evaluation, literal-token pattern matching, multi-rule dispatch
- **Attributes**: parameterized (`[align(16)]`), named args, `[llvm("...")]` escape hatch, built-in mappings
  (force-inline, no-inline, hot, cold, noreturn, …)
- **Static variables**: namespace-accessible globals, `mut` opt-in
- **Number literals**: `420_000`, `800'000'000`, `1e9`, `2.5e-3`, overflow-mode suffixes (`123w`, `123t`, `123c`,
  `123s`)
- **Intrinsics**: `sizeof`, `alignof`, `typeof`, `likely`, `unlikely`, `expect`
- **Libraries**: `--lib` mode, `.djlib` binary format, project file (`djinn.proj`), cross-module generic dedup via
  `LinkOnceODR` + COMDAT
- **RTTI / Reflection**: `TypeInfo` (16 B) cross-module, opt-in `TypeInfoExt` (fields, methods, attributes)
  via `[Reflect]` or `reflection-mode: all` — zero-cost when unused
- **Tooling**: LSP server, `--inspect` library introspection, diagnostics with suggestions

## Standard Library

Lives under `std/`:

- `types/` — core types (`size`, `bool`, `str`, `string`, `arr<T>`, `optional<T>`, `result<T, E>`,
  `TypeInfo`, `object`) and constraint interfaces (`Hashable`, `Comparable`, `Equatable`, …)
- `collections/` — `array<T>`, `map<Key, Value>`, `range`
- `sys/` — `console`, `io`, `debug`, `libc` (FFI bindings)
- `builtin/` — coroutine utilities

## Architecture

```
lexer/        Lexer, Token, TokenType
parser/       AST nodes (Declaration, Statement, Type, Generic, Scope)
binder/       Symbol table, scope, ownership tracking, type validation
generator/    LLVM IR generation, intrinsics, name mangling
evaluator/    Compile-time bytecode VM (ConstVM, ConstValue)
visitor/      Statement/Declaration visitors
diagnostics/  Error reporting with suggestions and error codes
lsp/          Language Server Protocol implementation
lib/          .djlib library format (writer/reader)
std/          Standard library (.djinn sources)
runtime/      C runtime (event loop, thread pool, coroutine wrappers)
tests/        GoogleTest unit tests (~200+ cases)
docs/         Fumadocs documentation site
```

## Status

- **Phase 1 — Foundation**: complete (generics, FFI, malloc/free, sizeof)
- **Phase 2 — Structures**: complete (enums, methods, constructors, `array<T>`, slices, string)
- **Phase 3 — Standard library**: complete (I/O, collections, HashMap)
- **Phase 4 — Advanced**: ~95% (interfaces, async/await, constexpr/consteval, macros, compile-time `if`,
  `is` expression, attribute system). Remaining: closures, `[platform(...)]` conditional compilation.
- **Phase 5 — Libraries**: complete (separate compilation, `.djlib`, RTTI/reflection, `--inspect`)

See `todo.md` for the full roadmap.

## Build

CMake-based build with GoogleTest for unit tests. New `.cpp` files in existing directories are picked up automatically
via `GLOB_RECURSE`.

## License

See `LICENSE`.
