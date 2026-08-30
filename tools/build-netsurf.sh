#!/usr/bin/env bash
# =============================================================================
#  NetSurf böngésző keresztfordítása RexOS-re (Fázis 3)
#
#  Előfeltétel: tools/build-toolchain.sh és tools/build-ports.sh lefutott.
#  A netsurf-all forráscsomagot fordítja TARGET=framebuffer módban a
#  x86_64-rexos-gcc wrapperrel (=x86_64-elf-gcc --specs=rexos.specs).
#
#  Kimenet: toolchain/ports-src/netsurf-all-3.11/inst-framebuffer/ (libek)
#           + a netsurf bináris a netsurf/ alkönyvtárban.
# =============================================================================
set -euo pipefail

NSVER=3.11

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$REPO_ROOT/toolchain"
TPREFIX="$PREFIX/x86_64-elf"
SRC="$PREFIX/ports-src"
NSDIR="$SRC/netsurf-all-$NSVER"
JOBS="$(nproc)"

export PATH="$PREFIX/bin:$PATH"

# Csak a cross .pc fájlokat lássa a pkg-config, a hostét soha (különben a
# gazdagép curl/openssl/sdl csomagjait találná meg).
export PKG_CONFIG_LIBDIR="$TPREFIX/lib/pkgconfig"

if [[ ! -d "$NSDIR" ]]; then
    echo "HIBA: $NSDIR hiányzik (netsurf-all-$NSVER forrás)" >&2
    exit 1
fi

# -----------------------------------------------------------------------------
# nsgenbind HOST tool: a build gépen fut (Duktape binding generátor), ezért
# a HOST fordítóval kell buildelni, NEM a cross-szal. A netsurf-all Makefile
# a PATH-ról hívja "nsgenbind" néven; a toolchain/bin előrébb van a PATH-ban,
# mint az inst-*/bin, így a mi host binárisunk nyer.
# -----------------------------------------------------------------------------
if [[ ! -x "$PREFIX/bin/nsgenbind" ]]; then
    echo "[netsurf] nsgenbind host build..."
    make -C "$NSDIR/nsgenbind" \
        CC=cc HOST= NSSHARED="$NSDIR/buildsystem" Q=@ >/dev/null
    cp "$NSDIR/nsgenbind"/build-*-release-binary/nsgenbind "$PREFIX/bin/nsgenbind"
fi

# -----------------------------------------------------------------------------
# Forráspatch-ek (idempotensek, marker kommenttel jelölve)
# -----------------------------------------------------------------------------
FETCH_C="$NSDIR/netsurf/content/fetch.c"
if ! grep -q 'rexos-curl-guard' "$FETCH_C"; then
    echo "[netsurf] patch: fetch.c curl include guard"
    python3 - "$FETCH_C" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
s = s.replace('#include "content/fetchers/curl.h"',
              '#ifdef WITH_CURL /* rexos-curl-guard */\n'
              '#include "content/fetchers/curl.h"\n'
              '#endif')
open(p, 'w').write(s)
PYEOF
fi

# RexOS http/https fetcher + CA bundle bemásolása és regisztrálása
cp "$REPO_ROOT/user/netsurf/fetch_rexos.c" "$NSDIR/netsurf/content/fetchers/rexos.c"
cp "$REPO_ROOT/user/ca_bundle.c"           "$NSDIR/netsurf/content/fetchers/rexos_ca.c"
FETCHERS_MK="$NSDIR/netsurf/content/fetchers/Makefile"
if ! grep -q 'rexos.c' "$FETCHERS_MK"; then
    echo "[netsurf] patch: fetchers Makefile += rexos.c rexos_ca.c"
    sed -i 's/^S_FETCHERS_YES := data.c resource.c/S_FETCHERS_YES := data.c resource.c rexos.c rexos_ca.c/' "$FETCHERS_MK"
fi
if ! grep -q 'rexos-fetcher-reg' "$NSDIR/netsurf/content/fetch.c"; then
    echo "[netsurf] patch: fetch.c fetcher_init += fetch_rexos_register"
    python3 - "$NSDIR/netsurf/content/fetch.c" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
s = s.replace('#ifdef WITH_CURL /* rexos-curl-guard */',
              'nserror fetch_rexos_register(void); /* rexos-fetcher-reg */\n'
              '#ifdef WITH_CURL /* rexos-curl-guard */')
