# djinnc

Self-hosted Djinn compiler, written in Djinn and built by the C++ compiler (stage0).

## Pipeline

`source -> Lexer -> Parser (AST) -> CodeGen (LLVM-C) -> .obj (LLVMTargetMachineEmitToFile) -> clang/lld + runtime`

The `.ll` is only written with `--emit-ir`, `--no-link` or `--keep-ir`. `--runtime-lib <lib>` links a precompiled runtime
(CMake target `djinn_runtime`) instead of recompiling `djinn_runtime.c` and `logger.c`. `--lld` calls `lld-link` directly (Windows only, requires `--runtime-lib`; library search paths come from the `LIB`
environment variable or `--link-arg /libpath:...`; `DJINN_LLD_LINK` overrides the executable), skipping the clang driver.
`-O2` runs `default<O2>` through `LLVMRunPasses` and raises the backend codegen level.
The object writer initializes the x86 target only.

## Build

LLVM-C must be reachable by the linker. Adjust `native-libs` in `djinn.proj` to the local LLVM install and
make sure `LLVM-C.dll` is on `PATH` when running.

Run stage0 on `djinnc/`; the resulting executable is stage1.

## Usage

```
djinnc <input.djinn> [-o out] [--runtime-dir dir] [--no-link] [--emit-tokens] [--emit-ast] [--emit-ir]
```

`DJINN_CLANG` overrides the clang executable. Tests: `../tests/djinnc/run.sh <djinnc> <runtime dir>`.

## Supported

Functions, extern functions and blocks, namespaces, imports, local variables, integer/float/bool/string/null
literals, arithmetic, comparison, short-circuit logic, casts, pointers (`&`, `*`, indexing), calls including
variadics, `if`, `while`, `do-while`, `for`, `break`, `continue`, `return`.

## Not yet supported

Structs and methods in codegen (parsed only), generics, enums and `switch`, interfaces/impl, async,
exceptions, string interpolation, arrays, `NodeKind`/`TokenKind` as real enums (needs `==` on enums in stage0).
