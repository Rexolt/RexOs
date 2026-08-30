/* Fazis 1 teszt: newlib libc a RexOS-en.
 * printf (float formazassal), malloc, fajl I/O (fopen/fread/fseek/ftell),
 * stat, ido. Ha minden megy, "MINDEN TESZT SIKERES" kerul a kimenetre. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

int main(void)
{
    printf("[newlib] hello a newlib-es vilagbol! (printf mukodik)\n");

    /* Float formazas: az SSE + a newlib teljes vfprintf utvonal tesztje */
    double pi = 3.14159265358979;
    printf("[newlib] float teszt: pi = %.5f, 1/3 = %.6f\n", pi, 1.0 / 3.0);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f", 2.5);
    if (strcmp(buf, "2.50") != 0) {
        printf("[newlib] FAIL: snprintf float\n");
        return 1;
    }
    printf("[newlib] PASS: snprintf float\n");

    /* malloc: nagyobb blokk + irhatosag */
    size_t sz = 4 * 1024 * 1024;
    unsigned char *p = malloc(sz);
    if (!p) {
        printf("[newlib] FAIL: malloc(4MB)\n");
        return 1;
    }
    memset(p, 0xAB, sz);
    if (p[0] != 0xAB || p[sz - 1] != 0xAB) {
        printf("[newlib] FAIL: malloc tartalom\n");
        return 1;
    }
    free(p);
    printf("[newlib] PASS: malloc(4MB) + memset\n");

    /* Fajl I/O az initrd-rol */
    struct stat st;
    if (stat("hello.elf", &st) != 0 || st.st_size <= 0) {
        printf("[newlib] FAIL: stat(hello.elf)\n");
        return 1;
    }
    FILE *f = fopen("hello.elf", "rb");
    if (!f) {
        printf("[newlib] FAIL: fopen(hello.elf)\n");
        return 1;
    }
    unsigned char magic[4];
    if (fread(magic, 1, 4, f) != 4 ||
        magic[0] != 0x7F || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') {
        printf("[newlib] FAIL: fread ELF magic\n");
        fclose(f);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || ftell(f) != (long)st.st_size) {
        printf("[newlib] FAIL: fseek/ftell (%ld != %ld)\n", ftell(f), (long)st.st_size);
        fclose(f);
        return 1;
    }
    fclose(f);
    printf("[newlib] PASS: fopen/fread/fseek/ftell (meret: %ld byte)\n", (long)st.st_size);

    /* Ido */
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    if (tm) {
        printf("[newlib] PASS: time/gmtime: %04d-%02d-%02d %02d:%02d\n",
               tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
               tm->tm_hour, tm->tm_min);
    }

    printf("[newlib] MINDEN TESZT SIKERES\n");
    return 0;
}
