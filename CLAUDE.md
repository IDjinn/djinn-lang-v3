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

## Project Status

See `todo.md` for full roadmap. Summary:

- **Phase 1 (Foundation)**: COMPLETE — generics, FFI extern "C", malloc/free, sizeof
- **Phase 2 (Structures)**: COMPLETE — enums, methods, constructors (incl. generic), array\<T\>, slices, string
- **Phase 3 (Std)**: COMPLETE — I/O, array\<T\> collection, String, HashMap
- **Phase 4 (Advanced)**: ~95% — interfaces, async/await, constexpr/consteval, macros, compile-time if,
  top-level CompileTimeBlock, `is` expression (pattern matching), attribute system (parameterized, validated,
  centralized LLVM mapping, `[llvm()]` escape hatch, built-in attrs defined in std) done. Missing: closures.
  TODO: `[platform(windows)]` attribute-based conditional compilation (implement as macro).
- **Phase 5 (Libraries)**: COMPLETE — separate compilation (`--lib`), module linking (`-l`),
  `--std-decl` mode, TypeInfo cross-module (`LinkOnceODR` + COMDAT), generic dedup,
  RTTI/reflection (`TypeInfoExt` with fields/methods/attributes, `[Reflect]` attribute,
  `reflection-mode: none|annotated|all` in .proj)

Already implemented: control flow, arithmetic, mutability, ownership/copy semantics,
name mangling, binder/scope, diagnostics, imports, namespaces, LSP server, intrinsics (sizeof, alignof, typeof,
likely/unlikely, expect), type casting, pointer compatibility, number literals (separators, scientific notation),
generic constraints (where clauses), slices (str, i32[]), macros (expression fragments, local hygiene, multi-rule).

## Architecture

```
lexer/          Lexer, Token, TokenType
parser/         AST nodes (Declaration, Statement, Type, Generic, Scope)
binder/         Symbol table, scope resolution, ownership tracking, type validation
generator/      LLVM IR generation, intrinsics (sizeof, alignof, typeof), name mangling
evaluator/      Compile-time VM for constexpr/consteval (BytecodeCompiler, ConstVM, ConstValue)
visitor/        Statement/Declaration visitors
diagnostics/    Error reporting with suggestions and error codes
lsp/            Language Server Protocol (JsonRpc, LspTypes, LspServer)
utils/          Logger, StopWatch, string_utils
std/            Standard library definitions (.djinn files)
tests/          GoogleTest unit tests (~200+ cases across 31 files)
playground/     Test files for manual testing
examples/       Example .djinn programs
runtime/        C runtime for async (event loop, thread pool, coroutine wrappers)
docs/           Fumadocs (Next.js) documentation site
```

## Important Constraints

- **`std::types` namespace is special**: In `bindAll()`, the binder collects declarations for the FIRST program with
  `fileNamespace == "std::types"` (the prelude pass), then **skips all other** programs with that namespace. New std
  structs/enums must NOT use `namespace std::types;` unless added to `types.djinn` itself. Use a different namespace
  (e.g., `std::collections`) for new std files.

## Build & Test

- **Build system**: CMake with GLOB_RECURSE (new .cpp files in existing dirs auto-included)
- **Test framework**: GoogleTest
- **Test helper**: `DjinnCompiler::run()` — accepts source code + flags (.optimize, .includeStd, .generateBinary, etc.)
- **Test pattern**: Compile djinn source → verify diagnostics count, return code, or IR output

## Implemented Features (verified by tests)

- **Arithmetic**: sum, sub, mult, div (integer/float)
- **Control flow**: if/else/else if, while, do-while, for, range-for, switch/case, break/continue
- **Postfix operators**: i++, i-- (postfix increment/decrement)
- **Range-for**: `for (i32 i in 0..10)`, `for (0..5)` (anonymous), `..=` (inclusive end),
  bracket bounds `[0..10]` (closed), `[0..10)` (half-open)
- **Structs**: definition, brace init (positional + designated), field access, methods (block + arrow =>), static
  methods, properties (get/set), transparent types (struct size : u32)
- **Generics**: single/multi param structs, generic enums (optional\<T\>, result\<T,E\>), generic constructors (stack +
  heap), monomorphization, nested generics, where clause constraints
- **Enums**: tagged unions/ADTs, variant payloads, generic enums
- **Interfaces**: definition, struct implements, multiple interface implementation, generic interfaces
- **Functions**: regular, async, extern "C" (single + block syntax), variadic (...)
- **Ownership**: move semantics for structs, copy for primitives/pointers, use-after-move detection, reinitialize after
  move, scope tracking
