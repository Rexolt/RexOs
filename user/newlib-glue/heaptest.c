/* ============================================================================
 *  Rex OS - heap / brk / TLB regressziós teszt
 *
 *  Ezt a tesztet az a hiba hívta életre, amitől a NetSurf véletlenszerűen
 *  elszállt (page fault a newlib malloc unlink()-jében):
 *
 *    a vmm_map_page_pml4() csak akkor ürítette a TLB-t, ha a KERNEL PML4-be
 *    mappelt. A sys_brk viszont az ÉPPEN FUTÓ user taszk PML4-jébe mappel,
 *    miközben annak a CR3-ja aktív -> minden heap-növelés invlpg nélkül
 *    történt, és a CPU a régi (nem létező, vagy egy korábbi frame-re mutató)
 *    fordítást használta tovább.
 *
 *  A teszt ezért kifejezetten arra hajt rá, hogy sok apró lépésben nőjön a
 *  heap, és MINDEN frissen kapott byte-ot azonnal megírjon és visszaolvasson.
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

static void fail(const char *what, size_t idx, size_t off,
                 unsigned expect, unsigned got)
{
    printf("[heap] FAIL: %s (blokk %lu, offset %lu: vart 0x%02x, kapott 0x%02x)\n",
           what, (unsigned long)idx, (unsigned long)off, expect, got);
    failures++;
}

/* A minta szándékosan függ a blokk indexétől ÉS az offsettől, hogy egy
 * elcsúszott/aliasolt lap is kiderüljön, ne csak a nullázott. */
static unsigned char pat(size_t idx, size_t off)
{
    return (unsigned char)((idx * 131u + off * 7u + (off >> 8) * 29u) | 1u);
}

/* --- 1. teszt: nyers sbrk-növelés, azonnali írás/olvasás -------------------- */
#define SBRK_STEPS 400

static void test_sbrk_growth(void)
{
    static unsigned char *base[SBRK_STEPS];
    static size_t         len[SBRK_STEPS];
    size_t i, j, n = 0;

    for (i = 0; i < SBRK_STEPS; i++) {
        /* Nem lap-többszörös méret, hogy a kernel felfelé kerekítése is
         * részt vegyen a játékban. */
        size_t sz = 4096 * 3 + (i % 11) * 517 + 1;
        unsigned char *p = sbrk((intptr_t)sz);
        if (p == (unsigned char *)-1) break;

        for (j = 0; j < sz; j++) p[j] = pat(i, j);
        /* azonnali visszaolvasás: a friss lapokra írt adat tényleg ott van? */
        for (j = 0; j < sz; j++) {
            if (p[j] != pat(i, j)) {
                fail("sbrk azonnali visszaolvasas", i, j, pat(i, j), p[j]);
                return;
            }
        }
        base[n] = p; len[n] = sz; n++;
    }

    /* És a korábbi blokkok is épek maradtak-e, miután a heap tovább nőtt? */
    for (i = 0; i < n; i++) {
        for (j = 0; j < len[i]; j++) {
            if (base[i][j] != pat(i, j)) {
                fail("sbrk kesobbi ellenorzes", i, j, pat(i, j), base[i][j]);
                return;
            }
        }
    }
    printf("[heap] PASS: sbrk novekedes (%lu blokk, %lu KB)\n",
           (unsigned long)n, (unsigned long)(n * 12 / 1024 + n * 12));
}

/* --- 2. teszt: malloc/free kavarás mintaellenőrzéssel ---------------------- */
#define SLOTS  192
#define ROUNDS 30000

static unsigned long rng_state = 123456789u;
static unsigned long rng(void)
{
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (rng_state >> 17);
}

static void test_malloc_churn(void)
{
    static unsigned char *slot[SLOTS];
    static size_t         slen[SLOTS];
    size_t r, i, j;
    unsigned long allocs = 0;

    for (r = 0; r < ROUNDS; r++) {
        i = rng() % SLOTS;
        if (slot[i] != NULL) {
            for (j = 0; j < slen[i]; j++) {
                if (slot[i][j] != pat(i, j)) {
                    fail("malloc blokk serult", i, j, pat(i, j), slot[i][j]);
                    return;
                }
            }
            free(slot[i]);
            slot[i] = NULL;
            slen[i] = 0;
        } else {
            /* vegyes méretek: kis binek, nagy binek és a top-chunk növelése */
            size_t sz = 16 + (rng() % ((r % 8 == 0) ? 96000 : 3000));
            unsigned char *p = malloc(sz);
            if (p == NULL) continue;      /* OOM: nem hiba, csak kihagyjuk */
            for (j = 0; j < sz; j++) p[j] = pat(i, j);
            slot[i] = p; slen[i] = sz; allocs++;
        }
    }

    for (i = 0; i < SLOTS; i++) {
        if (!slot[i]) continue;
        for (j = 0; j < slen[i]; j++) {
            if (slot[i][j] != pat(i, j)) {
                fail("malloc zaro ellenorzes", i, j, pat(i, j), slot[i][j]);
                return;
            }
        }
        free(slot[i]);
        slot[i] = NULL;
    }
    printf("[heap] PASS: malloc/free kavaras (%lu kor, %lu allokacio)\n",
           (unsigned long)ROUNDS, allocs);
}

/* --- 3. teszt: zsugorodás -> újranövés (malloc_trim / negativ sbrk) -------- */
static void test_shrink_regrow(void)
{
    size_t big = 6u * 1024u * 1024u;
    int cycle;

    for (cycle = 0; cycle < 4; cycle++) {
        unsigned char *p = malloc(big);
        if (!p) { printf("[heap] SKIP: shrink/regrow (nincs eleg memoria)\n"); return; }
        size_t j;
        for (j = 0; j < big; j += 4093) p[j] = pat((size_t)cycle, j);
        for (j = 0; j < big; j += 4093) {
            if (p[j] != pat((size_t)cycle, j)) {
                fail("nagy blokk", (size_t)cycle, j, pat((size_t)cycle, j), p[j]);
                free(p);
                return;
            }
        }
        /* A felszabadítás után a newlib visszaadja a heap tetejét (negativ
         * sbrk); a kovetkezo kor ujra felkeri ugyanazt a cimtartomanyt. */
        free(p);
    }
    printf("[heap] PASS: zsugorodas/ujranoves (4 kor x 6 MB)\n");
}

int main(void)
{
    printf("[heap] Rex OS heap/brk/TLB regresszios teszt indul\n");
    printf("[heap] kezdo brk: %p\n", sbrk(0));

    test_sbrk_growth();
    if (!failures) test_malloc_churn();
    if (!failures) test_shrink_regrow();

    printf("[heap] zaro brk: %p\n", sbrk(0));

    if (failures) {
        printf("[heap] %d HIBA\n", failures);
        return 1;
    }
    printf("[heap] MINDEN TESZT SIKERES\n");
    return 0;
}
