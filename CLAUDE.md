# Djinn Lang

Full compile, with lexer, parser and llvm ir generation. Focus in a language c-like but safe and C#/Rust inspired for
developer experience.

## Project Status

See `todo.md` for full roadmap. Summary:

- **Fase 1 (Fundacao)**: COMPLETA - generics, FFI extern "C", malloc/free, sizeof
- **Fase 2 (Estruturas)**: ~65% - enums, methods, constructors (incl. generic), array\<T\>. Faltam slices e string
  robusto
- **Fase 3 (Std)**: ~15% - I/O parcial via extern, array\<T\> collection. Faltam String, HashMap
- **Fase 4 (Avancado)**: ~50% - interfaces, async/await feitos. Faltam pattern matching, closures, constexpr

Extras ja implementados: control flow, aritmetica, mutabilidade, ownership/copy semantics,
name mangling, binder/scope, diagnostics, imports, namespaces, LSP server, intrinsics (sizeof, alignof, typeof,
likely/unlikely, expect), type casting, pointer compatibility, number literals (separators, scientific notation),
generic constraints (where clauses), slices (str, i32[]).

## Architecture

```
lexer/          Lexer, Token, TokenType
parser/         AST nodes (Declaration, Statement, Type, Generic, Scope)
binder/         Symbol table, scope resolution, ownership tracking, type validation
generator/      LLVM IR generation, intrinsics (sizeof, alignof, typeof), name mangling
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

## Build & Test

- **Build system**: CMake with GLOB_RECURSE (new .cpp files in existing dirs auto-included)
- **Test framework**: GoogleTest
- **Test helper**: `DjinnCompiler::run()` — accepts source code + flags (.optimize, .includeStd, .generateBinary, etc.)
- **Test pattern**: Compile djinn source → verify diagnostics count, return code, or IR output

## Implemented Features (verified by tests)

- **Arithmetic**: sum, sub, mult, div (integer/float)
- **Control flow**: if/else/else if, while, do-while, for, switch/case, break/continue
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
- **Async/Await**: LLVM coroutines, yield, spawn, event loop runtime, await in sync (busy-loop fallback)
- **Imports/Namespaces**: import qualified names, file-scoped namespaces, nested namespaces
- **Slices**: str (string slices), typed array slices (i32[]), index access, len field
- **Constructors**: stack + heap (new), generic constructors, forward references (two-pass)
- **Name mangling**: C++ compatible, demangling support
- **Diagnostics**: error codes, suggestions, multiple concurrent errors
- **Number literals**: underscore separators (420_000), tick separators (800'000'000), scientific notation (1e9)

## Quick Examples

```djinn
void main() {
    printf("hello world!");
}
```

```djinn
i64 sum(i64 a, i64 b) {
    return a + b;
}
```

```djinn
struct User {
    string* name;
    i8 age;
}
```

```djinn
struct Node<T> {
    optional<T*> next;
    optional<T*> previous;
}
```

```djinn
extern "C" {
    i32 printf(i8* format, ...);
    i32 puts(i8* s);
}
```

```djinn
// Static methods on structs
struct Console {
    public static c_result error(i8* message) {
        return write(2, message, strlen(message));
    }
}
```

```djinn
// Transparent types (newtype pattern)
struct size : u32;
struct c_result : i32;
struct bool : i1;
```

```djinn
// Async/Await (coroutines via LLVM)
async i32 compute(i32 x) {
    return x * 2 + 1;
}

async i32 main() {
    i32 result = await compute(10);
    printf("result: %d\n", result);
    return 0;
}
```

## Grammar (EBNF)

### Program Structure

```ebnf
program              = { import | extern_block | namespace | enum | interface | struct | function } ;

import               = "import" qualified_name ";" ;
qualified_name       = IDENTIFIER { "::" IDENTIFIER } ;
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

(* Examples:
   enum Color { Red(), Green(), Blue() }
   enum optional<T> { Empty(), Value(T) }
   enum result<T, E> { Ok(T), Error(E) }
*)
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
attributes           = { "[" IDENTIFIER "]" } ;

(* Examples:
   struct Point { i32 x; i32 y; }
   struct Array<T> { T* data; i32 size; }
   struct Pair<K, V> { K key; V value; }
   struct Size : i32;  // transparent type
   struct MyStruct : IComparable { ... }
*)
```

### Function

```ebnf
function             = [ "async" ] type IDENTIFIER "(" [ param_list ] ")" block ;
param_list           = parameter { "," parameter } ;
parameter            = type IDENTIFIER [ "mut" ] ;
```

### Generics

```ebnf
generic_params       = "<" generic_param { "," generic_param } ">" ;
generic_param        = IDENTIFIER [ "=" type ] ;
generic_args         = "<" type { "," type } ">" ;