- **Mutability**: mut keyword, immutable-by-default, error on immutable reassignment
- **Type system**: casting (truncation, widening, float<->int, pointer), auto type inference, pointer compatibility (
  void* coercion)
- **Intrinsics**: sizeof, alignof, typeof, likely/unlikely, expect
- **Pattern matching**: `is` expression for runtime type checking on `object` (`obj is i32`, `obj is f64`), with
  optional variable binding (`obj is i32 value` — extracts and casts `object.data` to target type)
- **Async/Await**: LLVM coroutines, yield, spawn, event loop runtime, await in sync (busy-loop fallback)
- **Imports/Namespaces**: import qualified names, file-scoped namespaces, nested namespaces
- **Slices**: str (string slices), typed array slices (i32[]), index access, len field
- **Constructors**: stack + heap (new), generic constructors, forward references (two-pass)
- **Name mangling**: C++ compatible, demangling support
- **Diagnostics**: error codes, suggestions, multiple concurrent errors
- **Number literals**: underscore separators (420_000), tick separators (800'000'000), scientific notation (1e9)
- **Macros**: declaration with `macro name { (params) => { body } }`, `expression` and `identifier` fragment types,
  literal token matching in rules, `local` modifier (per-param or rule-level), token-level substitution with parser
  re-parse, nested macro calls, multi-rule pattern matching by arity + literal tokens (first match wins, ambiguous
  rules rejected), side-effect warning (W6001) when non-local expression param used multiple times
- **Attributes**: parameterized (`[align(16)]`), named args (`[deprecated(message = "use v2")]`), centralized LLVM
  mapping (force-inline, no-inline, noreturn, hot, cold, nosync, nounwind, willreturn, norecurse), implicit nounwind
  on all functions, `[llvm("attr")]` escape hatch for raw LLVM attributes, built-in attrs defined in
  `std/sys/intrinsics.djinn` with `[attribute(target)]` meta-attribute
- **Static variables**: global variables with `static` keyword, immutable by default, `mut` for mutable,
  namespace-accessible, compile-time initialized, emitted as LLVM global variables
- **Library compilation**: `--lib` mode (skip main/runtime), `-l file.ll` linking via `llvm::Linker`,
  `--std-decl` (declarations only, link bodies from .ll), generic dedup via `LinkOnceODR` + COMDAT,
  project file (`djinn.proj`) with `library-mode`, `libs`, `reflection-mode` settings
- **RTTI/Reflection**: `TypeInfo` (16 bytes: id, size, name, kind) for boxing/variadics, deterministic FNV-1a
  type IDs cross-module, `TypeInfoExt` (fields, methods, attributes) opt-in via `[Reflect]` attribute or
  `reflection-mode: all`, zero-cost when unused (lazy generation), `AttributeInfo`, `FieldInfo`, `MethodInfo`
  structs in `std::types`

## Grammar (EBNF)

### Program Structure

```ebnf
program              = { import | extern_block | namespace | enum | interface | struct | function | constexpr_decl | macro_decl | static_var } ;

import               = "import" qualified_name ";" ;
qualified_name       = IDENTIFIER { "::" IDENTIFIER } ;
```

### Static Variables

```ebnf
static_var           = "static" [ "mut" ] type IDENTIFIER "=" expression ";" ;

(* Global variables require "static" keyword. Immutable by default, use "mut" for mutable.
   Accessible via namespace system. Initialized with compile-time evaluable expressions.

   Examples:
   static i32 MAX_SIZE = 1024;
   static mut i32 counter = 0;
*)
```

### Extern Block

```ebnf
extern_block         = "extern" STRING_LITERAL "{" { extern_function } "}" ;
extern_function      = type IDENTIFIER "(" [ param_list ] [ "," "..." ] ")" ";" ;
```

### Namespace

```ebnf
namespace            = "namespace" qualified_name ( ";" | "{" { struct | function | namespace } "}" ) ;
```

### Enum (Tagged Unions / ADT)

```ebnf
enum                 = "enum" IDENTIFIER [ generic_params ] "{" enum_variant { "," enum_variant } "}" ;
enum_variant         = IDENTIFIER "(" [ type { "," type } ] ")" ;
```

### Interface

```ebnf
interface            = "interface" IDENTIFIER [ generic_params ] "{" { method_signature } "}" ;
method_signature     = [ modifiers ] type IDENTIFIER [ generic_params ] "(" [ param_list ] ")" ";" ;
```

