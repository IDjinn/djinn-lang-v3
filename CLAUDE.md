# Djinn Lang

Full compile, with lexer, parser and llvm ir generation. Focus in a language c-like but safe and c#/rust inspired for
developer experiencefaça a

## Syntax

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
type User {
    string* name;
    i8 age;
}
```

```djinn
type Node<T> {
    optional<T*> next;
    optional<T*> previous;
}
```

```djinn
extern "C" {
    i32 printf(*i8 format, ...);
    i32 puts(*i8 s);
}
```