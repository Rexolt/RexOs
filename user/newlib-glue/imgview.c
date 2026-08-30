/* Fazis 2 teszt: libpng + libjpeg + zlib a RexOS-en.
 * A teszt kepeket (test.png, test.jpg) az initrd-rol dekodolja es a
 * framebufferre rajzolja. A dekodolt pixeleket ellenorzi is, igy headless
 * QEMU-ban automata tesztkent futtathato. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <jpeglib.h>
#include <setjmp.h>
#include "rexos_syscall.h"

static uint32_t *g_fb;
static uint64_t g_w, g_h, g_pitch;

static void blit(const unsigned char *rgb, int w, int h, int x0, int y0)
{
    if (!g_fb) return;
    for (int y = 0; y < h; y++) {
        if ((uint64_t)(y0 + y) >= g_h) break;
        uint32_t *row = (uint32_t *)((uint8_t *)g_fb + (uint64_t)(y0 + y) * g_pitch);
        for (int x = 0; x < w; x++) {
            if ((uint64_t)(x0 + x) >= g_w) break;
            const unsigned char *p = rgb + (y * w + x) * 3;
            row[x0 + x] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }
}

/* --- PNG (libpng simplified API) ------------------------------------------- */

static int test_png(void)
{
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_file(&img, "test.png")) {
        printf("[imgview] FAIL: png open: %s\n", img.message);
        return -1;
    }
    img.format = PNG_FORMAT_RGB;
    unsigned char *buf = malloc(PNG_IMAGE_SIZE(img));
    if (!buf || !png_image_finish_read(&img, NULL, buf, 0, NULL)) {
        printf("[imgview] FAIL: png decode: %s\n", img.message);
        return -1;
    }
    printf("[imgview] PNG dekodolva: %ux%u\n", img.width, img.height);

    /* A generalt teszt kep bal felso sarka tiszta piros (255,0,0) */
    if (buf[0] != 255 || buf[1] != 0 || buf[2] != 0) {
        printf("[imgview] FAIL: png pixel ellenorzes (%d,%d,%d)\n",
               buf[0], buf[1], buf[2]);
        return -1;
    }
    blit(buf, img.width, img.height, 40, 40);
    free(buf);
    printf("[imgview] PASS: PNG (libpng + zlib)\n");
    return 0;
}

/* --- JPEG (libjpeg) --------------------------------------------------------- */

struct jpg_err {
    struct jpeg_error_mgr mgr;
    jmp_buf jb;
};

static void jpg_err_exit(j_common_ptr cinfo)
{
    struct jpg_err *e = (struct jpg_err *)cinfo->err;
    longjmp(e->jb, 1);
}

static int test_jpg(void)
{
    FILE *f = fopen("test.jpg", "rb");
    if (!f) {
        printf("[imgview] FAIL: test.jpg nem nyithato\n");
        return -1;
    }

    struct jpeg_decompress_struct cinfo;
    struct jpg_err jerr;
    cinfo.err = jpeg_std_error(&jerr.mgr);
    jerr.mgr.error_exit = jpg_err_exit;
    if (setjmp(jerr.jb)) {
        printf("[imgview] FAIL: jpeg decode hiba\n");
        jpeg_destroy_decompress(&cinfo);
        fclose(f);
        return -1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int w = cinfo.output_width, h = cinfo.output_height;
    unsigned char *buf = malloc((size_t)w * h * 3);
    while ((int)cinfo.output_scanline < h) {
        unsigned char *row = buf + (size_t)cinfo.output_scanline * w * 3;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);

    printf("[imgview] JPEG dekodolva: %dx%d\n", w, h);

    /* A teszt JPEG bal felso sarka kek(es) - veszteseges, ezert toleranciaval */
    if (!(buf[0] < 80 && buf[1] < 80 && buf[2] > 170)) {
        printf("[imgview] FAIL: jpeg pixel ellenorzes (%d,%d,%d)\n",
               buf[0], buf[1], buf[2]);
        return -1;
    }
    blit(buf, w, h, 40, 260);
    free(buf);
    printf("[imgview] PASS: JPEG (libjpeg-turbo)\n");
    return 0;
}

int main(void)
{
    printf("[imgview] kepdekodolas teszt (zlib/libpng/libjpeg)...\n");
    g_fb = rexos_get_fb(&g_w, &g_h, &g_pitch);

    if (test_png() != 0) return 1;
    if (test_jpg() != 0) return 1;

    printf("[imgview] MINDEN TESZT SIKERES\n");
    return 0;
}