### Struct

```ebnf
struct               = [ attributes ] "struct" IDENTIFIER [ generic_params ] [ struct_base ] ( ";" | struct_body ) ;
struct_base          = ":" ( type | interface_list ) ;
interface_list       = IDENTIFIER { "," IDENTIFIER } ;
struct_body          = "{" { field | property | method } "}" ;

field                = [ "mut" ] type IDENTIFIER ";" ;
property             = type IDENTIFIER "{" { getter | setter } "}" ;
getter               = "get" ( ";" | "=>" expression ";" | block ) ;
setter               = "set" ( ";" | "=>" expression ";" | block ) ;
method               = [ modifiers ] type IDENTIFIER [ generic_params ] "(" [ param_list ] ")" ( ";" | "=>" expression ";" | block ) ;

modifiers            = { "public" | "private" | "static" } ;
attributes           = { "[" IDENTIFIER [ "(" attr_args ")" ] "]" } ;
attr_args            = attr_arg { "," attr_arg } ;
attr_arg             = [ IDENTIFIER "=" ] ( INTEGER_LITERAL | FLOAT_LITERAL | STRING_LITERAL | "true" | "false" | qualified_name ) ;
```

### Function

```ebnf
function             = [ "async" ] [ "constexpr" | "consteval" ] type IDENTIFIER "(" [ param_list ] ")" block ;
param_list           = parameter { "," parameter } ;
parameter            = type IDENTIFIER [ "mut" ] ;
```

### Constexpr / Consteval

```ebnf
constexpr_decl       = ( "constexpr" | "consteval" ) ( constexpr_var | constexpr_func ) ;
constexpr_var        = type IDENTIFIER "=" expression ";" ;
constexpr_func       = type IDENTIFIER "(" [ param_list ] ")" block ;

(* constexpr: evaluated at compile time if possible, falls back to runtime
   consteval: MUST be evaluated at compile time, error if not possible
   Uses a stack-based bytecode VM (evaluator/) for compile-time execution

   Examples:
   constexpr i32 MAX_SIZE = 1024;
   consteval i32 BUFFER = 64 * 1024;
   constexpr i32 square(i32 x) { return x * x; }
   consteval i32 factorial(i32 n) { if (n <= 1) { return 1; } return n * factorial(n - 1); }
*)
```

### Macros

```ebnf
macro_decl           = "macro" IDENTIFIER "{" { macro_rule } "}" ;
macro_rule           = [ "local" ] "(" macro_params ")" "=>" "{" token_stream "}" ;
macro_params         = macro_param { "," macro_param } ;
macro_param          = [ "local" ] ( fragment_type IDENTIFIER | IDENTIFIER ) ;
fragment_type        = "expression" | "identifier" | "literal" | "type" | "block" ;

(* Macros are expanded at parse time via token substitution + re-parse.
   Fragment types: expression (any expr), identifier (single ident), literal (int/float/string),
   type (any type), block ({ ... }).
   Bare identifiers in params are literal token matchers (pattern matching).

   The "local" modifier creates a temp variable (__macro_<name>_<param>) to avoid
   double evaluation / side effects. "local" before "(" applies to all expression params.
   Without "local", arguments are substituted directly (wrapped in parens for precedence).

   Multiple rules per macro: matched by arity + literal tokens (first match wins).
   Ambiguous rules (same signature) are rejected at parse time.
   Warning W6001 is emitted when a non-local expression param is used multiple times.

   Examples:
   macro square {
       (local expression v) => { v * v }
   }
   macro add {
       (expression a, expression b) => { a + b }
   }
   macro maybe_double {
       (local expression v, expression m) => { v * m }
       (local expression v) => { v * 2 }
   }
   macro calc {
       (double, local expression v) => { v + v }  // "double" is literal token
       (square, local expression v) => { v * v }
       (expression v) => { v }
   }
   macro apply {
       local (expression a, expression b) => { a + b }  // local on all params
   }

   i32 x = square(foo());       // evaluates foo() once via temp var
   i32 y = add(1 + 2, 3);       // expands to (1 + 2) + (3)
   i32 a = maybe_double(5, 3);  // matches rule 1 → 5 * 3
   i32 b = maybe_double(5);     // matches rule 2 → 5 * 2
   i32 c = calc(double, 5);     // matches literal "double" → 5 + 5
   i32 d = calc(7);             // matches expression-only rule → 7
*)
```

