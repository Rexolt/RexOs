/* =============================================================================
 *  RexOS - Hálózati end-to-end önteszt (headless)
 *
 *  Cél: QEMU-ban automatizáltan ellenőrizhető legyen a teljes út:
 *  DHCP -> DNS -> TCP -> HTTP, majd TLS handshake -> HTTPS.
 *  A kimenet a soros konzolra megy (write(1, ...) -> kprintf).
 *
 *  Csak akkor kerül az initrd-be, ha a build NETTEST=1-gyel fut
 *  (lásd Makefile); a kmain automatikusan elindítja, ha megtalálja.
 * ========================================================================== */
#include "libc.h"
#include "http.h"

static void say(const char *s) {
    uint64_t n = 0;
    while (s[n]) n++;
    write(1, s, n);
}

static void say_int(int v) {
    char buf[16];
    int i = 0;
    if (v < 0) { say("-"); v = -v; }
    if (v == 0) { say("0"); return; }
    while (v > 0 && i < 15) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    char rev[16];
    for (int j = 0; j < i; j++) rev[j] = buf[i - 1 - j];
    rev[i] = 0;
    say(rev);
}

static int fetch(const char *url) {
    static char body[8192];
    http_response_t resp;

    say("[nettest] GET "); say(url); say("\n");
    int rc = http_get(url, body, sizeof(body), &resp);
    say("[nettest] rc="); say_int(rc);
    say(" status="); say_int(resp.status_code);
    say(" body_len="); say_int(resp.body_len);
    say(" ("); say(resp.status); say(")\n");

    if (rc == HTTP_OK && resp.status_code == 200 && resp.body_len > 0) {
        say("[nettest] PASS "); say(url); say("\n");
        return 1;
    }
    say("[nettest] FAIL "); say(url); say("\n");
    return 0;
}

/* Nagy átvitel: az rx_buf-nál (32 KB) jóval nagyobb letöltés kényszeríti ki
 * az ablak-kezelést (zero window -> window update -> folytatás). A QEMU
 * gazdagépen futó teszt-szerverről tölt (10.0.2.2). Ha a szerver nem fut,
 * SKIP - nem számít bele az eredménybe. */
static int fetch_big(const char *url, int expect_len, int *skipped) {
    char *body = 0;
    http_response_t resp;

    *skipped = 0;
    say("[nettest] GET (big) "); say(url); say("\n");
    int rc = http_get_alloc(url, &body, 0, &resp);
    if (rc == HTTP_ERR_CONNECT) {
        say("[nettest] SKIP (no local test server)\n");
        *skipped = 1;
        return 0;
    }
    say("[nettest] rc="); say_int(rc);
    say(" status="); say_int(resp.status_code);
    say(" body_len="); say_int(resp.body_len);
    say(" truncated="); say_int(resp.truncated); say("\n");

    int ok = (rc == HTTP_OK && resp.status_code == 200 &&
              resp.body_len == expect_len && !resp.truncated);
    if (ok && body) {
        /* Tartalom-ellenőrzés: a teszt-fájl csupa 'R' byte. */
        for (int i = 0; i < expect_len; i++) {
            if (body[i] != 'R') { ok = 0; say("[nettest] content mismatch at "); say_int(i); say("\n"); break; }
        }
    }
    if (body) free(body);
    say(ok ? "[nettest] PASS big transfer\n" : "[nettest] FAIL big transfer\n");
    return ok;
}

void _start(void) {
    say("[nettest] ==== RexOS network self-test ====\n");

    int pass = 0, total = 0;

    total++; pass += fetch("http://example.com/");
    total++; pass += fetch("https://example.com/");

    int skipped = 0;
    int big = fetch_big("http://10.0.2.2:18080/big.bin", 120000, &skipped);
    if (!skipped) { total++; pass += big; }

    say("[nettest] result: ");
    say_int(pass); say("/"); say_int(total); say(" passed\n");
    say(pass == total ? "[nettest] ALL TESTS PASSED\n"
                      : "[nettest] SOME TESTS FAILED\n");
    exit(pass == total ? 0 : 1);
}
