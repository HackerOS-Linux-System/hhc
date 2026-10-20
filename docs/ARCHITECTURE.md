# hhc architecture

## How a GCC frontend plugs in

GCC has no plugin API for languages; a frontend is a directory
`gcc/<lang>/` that the top-level `configure` discovers through
`gcc/<lang>/config-lang.in`. That is why this repository only has to *copy*
`frontend/gcc/hsharp` into a GCC checkout (`lua5.5 hhc.lua overlay`) - no core
file is modified.

| file | role |
|------|------|
| `config-lang.in` | registers language `hsharp`, compiler `hsharp1`, GTY files |
| `Make-lang.in` | rules for `hsharp1` (compiler proper) and `hhc` (driver), install/clean hooks |
| `lang.opt` / `lang.opt.urls` | command line options (`-fhsharp-dump-ast`, ...) |
| `lang-specs.h` | maps `*.h#` to `hsharp1` and then to the assembler/linker |
| `hsharp-spec.cc` | driver hooks (`hhc` itself) |
| `hsharp-lang.cc` | `lang_hooks`: init, option handling, `parse_file`, type hooks |
| `hsharp-lex.cc` | lexer: `;;`, `//...\\`, `///`, strings with escapes, numbers |
| `hsharp-parse.cc` | recursive-descent parser -> plain C++ AST |
| `hsharp-gen.cc` | type checker + lowering of the AST to GENERIC, hands functions to cgraph |
| `hsharp.h` | shared tokens / types / AST |

## Pipeline

```
hhc file.h#                       driver (gcc.cc + hsharp-spec.cc)
 └─ hsharp1 file.h# ...           lang-specs.h
     ├─ hs_lex_file               tokens (linemap for diagnostics)
     ├─ hs_parse                  AST
     ├─ checker (hsharp-gen.cc)   types, scopes, literal typing
     └─ lowering                  GENERIC: BIND_EXPR / COND_EXPR / GOTO_EXPR ...
         └─ cgraph_node::finalize_function  ->  GCC middle end -> asm
 └─ as, collect2/ld               produced by the stock driver
```

`write(a + to_string(b))` is flattened at compile time into `printf` calls,
so no H# runtime library is needed yet.

## Naming: `hhc`

`Make-lang.in` builds the driver as `hhc$(exeext)`. The install name goes
through `program_transform_name`, so `--program-prefix/--program-suffix`
behave as for `gcc`. For cross builds a `<target>-hhc` link is installed, like
`gdc` does.

## One core change: the language mask

GCC stores one bit per front end language in the low 16 bits of
`cl_option::flags` (`gcc/opts.h`), and **every** `lang.opt` in the tree counts,
enabled or not. GCC 16 already fills all 16 slots (Ada x3, Algol 68, C, C++,
COBOL, D, Fortran, Go, LTO x2, Modula-2, ObjC, ObjC++, Rust), so a 17th
language fails to build with

    #error the number of languages exceeds the implementation limit

The `overlay` step therefore runs `build/lib/gccpatch.lua`, which counts the
`Language` stanzas and shifts the option class/attribute bits above them
(`CL_PARAMS` ... `CL_PCH_IGNORE`) by exactly the number of missing slots. It is
idempotent, needs no per-release patch file, and becomes a no-op once upstream
widens the mask itself. All of those constants are used symbolically, so the
change is invisible to the rest of GCC.

## Keeping up with GCC

The frontend is written against the stable interfaces that every in-tree
frontend uses (`lang_hooks`, `build_decl`, `cgraph_node::finalize_function`).
When a new GCC release changes one of them, there are two ways to adapt:

1. fix `frontend/gcc/hsharp` with `#if GCC_VERSION`-style guards, or
2. put a patch in `frontend/patches/gcc-<major>/NN-name.patch` (applied with
   `patch -p1` by the `overlay` step, idempotently).

The weekly CI run builds the newest release, so breakage shows up quickly.

## Roadmap

1. **stage 0 (now)** scalars, functions, control flow, `write`.
2. structs and `impl` methods (RECORD_TYPE, COMPONENT_REF).
3. enums and `match` (SWITCH_EXPR, tagged unions).
4. arrays / slices and a small `libhsharp` runtime (strings, `to_string`,
   allocation) built as a GCC target library (`target_libs=` in
   `config-lang.in`, like libphobos for D).
5. modules and `use` (own module files, like D's `.di`).
6. generics (monomorphisation), traits, closures.
7. FFI (`extern`) - maps directly onto GENERIC function declarations.
8. DWARF names for H# types, `-fhsharp-...` warning flags, `hhc.1` man page.
