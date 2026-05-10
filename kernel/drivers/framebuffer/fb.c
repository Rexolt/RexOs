/* ============================================================================
 *  Rex OS - linear framebuffer hozzáférés
 *
 *  A Limine-tól kérünk egy lineáris framebuffert. A response struct
 *  egy tömb framebuffer-leírót ad vissza (multi-monitor support),
 *  mi az elsőt használjuk.
 *
 *  Phase 2/A: csak pixel-szintű primitívek.
 *  Phase 2/B: font + szöveges konzol erre az alapra épül.
 * ========================================================================== */

#include <drivers/framebuffer/fb.h>
#include <limine.h>

/* --- Limine framebuffer request ----------------------------------------- */

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL,
};

static struct limine_framebuffer *s_fb = NULL;

bool fb_init(void)
{
    if (fb_request.response == NULL)                       return false;
    if (fb_request.response->framebuffer_count < 1)        return false;

    s_fb = fb_request.response->framebuffers[0];

    if (s_fb == NULL)        return false;
    if (s_fb->address == NULL) return false;
    if (s_fb->bpp != 32)       return false;  /* most csak 32bpp-t támogatunk */

    return true;
}

uint32_t fb_width(void)  { return s_fb ? (uint32_t)s_fb->width  : 0; }
uint32_t fb_height(void) { return s_fb ? (uint32_t)s_fb->height : 0; }
uint32_t fb_bpp(void)    { return s_fb ? (uint32_t)s_fb->bpp    : 0; }
uint32_t fb_pitch(void)  { return s_fb ? (uint32_t)s_fb->pitch  : 0; }
void *   fb_address(void){ return s_fb ? s_fb->address : NULL; }

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!s_fb) return;
    if (x >= s_fb->width || y >= s_fb->height) return;

    uint8_t  *base = (uint8_t *)s_fb->address;
    uint32_t *pix  = (uint32_t *)(base + (uint64_t)y * s_fb->pitch + x * 4);
    *pix = color;
}

void fb_clear(uint32_t color)
{
    if (!s_fb) return;

    uint8_t *base = (uint8_t *)s_fb->address;
    for (uint64_t y = 0; y < s_fb->height; y++) {
        uint32_t *row = (uint32_t *)(base + y * s_fb->pitch);
        for (uint64_t x = 0; x < s_fb->width; x++) {
            row[x] = color;
        }
    }
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!s_fb) return;

    uint32_t x_end = x + w;
    uint32_t y_end = y + h;
    if (x_end > s_fb->width)  x_end = s_fb->width;
    if (y_end > s_fb->height) y_end = s_fb->height;

    uint8_t *base = (uint8_t *)s_fb->address;
    for (uint32_t dy = y; dy < y_end; dy++) {
        uint32_t *row = (uint32_t *)(base + (uint64_t)dy * s_fb->pitch);
        for (uint32_t dx = x; dx < x_end; dx++) {
            row[dx] = color;
        }
    }
}

void fb_put_char(uint32_t x, uint32_t y, uint8_t ch, uint32_t fg, uint32_t bg)
{
    if (!s_fb) return;
    if (x + FONT_W > s_fb->width || y + FONT_H > s_fb->height) return;

    const uint8_t *glyph = font_8x16[ch];
    uint8_t *base = (uint8_t *)s_fb->address;

    for (uint32_t row = 0; row < FONT_H; row++) {
        uint32_t *line = (uint32_t *)(base + (uint64_t)(y + row) * s_fb->pitch);
        uint8_t bits = glyph[row];
        /* Bit 7 = bal szélső pixel */
        for (uint32_t col = 0; col < FONT_W; col++) {
            line[x + col] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

void fb_blit_rows(uint32_t dst_y, uint32_t src_y, uint32_t rows)
{
    if (!s_fb) return;
    if (src_y == dst_y || rows == 0) return;

    uint8_t *base = (uint8_t *)s_fb->address;
    uint64_t pitch = s_fb->pitch;

    /* Egyszerű kopás; itt csak felfelé (dst_y < src_y) használjuk scrolling-hoz. */
    if (dst_y < src_y) {
        for (uint32_t i = 0; i < rows; i++) {
            uint8_t *dst = base + (uint64_t)(dst_y + i) * pitch;
            uint8_t *src = base + (uint64_t)(src_y + i) * pitch;
            uint64_t bytes = s_fb->width * 4;
            for (uint64_t b = 0; b < bytes; b++) dst[b] = src[b];
        }
    } else {
        /* Lefelé másolás - hátulról előrefelé */
        for (uint32_t i = rows; i > 0; i--) {
            uint8_t *dst = base + (uint64_t)(dst_y + i - 1) * pitch;
            uint8_t *src = base + (uint64_t)(src_y + i - 1) * pitch;
            uint64_t bytes = s_fb->width * 4;
            for (uint64_t b = 0; b < bytes; b++) dst[b] = src[b];
        }
    }
}
