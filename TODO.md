## 1. Gestão de Memória (CRÍTICO)

Você já mencionou stack/heap. Precisa de:

- Alocadores: malloc/free ou wrappers LLVM
- Modelo de ownership: Decidir se vai ser:
    - Manual (C-style): new/delete
    - RAII (C++-style): destructors automáticos
    - Ownership (Rust-style): borrow checker
    - GC (Go/Java-style): garbage collector

import std.io;
import std.collections.Vec;

// ou
use std::io::println;
Precisa de:

- Parser para imports
- Namespace resolution
- Compilação multi-arquivo
- Linking

3. Features de Linguagem Essenciais

Você já tem:

- ✅ Structs
- ✅ Generics (acabou de implementar!)
- ✅ Funções
- ✅ Tipos básicos
- ✅ Ponteiros

Falta:

- Enums/Sum types:
  enum Option<T> {
  Some(T),
  None
  }

enum Result<T, E> {
Ok(T),
Err(E)
}

- Traits/Interfaces (para generics bounded):
  trait Display {
  void display();
  }

struct Point<T> where T: Display { ... }

- Slices (super importante para arrays):
  i32[] slice = array[0..10];

- Methods em structs:
  struct Vec<T> {
  fn push(T item) { ... }
  fn pop() -> Option<T> { ... }
  }

- Error handling robusto:
  Result<i32, string> divide(i32 a, i32 b) {
  if (b == 0) return Err("division by zero");
  return Ok(a / b);
  }

4. Loops e Controle de Fluxo

Você tem if/return? Precisa de:
for (i in 0..10) { }
while (condition) { }
loop { break; continue; }

5. FFI (Foreign Function Interface)

Para chamar C e usar libc:
extern "C" {
fn printf(string format, ...);
fn malloc(i64 size) -> *void;
}

Como fazer uma std?

Opção 1: Std em Djinn (Recomendado)

Estrutura:
std/
├── core/ # Primitivos, intrinsics
│ ├── mem.djinn # malloc, free, memcpy
│ └── ops.djinn # operadores, traits
├── collections/
│ ├── vec.djinn
│ ├── string.djinn
│ └── hashmap.djinn
├── io/
│ ├── stdio.djinn
│ └── file.djinn
└── prelude.djinn # Auto-importado

std/collections/vec.djinn:
extern "C" {
fn malloc(i64 size) -> *void;
fn realloc(*void ptr, i64 size) -> *void;
fn free(*void ptr);
}

struct Vec<T> {
*T data;
i64 len;
i64 capacity;
}

fn Vec<T>::new() -> Vec<T> {
return Vec<T> {
data: null,
len: 0,
capacity: 0
};
}

fn Vec<T>::push(T item) {
if (this.len == this.capacity) {
this.grow();
}
this.data[this.len] = item;
this.len++;
}

fn Vec<T>::grow() {
i64 new_cap = if (this.capacity == 0) 4 else this.capacity * 2;
this.data = realloc(this.data, new_cap * sizeof(T));
this.capacity = new_cap;
}

Opção 2: Intrinsics no Compilador

Implementar funções especiais direto no generator:

```cpp
// No Generator
void Generator::generate_intrinsic_call(const std::string& name, ...) {
  if (name == "sizeof") {
    // Gera LLVM sizeof
  } else if (name == "memcpy") {
    // Gera llvm.memcpy
  }
}
```

Opção 3: Bindings para libc

Criar wrappers automáticos:

```cpp
// std/sys/libc.djinn
extern "C" {
  fn puts(string s) -> i32;
  fn printf(string fmt, ...) -> i32;
  fn malloc(i64 size) -> *void;
  fn free(*void ptr);
}
```

Roadmap Sugerido (Por Prioridade)

Fase 1: Fundação (Agora)

1. ✅ Generics (você acabou de fazer!)
2. ⚡ FFI básico - chamar funções C
3. ⚡ Malloc/Free - via libc
4. ⚡ Sizeof operator - para alocar

Fase 2: Estruturas Básicas

5. Enums - Option, Result<T,E>
6. Methods - funções em structs
7. Slices - acesso a arrays
8. String type robusto

Fase 3: Std Básica

9. Vec - dynamic array
10. String - dynamic string
11. I/O básico - print, read
12. HashMap<K,V> - hash table

Fase 4: Features Avançadas

13. Traits/Interfaces
14. Pattern matching
15. Closures
16. Async (opcional)

Exemplo Prático de std Mínima

std/prelude.djinn:
// Auto-importado em todo arquivo

fn print(string s) {
extern_printf("%s\n", s);
}

fn panic(string msg) {
print(msg);
exit(1);
}

std/mem.djinn:
extern "C" {
fn malloc(i64 size) -> *void;
fn free(*void ptr);
}

fn alloc<T>() -> *T {
return malloc(sizeof(T)) as *T;
}

fn dealloc<T>(*T ptr) {
free(ptr as *void);
}

Uso:
import std.collections.Vec;
import std.mem;

void main() {
Vec<i32> numbers = Vec::new();
numbers.push(42);
numbers.push(100);

      print("Length: " + numbers.len);

}

Próximos Passos Imediatos

1. Implementar extern "C" no parser
2. Adicionar sizeof operator
3. Criar std/core/mem.djinn com malloc/free
4. Implementar methods em structs (fn Vec<T>::push)
5. Criar Vec como primeira estrutura da std

Quer começar por qual dessas features? Recomendo FFI + sizeof primeiro, pois são a base para tudo.