s = s.replace('''nserror fetcher_init(void)
{
	nserror ret;

#ifdef WITH_CURL''',
              '''nserror fetcher_init(void)
{
	nserror ret;

	ret = fetch_rexos_register(); /* rexos http/https fetcher */
	if (ret != NSERROR_OK) {
		return ret;
	}

#ifdef WITH_CURL''')
open(p, 'w').write(s)
PYEOF
fi
# libdom: a class-gyorsítótár frissítése attribútum-értékváltozáskor (enélkül
# a JavaScriptből végzett osztálycsere nem hat a stílusokra).
python3 "$REPO_ROOT/tools/patch-libdom-class.py" "$NSDIR/libdom"

# DOM-változás -> dobozfa újraépítés (a NetSurf 3.11 ezt nem tudja; enélkül a
# JavaScript minden DOM-módosítása láthatatlan marad a képernyőn).
cp "$REPO_ROOT/user/netsurf/html_relayout_rexos.c" \
   "$NSDIR/netsurf/content/handlers/html/relayout_rexos.c"
cp "$REPO_ROOT/user/netsurf/html_relayout_rexos.h" \
   "$NSDIR/netsurf/content/handlers/html/relayout_rexos.h"
python3 "$REPO_ROOT/tools/patch-html-relayout.py" "$NSDIR/netsurf"

# A framebuffer frontend path-plottere az eredetiben csonk ("path
# unimplemented") -> egyetlen SVG sem jelenik meg. Kicseréljük a sajátunkra.
cp "$REPO_ROOT/user/netsurf/fb_plot_path.c" \
   "$NSDIR/netsurf/frontends/framebuffer/fb_plot_path.c"
FB_C="$NSDIR/netsurf/frontends/framebuffer/framebuffer.c"
if ! grep -q 'rexos-plot-path' "$FB_C"; then
    echo "[netsurf] patch: framebuffer.c path plotter -> fb_plot_path.c"
    python3 "$REPO_ROOT/tools/patch-fb-path.py" "$FB_C"
fi

FB_MK="$NSDIR/netsurf/frontends/framebuffer/Makefile"
if ! grep -q 'lbearssl' "$FB_MK"; then
    echo "[netsurf] patch: framebuffer Makefile += -lbearssl"
    echo 'LDFLAGS += -lbearssl' >> "$FB_MK"
fi

# RexOS libnsfb surface bemásolása + regisztrálása a surface Makefile-ba
cp "$REPO_ROOT/user/netsurf/nsfb_surface_rexos.c" "$NSDIR/libnsfb/src/surface/rexos.c"
SURF_MK="$NSDIR/libnsfb/src/surface/Makefile"
if ! grep -q 'rexos.c' "$SURF_MK"; then
    echo "[netsurf] patch: libnsfb surface Makefile += rexos.c"
    sed -i 's/^SURFACE_HANDLER_yes := surface.c ram.c/SURFACE_HANDLER_yes := surface.c ram.c rexos.c/' "$SURF_MK"
fi

# libparserutils: iconv filter nélkül (belső kodekekkel) épüljön
# PATH_MAX: a newlib limits.h nem definiálja
export CFLAGS="-DWITHOUT_ICONV_FILTER -DPATH_MAX=1024"

cd "$NSDIR"
# A netsurf-all egy build-stamp fájllal jelzi, hogy kész; enélkül a "make build"
# akkor sem csinál semmit, ha a konfiguráció (pl. NETSURF_USE_WEBP) változott.
# Az alatta futó sub-make-ek amúgy is inkrementálisak, így ez olcsó.
rm -f "$NSDIR/inst-framebuffer/build-stamp"
make -j"$JOBS" \
    TARGET=framebuffer \
    HOST=x86_64-rexos \
    CC=x86_64-rexos-gcc \
    NETSURF_USE_CURL=NO \
    NETSURF_USE_OPENSSL=NO \
    NETSURF_USE_DUKTAPE=YES \
    NETSURF_USE_JPEG=YES \
    NETSURF_USE_PNG=YES \
    NETSURF_USE_WEBP=YES \
    NETSURF_USE_JPEGXL=NO \
    NETSURF_USE_VIDEO=NO \
    NETSURF_FB_FONTLIB=internal \
    NETSURF_FB_RESPATH=/netsurf \
    "$@"

echo "[netsurf] KÉSZ"
