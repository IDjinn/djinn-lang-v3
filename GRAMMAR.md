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

primitive_type       = integer_type | float_type | native_type ;
integer_type         = ( "i" | "u" ) DIGITS [ overflow_suffix ] ;  (* i8..i64, u8..u64, i32w, u32s, ... *)
float_type           = "f" DIGITS ;                                (* f16, f32, f64, f128 *)
native_type          = "nint" [ overflow_suffix ] | "nfloat" | "ndouble" ;
overflow_suffix      = "w" | "t" | "c" | "s" ;
                     (* w = wrapped (default, C-style), t = trapped (panic on overflow),
                        c = checked (throws Overflow, requires 'throws' function),
                        s = saturating (clamps to MIN/MAX) *)
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