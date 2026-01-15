# Djinn Lang

Full compile, with lexer, parser and llvm ir generation. Focus in a language c-like but safe and C#/Rust inspired for
developer experience.

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
function             = type IDENTIFIER "(" [ param_list ] ")" block ;
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
unary                = ( "!" | "-" | "*" | "&" ) unary | postfix ;
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

### std::types

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
```

### std::sys::math

```djinn
import std::sys::math;

// Trigonometric: sin, cos, tan, asin, acos, atan, atan2
// Hyperbolic: sinh, cosh, tanh, asinh, acosh, atanh
// Exponential/Log: exp, exp2, log, log10, log2
// Power: pow, sqrt, cbrt, hypot
// Rounding: ceil, floor, trunc, round, fmod
// Other: fabs, fmax, fmin, erf, tgamma

// Float (f32) versions have 'f' suffix: sinf, cosf, sqrtf, etc.
```

### std::sys::io

```djinn
import std::sys::io;

// File operations: fopen, fclose, fread, fwrite
// Character I/O: fgetc, fputc, getchar, putchar
// String I/O: fgets, fputs, puts
// Formatted I/O: fprintf, fscanf, sprintf, snprintf
// Low-level: open, close, read, write, lseek
```

### std::sys::libc

```djinn
import std::sys::libc;

// Memory: malloc, free, realloc, calloc, memcpy, memset, memmove
// String: strlen, strcpy, strncpy, strcmp, strcat
// I/O: printf, puts
```