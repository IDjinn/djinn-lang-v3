# djinnc

Self-hosted Djinn compiler, written in Djinn and built by the C++ compiler (stage0).

## Pipeline

`source -> Lexer -> Parser (AST) -> CodeGen (LLVM-C) -> .obj (LLVMTargetMachineEmitToFile) -> clang/lld + runtime`

The `.ll` is only written with `--emit-ir`, `--no-link` or `--keep-ir`. `--runtime-lib <lib>` links a precompiled runtime
(CMake target `djinn_runtime`) instead of recompiling the runtime sources. `--lld` calls `lld-link` directly (Windows only, requires `--runtime-lib`; library search paths come from the `LIB`
environment variable or `--link-arg /libpath:...`; `DJINN_LLD_LINK` overrides the executable), skipping the clang driver.
On POSIX `--lld` calls `ld.lld` directly with the glibc startup files (`DJINN_LD_LLD`, `DJINN_CRT_DIR` and `DJINN_DYNAMIC_LINKER` override the
executable, the crt directory and the dynamic loader). The test scripts take the runtime library as an optional third argument and add `--lld`
when `DJINN_USE_LLD` is set.
`-O1`/`-O2`/`-O3`/`-Os`/`-Oz` run `default<ON>` through `LLVMRunPasses` and raise the backend codegen level.
`--std` adds the Djinn standard library (`std/`, optional; see [STD_ROADMAP.md](STD_ROADMAP.md)): `import` makes a namespace visible, `struct size : u32;` is a type alias, and the std's `str`/`string`/`arr`/`object`/`TypeInfo` replace the core's.
`--run` executes the program in process with ORC LLJIT (the runtime library is loaded as a static archive, libc comes from the djinnc process, `__djinn_call_entry` in the runtime calls `main`).
`--target <triple>` selects the target (default: the host). It sets the width of `usize`/`isize`; the object writer only has the x86 backend linked in, so other targets stop at `--emit-ir`, and a non-host target is never linked.

## Source layout

One type per file, grouped by folder under `src/` (all files share `namespace djinnc;`): `ast/node`, `ast/attributes`, `ast/macros`, `ast/types`, `lexer`, `parser`, `semantic/{analyzer,symbols,types,checker,const_eval}`, `codegen/{function,target}`, `llvm`, `driver` (including `jit.djinn`) and `support/{memory,text,collections,source,system}`.

`Parser`, `CodeGenerator`, `TypeChecker` and `SemanticAnalyzer` are split by topic with `impl` blocks: the struct and its fields stay in `parser/parser.djinn` and `codegen/code_generator.djinn`, and each group of methods lives in its own file (`parser_types`, `parser_statements`, ..., `code_generator_types`, `type_checker_calls`, `semantic_analyzer_impls`, ...; the stage0 needs the struct file to sort before its impl files, so impl files are named after it).

See [USAGE.md](USAGE.md) for the full guide (building, options, testing and the bootstrap).

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