### Generics

```ebnf
generic_params       = "<" generic_param { "," generic_param } ">" ;
generic_param        = IDENTIFIER [ "=" type ] ;
generic_args         = "<" type { "," type } ">" ;
```

### Types

```ebnf
type                 = base_type { "*" } [ "[" "]" ] ;
base_type            = primitive_type | IDENTIFIER [ generic_args ] | "void" | "string" | "auto" ;

primitive_type       = integer_type | float_type ;
integer_type         = ( "i" | "u" ) DIGITS ;     (* i8, i16, i32, i64, u8, u16, u32, u64 *)
float_type           = "f" DIGITS ;                (* f32, f64 *)
```

### Statements

```ebnf
block                = "{" { statement } "}" ;

statement            = return_stmt
                     | if_stmt
                     | for_stmt
                     | while_stmt
                     | do_while_stmt
                     | switch_stmt
                     | break_stmt
                     | continue_stmt
                     | expression_stmt ;

return_stmt          = "return" [ expression ] ";" ;
break_stmt           = "break" ";" ;
continue_stmt        = "continue" ";" ;

if_stmt              = "if" "(" expression ")" block [ "else" ( if_stmt | block ) ] ;
for_stmt             = "for" "(" ( for_classic | for_range ) ")" block ;
for_classic          = [ expression ] ";" [ expression ] ";" [ expression ] ;
for_range            = [ type IDENTIFIER "in" ] range_expr ;
range_expr           = [ "[" ] expression ( ".." | "..=" ) expression [ "]" | ")" ] ;
while_stmt           = "while" "(" expression ")" block ;
do_while_stmt        = "do" block "while" "(" expression ")" ";" ;

switch_stmt          = "switch" "(" expression ")" "{" { switch_case } "}" ;
switch_case          = ( "case" expression | "default" ) ":" { statement } ;

expression_stmt      = expression ";" ;
```

### Expressions

```ebnf
expression           = assignment ;

assignment           = or_expr [ "=" assignment ] ;
or_expr              = and_expr { "||" and_expr } ;
and_expr             = equality { "&&" equality } ;
equality             = comparison { ( "==" | "!=" ) comparison } [ "is" type [ IDENTIFIER ] ] ;
comparison           = term { ( "<" | "<=" | ">" | ">=" ) term } ;
term                 = factor { ( "+" | "-" ) factor } ;
factor               = unary { ( "*" | "/" | "%" ) unary } ;
unary                = ( "!" | "-" | "*" | "&" ) unary | await_expr | postfix ;
await_expr           = "await" unary ;
postfix              = primary { "." IDENTIFIER | "[" expression "]" | "(" [ arg_list ] ")" | "++" | "--" } ;

primary              = INTEGER_LITERAL
                     | FLOAT_LITERAL
                     | STRING_LITERAL
                     | IDENTIFIER
                     | "(" expression ")"
                     | brace_initializer
                     | type_expression
                     | variable_decl ;

variable_decl        = type IDENTIFIER [ "=" expression ] ;
brace_initializer    = "{" [ init_element { "," init_element } ] "}" ;
init_element         = [ "." IDENTIFIER "=" ] expression ;

type_expression      = IDENTIFIER [ generic_args ] "::" IDENTIFIER "(" [ arg_list ] ")" ;

arg_list             = expression { "," expression } ;
```

### Literals

```ebnf
INTEGER_LITERAL      = DIGIT_SEQ [ EXPONENT ] ;
FLOAT_LITERAL        = DIGIT_SEQ "." DIGIT_SEQ [ EXPONENT ] ;
STRING_LITERAL       = '"' { any_char } '"' ;
IDENTIFIER           = LETTER { LETTER | DIGIT | "_" } ;

DIGIT_SEQ            = DIGIT { DIGIT | "_" | "'" } ;  (* separators: 420_000, 800'000'000 *)
EXPONENT             = ( "e" | "E" ) [ "+" | "-" ] DIGITS ;  (* scientific: 1e9, 2.5e-3 *)
DIGITS               = DIGIT { DIGIT } ;
DIGIT                = "0" | "1" | ... | "9" ;
LETTER               = "a" | ... | "z" | "A" | ... | "Z" | "_" ;

(* Number literal examples:
   42              — plain integer
   420_000         — underscore separator
   800'000'000     — tick separator (C++ style)
   1e9             — scientific notation (1000000000)
   2.5e-3          — float scientific (0.0025)
   3.14            — plain float
*)
```

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