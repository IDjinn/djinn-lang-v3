## TODO

### Language Features

1. **Closures** - funções anônimas com captura de escopo
2. **Pattern matching em enums** - match/switch com destructuring de tagged unions
3. ~~**Null handling operators**~~ - `?.` (null-conditional), `??` (null-coalescing), `??=` (null-coalescing
   assignment), `!.` (null-forgiving) (DONE: `null` keyword + `T?` type, `??` short-circuit, `?.`/`!.`/`??=` parsed;
   smart-cast em if pendente)
4. **Operator overloading** - operadores custom para structs
5. **Iterators** - protocolo de iteração para for-each em coleções (array<T>, map<K,V>)
6. ~~**Destructuring**~~ - desestruturação de structs em variáveis (DONE: `auto { x, y } = expr;`)
7. **Error propagation** - operador `?` para result<T,E> (early return on error)
8. ~~**String interpolation**~~ - `"hello {name}"` syntax (DONE em arg de função; inclui block strings `"""..."""`)
9. **Defer** - cleanup automático de recursos (Go/Zig style)

### Standard Library

10. **I/O wrappers** - print/println próprio da linguagem