(* Examples:
   struct Array<T> { ... }
   struct Map<K, V> { ... }
   enum result<T, E> { Ok(T), Error(E) }
   optional<i32>::Value(42)
   result<i32, i8*>::Ok(100)
*)
```

### Types

```ebnf
type                 = base_type { "*" } [ "[" "]" ] ;
base_type            = primitive_type | IDENTIFIER [ generic_args ] | "void" | "string" | "auto" ;

primitive_type       = integer_type | float_type ;
integer_type         = ( "i" | "u" ) DIGITS ;     (* i8, i16, i32, i64, u8, u16, u32, u64 *)
float_type           = "f" DIGITS ;                (* f32, f64 *)

(* Examples:
   i32, i64, u8, u32
   f32, f64
   i8*, i32**
   string, string*
   Array<i32>, Map<string, i64>
   optional<T*>
*)
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
for_stmt             = "for" "(" [ expression ] ";" [ expression ] ";" [ expression ] ")" block ;
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
equality             = comparison { ( "==" | "!=" ) comparison } ;
comparison           = term { ( "<" | "<=" | ">" | ">=" ) term } ;
term                 = factor { ( "+" | "-" ) factor } ;
factor               = unary { ( "*" | "/" | "%" ) unary } ;
unary                = ( "!" | "-" | "*" | "&" ) unary | await_expr | postfix ;
await_expr           = "await" unary ;
postfix              = primary { "." IDENTIFIER | "[" expression "]" | "(" [ arg_list ] ")" } ;

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

(* Examples:
   x = 10
   a + b * c
   point.x
   array[0]
   printf("hello")
   Point p = { .x = 10, .y = 20 }
   optional<i32>::Value(42)
   result<i32, string*>::Error("fail")
*)
```

### Literals

```ebnf
INTEGER_LITERAL      = DIGITS ;
FLOAT_LITERAL        = DIGITS "." DIGITS ;
STRING_LITERAL       = '"' { any_char } '"' ;
IDENTIFIER           = LETTER { LETTER | DIGIT | "_" } ;

DIGITS               = DIGIT { DIGIT } ;
DIGIT                = "0" | "1" | ... | "9" ;
LETTER               = "a" | ... | "z" | "A" | ... | "Z" | "_" ;
```

## Standard Library

Files in `std/` directory. All use `import std::types;` for base types.

### std::types (std/types/types.djinn)

```djinn
namespace std::types;

struct size : u32;
struct size_long : u64;
struct c_result : i32;
struct bool : i1;

enum optional<T> {
    Empty(),
    Value(T)
}

enum result<T, E> {
    Ok(T),
    Error(E)
}

struct string {
    i8* data;
    size size;
    i32 flags; // metadata: static str, expandable, etc.
}

struct vector<T> {
    T[] value;
    size size;
}

struct dynamic_vector<T> {
    vector<T> vector;
    size capacity;
}
```

### std::sys::libc (std/sys/libc.djinn)

```djinn
import std::types;
namespace std::libc;

extern "C" {
    c_result printf(i8* fmt, ...);
    void* malloc(size_long size);
    void free(void* ptr);
    c_result strlen(i8* s);
}
```

### std::sys::Console (std/sys/console.djinn)

```djinn
import std::types;
namespace std::sys;

struct Console {
    public static c_result error(i8* message) {
        return write(2, message, strlen(message));
    }
    public static c_result printf(i8* format) {
        return printf(format);
    }
}
```

### std::debug (std/sys/debug.djinn)

```djinn
namespace std::debug;

struct Debug {
    public static void pause() {
        debugtrap();
    }
}
```

### std::sys::io (std/sys/io.djinn)

```djinn
import std::types;
namespace std::io;

struct FILE : void*;

extern "C" {
    // File ops: fopen, fclose, fflush, freopen
    // Positioning: fseek, ftell, rewind, fgetpos, fsetpos
    // Binary: fread, fwrite
    // Char I/O: fgetc, fputc, getc, putc, getchar, putchar, ungetc
    // String I/O: fgets, fputs, puts, gets
    // Formatted: fprintf, fscanf, sprintf, snprintf, sscanf
    // Error: feof, ferror, clearerr, perror
    // File mgmt: remove, rename, tmpfile, tmpnam
    // POSIX: open, close, read, write, lseek
    // FD: fileno, fdopen, dup, dup2
    // Pipes: pipe, popen, pclose
}
```

### std::math (std/sys/math.djinn)

```djinn
import std::types;
namespace std::math;

extern "C" {
    // Trigonometric: sin, cos, tan, asin, acos, atan, atan2
    // Hyperbolic: sinh, cosh, tanh, asinh, acosh, atanh
    // Exponential/Log: exp, exp2, expm1, log, log10, log2, log1p
    // Power: pow, sqrt, cbrt, hypot
    // Rounding: ceil, floor, trunc, round, fmod, remainder
    // Other: fabs, fmax, fmin, erf, erfc, tgamma, lgamma
    // Float (f32) versions have 'f' suffix: sinf, cosf, sqrtf, etc.
}
```