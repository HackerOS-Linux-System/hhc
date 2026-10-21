# hhc

**hhc** is the GCC-based compiler for [H#](https://github.com/HackerOS-Linux-System/H-Sharp),
the programming language of HackerOS.

It is built the same way the D language gets `gdc`: H# is a *frontend* inside
the GNU Compiler Collection. GCC's own backend does the optimisation and code
generation, so `hhc` produces native code for every architecture GCC supports.

```
H# source  ->  hsharp1 (lexer, parser, checker -> GENERIC)  ->  GCC middle/back end  ->  binary
                 ^ this repository                              ^ stock GCC
```

| binary     | what it is                                    | GCC counterpart |
|------------|-----------------------------------------------|-----------------|
| `hhc`      | compiler driver (the command you run)         | `gcc` / `gdc`   |
| `hsharp1`  | the compiler proper (installed in `libexec`)  | `cc1` / `d21`   |

The existing `h#` tool (Rust + LLVM, interpreter, package tooling) lives in the
H-Sharp repository; `hhc` is the GCC route to the same language.

## Using it

```sh
hhc hello.h# -o hello && ./hello
hhc -O2 prog.h# -o prog
hhc -S prog.h#                 # assembly
hhc -fsyntax-only prog.h#      # check only
```

```
;; hello.h#
fn main() is
    write("Hello from H#!")
end
```

## Building hhc from source

Everything is driven by a small pure-Lua 5.5 build system (no Lua libraries
needed). It fetches the **newest stable GCC release** straight from git, drops
the H# frontend into the tree, configures, builds, installs, tests and
packages.

```sh
sh scripts/bootstrap-lua.sh           # only if you do not have lua5.5 yet
lua5.5 hhc.lua deps                 # apt / dnf / pacman / zypper packages
lua5.5 hhc.lua all                  # fetch -> overlay -> configure -> build -> install -> test -> package
./_install/bin/hhc --version
```

Single steps, useful while hacking on the frontend:

```sh
lua5.5 hhc.lua overlay              # re-sync frontend/gcc/hsharp into the GCC tree
lua5.5 hhc.lua build                # incremental: only the changed frontend files rebuild
lua5.5 hhc.lua install
lua5.5 hhc.lua test
```

Useful options: `--ref=master` (GCC trunk), `--ref=releases/gcc-15.2.0`,
`--prefix=/opt/hhc`, `--jobs=8`, `--with-cxx`, `--bootstrap`, `--ccache`.
`lua5.5 hhc.lua help` lists everything.

A full build needs roughly 1-2 hours on 4 cores and ~10 GB of disk.

### CI

`.github/workflows/build.yml` does the same on GitHub Actions for x86_64 and
aarch64: it resolves the newest stable GCC, builds it with the frontend,
runs `tests/cases`, uploads the tarballs and (on `v*` tags) publishes a
release. A weekly schedule picks up new GCC releases on its own.

## Repository layout

```
hhc.lua                      build driver entry point (Lua 5.5)
build/
  config.lua                 defaults (GCC ref, languages, prefix, ...)
  lib/                       log, sh, args, gccver, state
  steps/                     doctor deps fetch overlay configure compile
                             install test package clean
frontend/
  gcc/hsharp/                the GCC frontend  ->  copied to <gcc>/gcc/hsharp
  patches/                   optional patches for core GCC files, per release
scripts/bootstrap-lua.sh     get lua5.5 (apt, lua.org, or GitHub mirror)
tests/
  cases/                     *.h# programs + .expected / .expected_error
  selftest.lua               unit tests of the build scripts
docs/                        architecture and language-subset notes
.github/workflows/build.yml  CI
```

## Status: what the frontend supports today

hhc is at **stage 0**: a real, working GCC frontend covering a core subset of
H#, designed to grow. Supported: functions (incl. `fn f() = expr`, recursion),
`let` / `let mut`, all integer types, `f32`/`f64`, `bool`, `string` literals,
arithmetic / comparison / logical operators, `if`-`elsif`-`else`, `while`,
`loop`, `for i in a..b` / `a..=b`, `break`, `continue`, `return`, `write()`,
`to_string()`, `{name}` interpolation inside `write` strings.

Not yet: structs, enums, traits, generics, closures, modules/`use`, arrays,
`match`, FFI, async, runtime string concatenation outside `write`. See
[docs/LANGUAGE-SUBSET.md](docs/LANGUAGE-SUBSET.md) and the roadmap in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## License

BSD 3-Clause, see [LICENSE](LICENSE). The frontend is built against and linked
into GCC, which is GPLv3+; binary distributions of `hhc` therefore follow GCC's
licensing.
