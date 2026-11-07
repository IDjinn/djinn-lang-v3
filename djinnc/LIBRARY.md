# Djinn libraries (`.djlib`)

A `.djlib` is a single file with the compiled object **and** the Djinn interface of the library, so consumers keep the
full language semantics (macros, attributes, generics, constexpr, reflection) instead of only seeing symbols.

## CLI

```
djinnc lib.djinn --library -o lib.djlib          # build a library (no entry point is generated)
djinnc app.djinn --lib lib.djlib -o app.exe      # consume it (repeat --lib for several)
djinnc --inspect-lib lib.djlib                   # header, sections, integrity and the interface
djinnc lib.djinn --emit-lib-view -o view.txt     # build + inspection text (used by Compiler Explorer)
```

`DJINN_BUILD_ID` is stored in the header as the build id when set (commit hash, CI number...).

## Projects

A `djinn.proj` builds a library with `library-mode: true`. `libs:` lists what the project uses: `std`, or a name that resolves
to `<lib dir>/<name>.djlib`, where the directory comes from `--lib-dir` (default `build/libs`). `sources:` adds files outside
`src/`, relative to the project. `reflection-mode: all` is the same as `--reflect-all`.

```
compiler:
  library-mode: true
  output:
    file: "djinnc_testing.djlib"
    directory: "build/libs"
  libs: [
    'std',
    'testing'
  ]
  sources: [
    '../src/support/source/diagnostic_code.djinn'
  ]
```

A library can be built on another one. A `.djlib` has no dependency list, so the final program lists all of them.
Import a library after `namespace`: an `import` above it does not expose the names.

## File layout (all integers little-endian)

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic `DJINNLIB` |
| 8 | 4 | format version (1). Readers must reject a greater value |
| 12 | 4 | header size (128). Readers skip to this offset |
| 16 | 4 | flags. bit 0: built with `--std` |
| 20 | 4 | section count |
| 24 | 4 | section table offset (128) |
| 28 | 4 | compiler version, `major<<16 \| minor<<8 \| patch` (256 = 0.1.0), for comparison |
| 32 | 32 | version label, NUL padded free text (`0.1.0-dev`, `0.2.0-rc.1`, `nightly`...), for display |
| 64 | 32 | build id, NUL padded free text (commit hash...) |
| 96 | 32 | reserved, zero |

Section table: one 32 byte entry per section: `u32 kind, u32 pad, u64 offset, u64 size, u64 FNV-1a-64 checksum`.

| Kind | Section | Content |
|---|---|---|
| 1 | manifest | `u32 len + name`, `u32 len + target triple`, file count |
| 2 | interface | per file: `u32 len + path`, `u32 len + minified Djinn text` |
| 3 | object | native object file |

The interface is the original source minified (comments and whitespace removed) with the bodies of non generic,
non constexpr functions, constructors, operators and property accessors replaced by `{}`. Generics, macros,
constexpr and struct layouts stay complete.

## How it works

- Build: bodies are recorded by the parser (`Library.record_body`); the object is produced with no entry point and
  with core/std definitions demoted to declarations; sections are packed and checksummed.
- Consume: interface files are parsed first as header sources (`SourceFile.is_header`), so macros are known; their
  functions become `extern`, header globals become declarations; the object is extracted next to the output and
  linked, then removed.
- Dedup across modules: generic instances, constexpr functions and struct-instance methods are `linkonce_odr` in a
  comdat; typeinfo is `linkonce_odr`; reflection tables are internal.

## Reflection and debug symbols

- Reflection: `[Reflect]` types of a library are reflected in the library object (typeinfo is `linkonce_odr`), and
  consumers see the same types through the interface, so `typeof` works across the boundary.
- Debug: building the library with `-g` embeds debug info for the library code in its object, pointing at the
  original source paths, so a debugger steps into library code from the consumer (`-g` on both sides works).

## Roadmap / not yet supported

- Debug synergy: store the original sources (or a source map) in the library so debuggers and Compiler Explorer can
  show library code when the original path is gone; interface text is minified, so its line numbers do not match
  the original files and consumer diagnostics point at `<lib>!<path>`.
- Reflection synergy: a reflection section with the type/attribute tables so `--inspect-lib` lists reflected types,
  fields and attributes, and registry-style lookups across several libraries.

- Library depending on other libraries (manifest has no dependency list; header bytes 96..127 are reserved for it).
- Version policy: reject libraries from an incompatible compiler version, and check the interface hash.
- Std as a library (`--std` is still source based; flag bit 0 only records it).
- `inline` / `ForceInline` functions across libraries (bodies are stripped, so they are not inlined).
- Binary AST interface instead of minified source (faster load, smaller files); `.djni` interface-only files.
- Static variable initializers and global constructors across libraries (header globals are declarations only).
- Reflection method pointers (`funcPtr`) and `[Reflect]` tables of library types when mixed with `--reflect-all`.
- Cross-target libraries (one interface, several objects per triple).
- Incremental rebuild: skip packing when interface and object checksums are unchanged.
- Rename `is_header` to something that does not read as "C header" (it marks a library interface source).
- Compression of the interface and object sections.
