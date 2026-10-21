#!/bin/sh
# scripts/bootstrap-lua.sh -- make `lua5.5` available (used by CI and by hand).
#
# Order of attempts:
#   1. an existing lua5.5 on PATH
#   2. the distro package (apt: lua5.5)
#   3. the official tarball from lua.org
#   4. the official GitHub mirror (compiled as a single translation unit)
#
# Installs to $LUA_PREFIX/bin (default: $HOME/.local) when it has to build.
set -eu

LUA_VERSION="${LUA_VERSION:-5.5.1}"
LUA_PREFIX="${LUA_PREFIX:-$HOME/.local}"

if command -v lua5.5 >/dev/null 2>&1; then
    echo "lua5.5 already present: $(lua5.5 -v)"
    exit 0
fi

SUDO=""
[ "$(id -u)" = 0 ] || SUDO="sudo"

if command -v apt-get >/dev/null 2>&1; then
    if $SUDO apt-get install -y --no-install-recommends lua5.5 >/dev/null 2>&1; then
        echo "installed lua5.5 from apt: $(lua5.5 -v)"
        exit 0
    fi
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$LUA_PREFIX/bin"

# NOTE: these run inside `if`, where `set -e` is disabled, so every step
# must report failure explicitly with `|| return 1`.
build_tarball() {
    cd "$WORK" || return 1
    curl -fsSL "https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz" -o lua.tgz || return 1
    tar xzf lua.tgz || return 1
    cd "lua-$LUA_VERSION" || return 1
    make -j"$(nproc 2>/dev/null || echo 2)" linux >/dev/null || return 1
    install -m 755 src/lua "$LUA_PREFIX/bin/lua5.5" || return 1
}

build_github() {
    cd "$WORK"
    git clone --depth 1 --branch "v$LUA_VERSION" https://github.com/lua/lua.git lua-src
    cd lua-src
    cc -O2 -DMAKE_LUA -DLUA_USE_LINUX -o lua onelua.c -lm -ldl
    install -m 755 lua "$LUA_PREFIX/bin/lua5.5"
}

if build_tarball; then
    :
else
    echo "lua.org tarball unavailable, falling back to the GitHub mirror"
    build_github
fi

echo "installed $("$LUA_PREFIX/bin/lua5.5" -v) -> $LUA_PREFIX/bin/lua5.5"
case ":$PATH:" in
    *":$LUA_PREFIX/bin:"*) ;;
    *) echo "add $LUA_PREFIX/bin to PATH" ;;
esac
