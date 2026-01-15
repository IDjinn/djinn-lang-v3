# Djinn Language

A c-like language with focus in development experience

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

## Goals

- [ ] memory control & safety
- [ ] async, multi-thread and parallelism
- [ ] standard with all needs for any kind of application

## Specs

Type system

```djinn
type User {
    string* name;
    i8 age;
}
```

Parallelism

```djinn
void process_io() {
    await async parallel { 
        this.requests.dequeue().execute(TIMEOUT);
    }
    
    parallel { 
        this.requests.dequeue().execute(TIMEOUT);
    }
    
    atomic {
        this.bank.decrease_currency(99.99);
        this.bank.save();
    }
}
```

```djinn
i64 div(i64 a, i64 b) try {
   return a / b;
} | return -1
```

```djinn
type LinkedList<T> {
    optional<T> next;
    optional<T> prev;
}
```

```djinn
result<i64 | MathError>
```