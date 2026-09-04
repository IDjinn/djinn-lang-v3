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
function             = [ "async" ] [ "constexpr" | "consteval" ] type IDENTIFIER "(" [ param_list ] ")" [ throws_clause ] { contract_clause } block ;
param_list           = parameter { "," parameter } ;
parameter            = type IDENTIFIER [ "mut" ] ;

throws_clause        = "throws" | "throws" "(" type { "," type } ")" ;
contract_clause      = ( "require" | "ensure" ) "(" expression ")"
                     | ( "require" | "ensure" ) block ;
```

### Error handling (throw / throws / try)

```ebnf
throw_statement      = "throw" expression ";" ;
try_expression       = "try" expression [ "?:" expression ] ;
try_catch_statement  = "try" block { catch_clause } [ "finally" block ] ;
catch_clause         = "catch" "(" catch_pattern ")" block ;
catch_pattern        = ( error_type | "Error" | "_" ) [ identifier ] ;
```

(* Error values are structs deriving a builtin error type (`Exception`,
`Generic`, `DivisionByZero`, `Argument`, `Overflow`, `OutOfBounds`,
`InvalidArgument`, `ContractViolation`, `ForeignError`, or user-defined
`struct MyError : Base;`) with layout `{ tag: i32, message: str, type_name: str }`.

- `throws(T...)` declares what a function may throw; bare `throws` = anything.
- `throw ErrorType("message")` sets the error state and returns the default value.
- `try expr ?: fallback` catches a failed operand and yields the fallback; a bare `try expr` propagates (allowed inside a
  `throws` caller).
- A `switch` over a throwing call matches its outcome (`Result`/`_` for
  success, `Error`/error types for failures) — see Switch expressions.
- Calling a throwing function without `try` is an error outside `throws`
  functions; inside them the error propagates automatically.
- An exception escaping `main() throws` is reported at runtime (`Type: message`,
  pointing at the outermost unhandled call — the origin rises as the error
  propagates; debug builds also print the stack trace captured at the throw
  site) and aborts.

**Native exceptions (opt-in)**: with `--exceptions` (or `compiler.exceptions:
true` in `djinn.proj`) the whole build switches the propagation mechanism from
error-state returns to LLVM zero-cost unwinding (tables; no cost on the happy
path) and enables the block-form `try/catch/finally`:

- `catch (MyError e)` matches by error type (derived types match too),
  `catch (Error e)`/`catch (_)` catch everything, binding the error value.
- `finally` runs on every non-unwinding path and once more before a re-throw.
- Foreign C++ exceptions crossing `extern` boundaries are wrapped as
  `ForeignError`; C++ code can catch `djinn::error` (a `std::exception`)
  directly — see `runtime/djinn_error.h`.
- Throwing across `await` travels in the coroutine's promise error slot and
  re-throws at the resume point.
- The expression forms (`try expr`, outcome `switch`) keep their semantics and
  lower to the same unwinding machinery.
- Block-form `try/catch` without `--exceptions` is a compile error (9009);
  native exceptions require an AOT build (the JIT falls back to it).

Runtime error reporting detail is gated by build mode (`--debug`/`--release`,
`build-mode:` in `djinn.proj`; default is debug):

- Debug: full reports — source snippet with caret, operand values, variable
  assignment history and a native stack trace (captured at the raise site,
  symbolized from debug info). Source snippets are read from disk at report
  time (JIT runs fall back to the in-memory source) — no line text is baked
  into the binary.
- Release: minimal reports — message, `file:line` and operand values only; no
  per-site source strings, traces or tracking, keeping binaries small.

Debug builds also split the readable IR from the symbol payload: `<name>.ll`
is written without debug metadata while `<name>.debug.ll` carries it and is
what gets compiled with `-g` (CodeView/PDB). `--embed-debug-info` (or
`compiler.split-debug-info: false` in `djinn.proj`) restores the single-file
output.

Error enforcement level (CompilerOptions::errorEnforcement):

- `Off`: no error-flow checks.
- `Runtime`: `try` enforcement + propagation only (no compile-time analysis).
- `CompileTime` (default): additionally, calls whose outcome is provable are checked — a constexpr call that always
  throws with constant arguments, or a
  `require` clause violated by constant arguments, is a compile error when unhandled (diagnostics 9006/9007); inside
  `try` the error is handled and nothing is reported.
- `Strict`: like CompileTime, but every call to a throwing function must be wrapped in `try` (bare `try` to propagate),
  even inside `throws` functions. *)

### Contracts (require / ensure)

```ebnf
contract_clause      = ( "require" | "ensure" ) "(" expression ")"
                     | ( "require" | "ensure" ) block ;
