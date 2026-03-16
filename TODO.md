# Djinn Lang - Roadmap & Status

## Fase 1: Fundacao - COMPLETA

- [x] Generics (multi-param: 2, 3, 4 type params)
- [x] FFI basico - extern "C" (printf, block syntax, multiplos blocos)
- [x] Malloc/Free - via libc (intrinsics)
- [x] Sizeof operator (i8, i32, i64, structs)

## Fase 2: Estruturas Basicas - ~65%

- [x] Enums / Tagged unions (definicao, variantes, payload)
- [x] Methods em structs (block body, arrow =>, parametros)
- [x] Constructors (stack, heap, field access)
- [x] Generic struct constructors (Box\<i32\>(42), array\<T\>())
- [x] Slices - acesso a arrays
- [x] String type robusto

## Fase 3: Std Basica - ~15%

- [x] array\<T\> - generic collection (push, get, set, grow, destroy)
- [x] String - dynamic string
- [x] I/O basico - funciona via extern printf/puts, sem wrapper proprio
- [x] HashMap\<K,V\> - hash table

## Fase 4: Features Avancadas - ~50%

- [x] Traits/Interfaces (basica, generica, struct implementando)
- [ ] Pattern matching (existe exemplo em examples/, nao testado)
- [ ] Closures
- [x] Async/Await - coroutines via LLVM coro intrinsics, presplitcoroutine, await loop

---

## Extras implementados (fora do roadmap original)

- [x] Control flow (if/else)
- [x] Aritmetica completa (+, -, *, /)
- [x] Math C extern (sqrt, sqrtf, pow)
- [x] Structs + brace init (positional e designated)
- [x] Mutabilidade (mut) e validacao de imutaveis
- [x] Ownership / Copy semantics (primitivos copiam, ponteiros copiam)
- [x] Name mangling (tipos signed/unsigned, void, float)
- [x] Binder / Scope resolution
- [x] Diagnostics / Error reporting (com sugestoes)
- [x] Imports (basico, qualified, multiplos)
- [x] Namespaces (basico, nested, com structs)
- [x] LSP server (JsonRpc, LspTypes)

---

## Proximos passos sugeridos

3. **Pattern matching** - match em enums/tagged unions
5. **I/O wrappers** - print/println proprio da linguagem
7. **Closures** - funcoes anonimas