#!/usr/bin/env bash
# =============================================================================
#  RexOS - CA bundle generátor (Mozilla CCADB -> BearSSL trust anchors)
#
#  Mit csinál:
#   1. Letölti a Mozilla CA bundle-t (curl.se mirror, naponta frissül).
#   2. Lefordítja a BearSSL upstream Makefile-jával a `brssl` host tool-t.
#   3. brssl ta paranccsal C-forrássá konvertálja a TA-kat.
#   4. Az eredményt `user/ca_bundle.c`-be írja: extern br_x509_trust_anchor TAs[]
#      és const size_t TAs_NUM.
#
#  Futtatás:
#      ./tools/gen_ca_bundle.sh
#
#  Függőségek: curl, make, gcc (host), BearSSL forrás (third_party/bearssl/).
#  Internet kell hozzá. Idempotens - már meglevő letöltést újrahasznál.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BEARSSL="$ROOT/third_party/bearssl"
OUT_BUNDLE="$ROOT/user/ca_bundle.c"
CACHE_DIR="$ROOT/build/ca_cache"
CACERT_PEM="$CACHE_DIR/cacert.pem"
# Az upstream Makefile a host build artifact-okat a `build/` alá teszi
# (kernel águnktól független, ők azt mappát újrahasznosítják). A `tools`
# target állítja elő a brssl bináris-t.
BRSSL_BIN="$BEARSSL/build/brssl"

mkdir -p "$CACHE_DIR"

# 1) Mozilla CA bundle letöltés (ha még nincs)
if [[ ! -f "$CACERT_PEM" ]]; then
    echo "==> cacert.pem letöltése (curl.se mirror)"
    curl -fL -o "$CACERT_PEM" "https://curl.se/ca/cacert.pem"
fi
echo "==> cacert.pem méret: $(wc -c < "$CACERT_PEM") byte"

# 2) brssl host tool build (a BearSSL saját Makefile-jával, normál host gcc-vel)
if [[ ! -x "$BRSSL_BIN" ]]; then
    echo "==> brssl host tool build (BearSSL upstream Makefile, 'tools' target)"
    ( cd "$BEARSSL" && make -s tools )
fi
if [[ ! -x "$BRSSL_BIN" ]]; then
    echo "HIBA: $BRSSL_BIN nem készült el" >&2
    exit 1
fi

# 3) TA-k generálása C-be
echo "==> brssl ta -> $OUT_BUNDLE"
# brssl a végére kibocsát egy "#define TAs_NUM N"-t. A tls.c viszont externel
# kell legyen rá hivatkozni, ezért a #define-ot const size_t-vé konvertáljuk.
TMP_BUNDLE="$(mktemp)"
trap 'rm -f "$TMP_BUNDLE"' EXIT
{
    cat <<'HDR'
/* =============================================================================
 *  RexOS - CA trust anchor bundle (auto-generated)
 *  Forrás: Mozilla CCADB (curl.se cacert.pem)
 *  NE szerkeszd kézzel - használd a tools/gen_ca_bundle.sh-t.
 * ========================================================================== */
#include "bearssl.h"
#include <stddef.h>

HDR
    "$BRSSL_BIN" ta "$CACERT_PEM"
} > "$TMP_BUNDLE"

# Két dolgot kell igazítani brssl kimenetén:
#  1) `#define TAs_NUM   121` -> `const size_t TAs_NUM = 121;` (mert externel jön a tls.c-be)
#  2) `static const br_x509_trust_anchor TAs[...]` -> `const br_x509_trust_anchor TAs[...]`
#     (a static megakadályozná a külső linkelést)
sed -E \
    -e 's|^#define[[:space:]]+TAs_NUM[[:space:]]+([0-9]+)[[:space:]]*$|const size_t TAs_NUM = \1;|' \
    -e 's|^static[[:space:]]+const[[:space:]]+br_x509_trust_anchor[[:space:]]+TAs\[|const br_x509_trust_anchor TAs[|' \
    "$TMP_BUNDLE" > "$OUT_BUNDLE"

echo "==> Kész. TA-k száma:"
grep -c '^\s*{' "$OUT_BUNDLE" || true
echo "==> $OUT_BUNDLE"