```

(* Contracts are clauses between a function's signature and its body:

- `require(cond)` — checked at function entry; throws `ContractViolation` on failure. Block form
  `require { return cond; }` is also allowed.
- `ensure(return == expr)` — checked before each return; the return value is exposed through the `return`
  pseudo-variable.
- A function with contracts is implicitly throwing: call sites must use `try`.
- With the default enforcement level, `require` clauses decided by constant arguments are verified at compile time (see
  error enforcement above).

Example:

```djinn
i32 clamp(i32 value, i32 lo, i32 hi)
    require(lo <= hi)
    ensure(return >= lo)
{
    return value;
}
i32 ok = try clamp(5, 1, 10) ?: 0;
```

*)

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
integer_type         = ( "i" | "u" ) DIGITS { integer_suffix } ;  (* i8..i64, u8..u64, i32w, u32n, i32nt, ... *)
float_type           = "f" DIGITS ;                                (* f16, f32, f64, f128 *)
native_type          = "nint" [ overflow_suffix ] | "nfloat" | "ndouble" ;
integer_suffix       = non_zero_suffix | overflow_suffix ;
non_zero_suffix      = "n" ;   (* type cannot hold the value 0 (i32n: 1..MAX and MIN..-1,
                                   u32n: 1..MAX); combinable with overflow suffix in
                                   either order (i32nw / i32wn) *)
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
                     | switch_expression
                     | variable_decl ;

variable_decl        = type IDENTIFIER [ "=" expression ] ;
brace_initializer    = "{" [ init_element { "," init_element } ] "}" ;
init_element         = [ "." IDENTIFIER "=" ] expression ;

type_expression      = IDENTIFIER [ generic_args ] "::" IDENTIFIER "(" [ arg_list ] ")" ;

switch_expression     = "switch" expression "{" { switch_arm [ "," ] } "}" ;
switch_arm            = IDENTIFIER [ IDENTIFIER ] "->" arm_body ;
arm_body              = expression | block ;

arg_list             = expression { "," expression } ;
```

(* Switch expressions pattern-match on two kinds of operands:

**Enum values** — arms name variants; an optional binding extracts the payload:

```
i32 result = switch opt {
    Value val -> val,
    Empty -> -1
};
```

`_` is a catch-all arm matching any variant (no binding). All arms must yield
the same type.

**Throwing calls** — the arms match the call's outcome, `try`-style:

```
i32 val = switch division(1, 0) {
    _ v -> v,
    DivisionByZeroException divByZero -> -1
};
```

- `Result [v]` or `_ [v]` match success; the binding receives the call's
  return value (first success arm wins).
- `Error [e]` matches any error; the binding receives the thrown error value
  (`{ tag, message, type_name }`).
- An error type name (`DivisionByZeroException e`) matches that type and every
  type deriving from it.
- Error arms are tried in source order; if none matches, the error propagates
  inside a `throws` function or aborts with the uncaught-exception report.
- For enum-returning throwing calls, variant names win over the outcome arms,
  so `switch makeResult() { Ok v -> v, Error e -> ... }` still matches variants.

Any arm body may instead be a block of statements (`Error e -> { ... }`). Inside
an arm block, `yield expr;` produces the arm's value and execution continues
after the switch, and `throw` exits the enclosing function; every path through
the block must end in one of them. `return` is not allowed inside an arm block —
it would bypass the switch value and confuse the flow, so use `yield` (arm
value) or `throw` (propagate) instead. (A bare `yield;` is the coroutine
suspend and is unrelated to switch arms.) *)

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