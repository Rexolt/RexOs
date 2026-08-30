#!/usr/bin/env bash
# =============================================================================
#  RexOS cross-toolchain build
#
#  binutils + gcc (x86_64-elf target) + newlib libc a toolchain/ prefixbe.
#  Ezzel fordulnak a POSIX-közeli userspace portok (zlib, libpng, freetype,
#  NetSurf, ...). A RexOS-specifikus syscall glue a user/newlib-glue/-ban él,
#  azt a fő Makefile fordítja és linkeli az appokhoz.
#
#  Használat:  tools/build-toolchain.sh
#  Idempotens: a kész fázisokat marker fájlok jelzik, újrafuttatáskor átugorja.
# =============================================================================
set -euo pipefail

BINUTILS_VER=2.43.1
GCC_VER=14.2.0
NEWLIB_VER=4.4.0.20231231

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$REPO_ROOT/toolchain"
TARGET=x86_64-elf
SRC="$PREFIX/src"
JOBS="$(nproc)"

export PATH="$PREFIX/bin:$PATH"

mkdir -p "$SRC"

log() { echo "[toolchain] $*"; }

fetch() {
    local url="$1" out="$SRC/$(basename "$1")"
    if [[ ! -f "$out" ]]; then
        log "letöltés: $url"
        wget -q --show-progress -O "$out.part" "$url"
        mv "$out.part" "$out"
    fi
}

extract() {
    local tarball="$SRC/$1" dir="$SRC/$2"
    if [[ ! -d "$dir" ]]; then
        log "kibontás: $1"
        tar -xf "$tarball" -C "$SRC"
    fi
}

# -----------------------------------------------------------------------------
# 1. Források
# -----------------------------------------------------------------------------
fetch "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz"
fetch "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz"
fetch "https://sourceware.org/pub/newlib/newlib-$NEWLIB_VER.tar.gz"

extract "binutils-$BINUTILS_VER.tar.xz" "binutils-$BINUTILS_VER"
extract "gcc-$GCC_VER.tar.xz"           "gcc-$GCC_VER"
extract "newlib-$NEWLIB_VER.tar.gz"     "newlib-$NEWLIB_VER"

# -----------------------------------------------------------------------------
# 2. binutils
# -----------------------------------------------------------------------------
if [[ ! -f "$PREFIX/.stamp-binutils" ]]; then
    log "binutils $BINUTILS_VER build..."
    rm -rf "$SRC/build-binutils"
    mkdir -p "$SRC/build-binutils"
    pushd "$SRC/build-binutils" >/dev/null
    "../binutils-$BINUTILS_VER/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --with-sysroot --disable-nls --disable-werror
    make -j"$JOBS"
    make install
    popd >/dev/null
    touch "$PREFIX/.stamp-binutils"
fi

# -----------------------------------------------------------------------------
# 3. gcc (C fordító + libgcc; newlib-hez konfigurálva)
# -----------------------------------------------------------------------------
if [[ ! -f "$PREFIX/.stamp-gcc" ]]; then
    log "gcc $GCC_VER build..."
    rm -rf "$SRC/build-gcc"
    mkdir -p "$SRC/build-gcc"
    pushd "$SRC/build-gcc" >/dev/null
    # A friss host fordítók (gcc >= 15) alapból C++20-szal fordítanak, amin a
    # gcc-14 libcody-ja elhasal (u8"" -> char8_t). gnu++17-re rögzítjük.
    CXX="g++ -std=gnu++17" \
    "../gcc-$GCC_VER/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --disable-nls --enable-languages=c \
        --without-headers --with-newlib \
        --disable-shared --disable-threads \
        --disable-multilib \
        --disable-libssp --disable-libquadmath --disable-libgomp
    make -j"$JOBS" all-gcc all-target-libgcc
    make install-gcc install-target-libgcc
    popd >/dev/null
    touch "$PREFIX/.stamp-gcc"
fi

# -----------------------------------------------------------------------------
# 4. newlib (a frissen épült cross gcc-vel)
# -----------------------------------------------------------------------------
if [[ ! -f "$PREFIX/.stamp-newlib" ]]; then
    log "newlib $NEWLIB_VER build..."
    rm -rf "$SRC/build-newlib"
    mkdir -p "$SRC/build-newlib"
    pushd "$SRC/build-newlib" >/dev/null
    "../newlib-$NEWLIB_VER/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --disable-multilib \
        --disable-newlib-supplied-syscalls \
        --enable-newlib-io-long-long \
        --enable-newlib-io-c99-formats
    make -j"$JOBS"
    make install
    popd >/dev/null
    touch "$PREFIX/.stamp-newlib"
fi

log "KÉSZ. Fordító: $PREFIX/bin/$TARGET-gcc"
"$PREFIX/bin/$TARGET-gcc" --version | head -1
