#!/usr/bin/env bash
# =============================================================================
#  RexOS userspace könyvtárportok (Fázis 2)
#
#  zlib, libpng, libjpeg-turbo, freetype keresztfordítása az x86_64-elf
#  toolchainnel (tools/build-toolchain.sh után futtatható). A statikus libek
#  és headerek a toolchain/x86_64-elf/ prefixbe kerülnek, így a cross gcc
#  automatikusan megtalálja őket (-lz, -lpng16, -ljpeg, -lfreetype).
#
#  Idempotens: stamp fájlok jelzik a kész portokat.
# =============================================================================
set -euo pipefail

ZLIB_VER=1.3.1
LIBPNG_VER=1.6.43
LIBJPEG_VER=3.0.3
FREETYPE_VER=2.13.2
LIBWEBP_VER=1.4.0

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$REPO_ROOT/toolchain"
TARGET=x86_64-elf
TPREFIX="$PREFIX/$TARGET"       # ide települnek a portok
SRC="$PREFIX/ports-src"
JOBS="$(nproc)"

export PATH="$PREFIX/bin:$PATH"

CROSS_CC="$TARGET-gcc"
CROSS_AR="$TARGET-ar"
CROSS_RANLIB="$TARGET-ranlib"

if ! command -v "$CROSS_CC" >/dev/null; then
    echo "HIBA: $CROSS_CC nem található - előbb futtasd: tools/build-toolchain.sh" >&2
    exit 1
fi

mkdir -p "$SRC"
log() { echo "[ports] $*"; }

# -----------------------------------------------------------------------------
# RexOS runtime a toolchain prefixbe: crt0 + syscall glue + spec fájl.
# Ettől a "x86_64-elf-gcc --specs=rexos.specs" önállóan tud futtatható RexOS
# programot linkelni - az autoconf "C compiler works" tesztjéhez ez kell.
# Mindig újraépítjük (olcsó), így a glue változásai azonnal átjönnek.
# -----------------------------------------------------------------------------
log "RexOS runtime (crt0 + librexos + rexos.specs) telepítése a prefixbe..."
"$CROSS_CC" -c "$REPO_ROOT/user/newlib-glue/crt0.S" -o "$TPREFIX/lib/crt0.o"
# headerek előbb: a glue forrásai már ezekre épülnek
cp "$REPO_ROOT/user/newlib-glue/iconv.h"        "$TPREFIX/include/iconv.h"
cp "$REPO_ROOT/user/newlib-glue/endian.h"       "$TPREFIX/include/endian.h"
cp "$REPO_ROOT/user/newlib-glue/netdb.h"        "$TPREFIX/include/netdb.h"
mkdir -p "$TPREFIX/include/sys" "$TPREFIX/include/netinet" "$TPREFIX/include/arpa"
cp "$REPO_ROOT/user/newlib-glue/sys_dirent.h"   "$TPREFIX/include/sys/dirent.h"
cp "$REPO_ROOT/user/newlib-glue/sys_socket.h"   "$TPREFIX/include/sys/socket.h"
cp "$REPO_ROOT/user/newlib-glue/sys_mman.h"     "$TPREFIX/include/sys/mman.h"
cp "$REPO_ROOT/user/newlib-glue/sys_utsname.h"  "$TPREFIX/include/sys/utsname.h"
cp "$REPO_ROOT/user/newlib-glue/netinet_in.h"   "$TPREFIX/include/netinet/in.h"
cp "$REPO_ROOT/user/newlib-glue/arpa_inet.h"    "$TPREFIX/include/arpa/inet.h"
GLUE_OBJS=()
for src in syscalls dirent iconv_mini rexos_sock posix_extra; do
    "$CROSS_CC" -O2 -c "$REPO_ROOT/user/newlib-glue/$src.c" -o "$SRC/$src.o" \
        -I"$REPO_ROOT/user/newlib-glue"
    GLUE_OBJS+=("$SRC/$src.o")
done
rm -f "$TPREFIX/lib/librexos.a"
"$CROSS_AR" rcs "$TPREFIX/lib/librexos.a" "${GLUE_OBJS[@]}"
cat > "$TPREFIX/lib/rexos.specs" <<'EOF'
%rename link old_link

*startfile:
crt0%O%s

*lib:
--start-group -lc -lrexos -lm -lgcc --end-group
EOF

# A portok configure tesztjeihez: így a sima "CC conftest.c" link is működik
REXOS_CC="$CROSS_CC --specs=rexos.specs"

