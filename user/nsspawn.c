/* ============================================================================
 *  Rex OS - NetSurf spawn teszt
 *
 *  Pontosan azt csinalja, amit a desktop NetSurf menupontja (apps.c,
 *  netsurf_open): spawn("netsurf.elf") + waitpid. Igy a GUI nelkul is
 *  vegigmegy a valodi felhasznaloi utvonal: ket parhuzamos user taszk,
 *  kulon cimterrel, mindketto a framebufferre mappelve.
 * ========================================================================== */
#include "libc.h"

void _start(void) {
    print("[nsspawn] netsurf.elf inditasa spawn()-nal...\n");
    int pid = spawn("netsurf.elf");
    if (pid <= 0) {
        print("[nsspawn] HIBA: spawn sikertelen\n");
        exit(1);
    }
    print("[nsspawn] elindult, varakozas waitpid-del\n");
    waitpid(pid);
    print("[nsspawn] a NetSurf munkamenet veget ert\n");
    exit(0);
}
