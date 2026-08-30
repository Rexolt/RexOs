#include "libc.h"

/* Fazis 0 teszt: FPU/SSE allapotmentes kontextusvaltaskor + uj syscallok.
 *
 * 1. Elindit egy masodik FP-hasznalo taskot (fpspin.elf), majd sajat
 *    szamitast vegez sok yield-del. Ha a scheduler nem mentene az XMM
 *    regisztereket, a ket task osszeakadna es az eredmenyek eltornenek.
 * 2. Fust-teszt az uj syscallokra: stat_path, fstat_fd, sleep_ms.
 */

void _start(void) {
    print("[fptest] FPU/SSE kontextus teszt indul...\n");

    int pid = spawn("fpspin.elf");
    if (pid < 0) {
        print("[fptest] figyelem: fpspin.elf nem indult el (nincs parhuzamos FP terheles)\n");
    }

    /* Referencia: Leibniz-sor 100k tagja egy huzamban. */
    double ref = 0.0;
    for (int i = 0; i < 100000; i++) {
        double t = (i & 1) ? -1.0 : 1.0;
        ref += t / (2.0 * (double)i + 1.0);
    }

    /* Ugyanaz a szamitas, de 1000 tagonkent atadjuk a CPU-t. Bitre azonos
     * eredmenyt kell kapnunk, ha az FP allapot tulel minden valtast. */
    double sum = 0.0;
    for (int i = 0; i < 100000; i++) {
        double t = (i & 1) ? -1.0 : 1.0;
        sum += t / (2.0 * (double)i + 1.0);
        if ((i % 1000) == 0) yield();
    }

    if (sum == ref) {
        print("[fptest] PASS: FP allapot konzisztens a kontextusvaltasok kozben\n");
    } else {
        print("[fptest] FAIL: FP allapot serult kontextusvaltaskor!\n");
        exit(1);
    }

    /* --- Uj syscallok fust-tesztje --- */
    stat_t st;
    if (stat_path("hello.elf", &st) == 0 && st.length > 0 && (st.flags & 1)) {
        print("[fptest] PASS: stat_path(hello.elf) OK\n");
    } else {
        print("[fptest] FAIL: stat_path nem mukodik\n");
        exit(1);
    }

    int fd = open("hello.elf");
    if (fd >= 0) {
        stat_t fs;
        if (fstat_fd(fd, &fs) == 0 && fs.length == st.length) {
            print("[fptest] PASS: fstat_fd egyezik a stat_path-szal\n");
        } else {
            print("[fptest] FAIL: fstat_fd nem mukodik\n");
            exit(1);
        }
        close(fd);
    }

    uint64_t t0 = get_ticks();
    sleep_ms(100);
    uint64_t t1 = get_ticks();
    if (t1 - t0 >= 10) { /* 100 Hz PIT: 100 ms = 10 tick */
        print("[fptest] PASS: sleep_ms(100) legalabb 10 ticket vart\n");
    } else {
        print("[fptest] FAIL: sleep_ms tul gyorsan tert vissza\n");
        exit(1);
    }

    if (pid > 0) waitpid(pid);
    print("[fptest] MINDEN TESZT SIKERES\n");
    exit(0);
}