fetch() {
    local url="$1" out="$SRC/$(basename "$1")"
    if [[ ! -f "$out" ]]; then
        log "letöltés: $url"
        wget -q -O "$out.part" "$url"
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
# zlib
# -----------------------------------------------------------------------------
if [[ ! -f "$PREFIX/.stamp-zlib" ]]; then
    fetch "https://github.com/madler/zlib/releases/download/v$ZLIB_VER/zlib-$ZLIB_VER.tar.gz"
    extract "zlib-$ZLIB_VER.tar.gz" "zlib-$ZLIB_VER"
    log "zlib $ZLIB_VER build..."
    pushd "$SRC/zlib-$ZLIB_VER" >/dev/null
    make -f Makefile distclean >/dev/null 2>&1 || true
    CC="$CROSS_CC" AR="$CROSS_AR" RANLIB="$CROSS_RANLIB" CFLAGS="-O2" \
        ./configure --static --prefix="$TPREFIX"
    make -j"$JOBS" libz.a
    make install
    popd >/dev/null
    touch "$PREFIX/.stamp-zlib"
fi

# -----------------------------------------------------------------------------
# libpng
# -----------------------------------------------------------------------------
if [[ ! -f "$PREFIX/.stamp-libpng" ]]; then
    fetch "https://download.sourceforge.net/libpng/libpng-$LIBPNG_VER.tar.gz"
    extract "libpng-$LIBPNG_VER.tar.gz" "libpng-$LIBPNG_VER"
    log "libpng $LIBPNG_VER build..."
    rm -rf "$SRC/build-libpng"
    mkdir -p "$SRC/build-libpng"
    pushd "$SRC/build-libpng" >/dev/null
    CC="$REXOS_CC" AR="$CROSS_AR" RANLIB="$CROSS_RANLIB" \
    CFLAGS="-O2" CPPFLAGS="-I$TPREFIX/include" LDFLAGS="-L$TPREFIX/lib" \
        "../libpng-$LIBPNG_VER/configure" \
        --host="$TARGET" --prefix="$TPREFIX" \
        --disable-shared --enable-static \
        --disable-tools
    make -j"$JOBS"
    make install
    popd >/dev/null
    touch "$PREFIX/.stamp-libpng"
fi

# -----------------------------------------------------------------------------
# libjpeg-turbo (SIMD nélkül, sima C út - nincs szükség nasm-ra)
# -----------------------------------------------------------------------------
if [[ ! -f "$PREFIX/.stamp-libjpeg" ]]; then
    fetch "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$LIBJPEG_VER/libjpeg-turbo-$LIBJPEG_VER.tar.gz"
    extract "libjpeg-turbo-$LIBJPEG_VER.tar.gz" "libjpeg-turbo-$LIBJPEG_VER"
    log "libjpeg-turbo $LIBJPEG_VER build..."
    cat > "$SRC/rexos-toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $PREFIX/bin/$CROSS_CC)
set(CMAKE_AR $PREFIX/bin/$CROSS_AR)
set(CMAKE_RANLIB $PREFIX/bin/$CROSS_RANLIB)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH $TPREFIX)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
    rm -rf "$SRC/build-libjpeg"
    cmake -S "$SRC/libjpeg-turbo-$LIBJPEG_VER" -B "$SRC/build-libjpeg" \
        -DCMAKE_TOOLCHAIN_FILE="$SRC/rexos-toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$TPREFIX" \
        -DENABLE_SHARED=0 -DENABLE_STATIC=1 \
        -DWITH_SIMD=0 -DWITH_TURBOJPEG=0 \
        >/dev/null
    cmake --build "$SRC/build-libjpeg" -j"$JOBS" --target jpeg-static
    # csak a lib + headerek kellenek; a cmake install a teszt binárisokat is
    # akarná, ezért kézzel telepítünk
    cp "$SRC/build-libjpeg/libjpeg.a" "$TPREFIX/lib/"
    cp "$SRC/build-libjpeg/jconfig.h" "$SRC/libjpeg-turbo-$LIBJPEG_VER"/{jpeglib.h,jmorecfg.h,jerror.h} "$TPREFIX/include/"
    touch "$PREFIX/.stamp-libjpeg"
fi

# -----------------------------------------------------------------------------
# freetype (zlib-bel, png/harfbuzz/brotli nélkül)
# -----------------------------------------------------------------------------
if [[ ! -f "$PREFIX/.stamp-freetype" ]]; then
    fetch "https://download.savannah.gnu.org/releases/freetype/freetype-$FREETYPE_VER.tar.xz"
    extract "freetype-$FREETYPE_VER.tar.xz" "freetype-$FREETYPE_VER"
    log "freetype $FREETYPE_VER build..."
    rm -rf "$SRC/build-freetype"
    mkdir -p "$SRC/build-freetype"
    pushd "$SRC/build-freetype" >/dev/null
    CC="$REXOS_CC" AR="$CROSS_AR" RANLIB="$CROSS_RANLIB" \
    CFLAGS="-O2" CPPFLAGS="-I$TPREFIX/include" LDFLAGS="-L$TPREFIX/lib" \
        "../freetype-$FREETYPE_VER/configure" \
        --host="$TARGET" --prefix="$TPREFIX" \
        --disable-shared --enable-static \
        --with-zlib=yes --with-png=no --with-harfbuzz=no \
        --with-brotli=no --with-bzip2=no
    make -j"$JOBS"
    make install
    popd >/dev/null
    touch "$PREFIX/.stamp-freetype"
