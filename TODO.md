# Djinn Lang - Roadmap & Status

## Fase 1: Fundacao - COMPLETA

- [x] Generics (multi-param: 2, 3, 4 type params)
- [x] FFI basico - extern "C" (printf, block syntax, multiplos blocos)
- [x] Malloc/Free - via libc (intrinsics)
- [x] Sizeof operator (i8, i32, i64, structs)

## Fase 2: Estruturas Basicas - ~60%

- [x] Enums / Tagged unions (definicao, variantes, payload)
- [x] Methods em structs (block body, arrow =>, parametros)
- [x] Constructors (stack, field access)
- [ ] Slices - acesso a arrays
- [ ] String type robusto

## Fase 3: Std Basica - ~10%

- [ ] Vec\<T\> - dynamic array
- [ ] String - dynamic string
- [~] I/O basico - funciona via extern printf/puts, sem wrapper proprio
- [ ] HashMap\<K,V\> - hash table

## Fase 4: Features Avancadas - ~25%

- [x] Traits/Interfaces (basica, generica, struct implementando)
- [ ] Pattern matching (existe exemplo em examples/, nao testado)
- [ ] Closures
- [ ] Async (opcional)

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

1. **Slices** - base para manipulacao de arrays
2. **String type** - wrapper seguro sobre i8*
3. **Pattern matching** - match em enums/tagged unions
4. **Vec\<T\>** - dynamic array (depende de slices + constructors + methods)
5. **I/O wrappers** - print/println proprio da linguagem
6. **HashMap\<K,V\>** - hash table
7. **Closures** - funcoes anonimas
8. **Async** - opcional, futuro