fi

# -----------------------------------------------------------------------------
# expat (libdom XML binding)
# -----------------------------------------------------------------------------
EXPAT_VER=2.6.2
EXPAT_TAG="R_$(echo "$EXPAT_VER" | tr . _)"
if [[ ! -f "$PREFIX/.stamp-expat" ]]; then
    fetch "https://github.com/libexpat/libexpat/releases/download/$EXPAT_TAG/expat-$EXPAT_VER.tar.gz"
    extract "expat-$EXPAT_VER.tar.gz" "expat-$EXPAT_VER"
    log "expat $EXPAT_VER build..."
    rm -rf "$SRC/build-expat"
    mkdir -p "$SRC/build-expat"
    pushd "$SRC/build-expat" >/dev/null
    CC="$REXOS_CC" AR="$CROSS_AR" RANLIB="$CROSS_RANLIB" CFLAGS="-O2" \
        "../expat-$EXPAT_VER/configure" \
        --host="$TARGET" --prefix="$TPREFIX" \
        --disable-shared --enable-static \
        --without-xmlwf --without-examples --without-tests --without-docbook
    make -j"$JOBS"
    make install
    popd >/dev/null
    touch "$PREFIX/.stamp-expat"
fi

# -----------------------------------------------------------------------------
# libwebp (WebP képdekóder a NetSurfhöz)
#
# Csak a dekóder kell; a kódolót és az összes eszközt/formátum-bővítményt
# kikapcsoljuk, hogy ne kelljen se libpng-tool, se SDL, se szálkezelés.
# A NetSurf a "libwebp" pkg-config modult keresi (webp/decode.h).
# -----------------------------------------------------------------------------
if [[ ! -f "$PREFIX/.stamp-libwebp" ]]; then
    fetch "https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-$LIBWEBP_VER.tar.gz"
    extract "libwebp-$LIBWEBP_VER.tar.gz" "libwebp-$LIBWEBP_VER"
    log "libwebp $LIBWEBP_VER build..."
    rm -rf "$SRC/build-libwebp"
    mkdir -p "$SRC/build-libwebp"
    pushd "$SRC/build-libwebp" >/dev/null
    CC="$REXOS_CC" AR="$CROSS_AR" RANLIB="$CROSS_RANLIB" CFLAGS="-O2" \
        "../libwebp-$LIBWEBP_VER/configure" \
        --host="$TARGET" --prefix="$TPREFIX" \
        --disable-shared --enable-static \
        --disable-libwebpmux --disable-libwebpdemux --disable-libwebpextras \
        --disable-libwebpdecoder --disable-threading \
        --disable-png --disable-jpeg --disable-tiff --disable-gif \
        --disable-wic --disable-sdl --disable-gl
    make -j"$JOBS"
    make install
    popd >/dev/null
    touch "$PREFIX/.stamp-libwebp"
fi

# -----------------------------------------------------------------------------
# BearSSL (a repo third_party/bearssl fájából, TLS a NetSurf fetcherhez)
# -----------------------------------------------------------------------------
if [[ ! -f "$PREFIX/.stamp-bearssl" ]]; then
    log "BearSSL build (third_party/bearssl)..."
    BSSL="$REPO_ROOT/third_party/bearssl"
    BSSL_BUILD="$SRC/build-bearssl"
    rm -rf "$BSSL_BUILD"
    mkdir -p "$BSSL_BUILD"
    BSSL_OBJS=()
    while IFS= read -r -d '' f; do
        obj="$BSSL_BUILD/$(echo "${f#"$BSSL/src/"}" | tr / _).o"
        "$CROSS_CC" -O2 -c "$f" -o "$obj" \
            -I"$BSSL/inc" -I"$BSSL/src" \
            -DBR_64=1 -DBR_INT128=1 -DBR_LE_UNALIGNED=1 \
            -DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0 -DBR_RDRAND=0
        BSSL_OBJS+=("$obj")
    done < <(find "$BSSL/src" -name '*.c' -print0)
    "$CROSS_AR" rcs "$TPREFIX/lib/libbearssl.a" "${BSSL_OBJS[@]}"
    cp "$BSSL/inc/"*.h "$TPREFIX/include/"
    touch "$PREFIX/.stamp-bearssl"
fi

log "KÉSZ. Telepített libek:"
ls -1 "$TPREFIX/lib/" | grep -E '\.(a)$' | sed 's/^/  /'
