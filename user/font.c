/* =============================================================================
 *  RexOS - Font subsystem implementation
 *  TTF font (DejaVu Sans Mono) + LRU glyph cache + alpha blending.
 * ========================================================================== */

#include "font.h"
#include "libc.h"

/* Forward decl - stb_truetype API (font_stb.c implementálja). */
typedef struct stbtt_fontinfo stbtt_fontinfo;
extern int  stbtt_InitFont(stbtt_fontinfo *info, const unsigned char *data, int offset);
extern int  stbtt_GetFontOffsetForIndex(const unsigned char *data, int index);
extern float stbtt_ScaleForPixelHeight(const stbtt_fontinfo *info, float pixels);
extern void stbtt_GetFontVMetrics(const stbtt_fontinfo *info, int *ascent, int *descent, int *lineGap);
extern int  stbtt_FindGlyphIndex(const stbtt_fontinfo *info, int unicode_codepoint);
extern void stbtt_GetGlyphHMetrics(const stbtt_fontinfo *info, int glyph_index, int *advanceWidth, int *leftSideBearing);
extern void stbtt_GetGlyphBitmapBox(const stbtt_fontinfo *info, int glyph,
                                    float scale_x, float scale_y,
                                    int *ix0, int *iy0, int *ix1, int *iy1);
extern void stbtt_MakeGlyphBitmap(const stbtt_fontinfo *info, unsigned char *output,
                                  int out_w, int out_h, int out_stride,
                                  float scale_x, float scale_y, int glyph);

/* A font binary embeddelve van: ld -r -b binary készíti a font_data.o-t,
 * ami exportálja ezeket a szimbólumokat (path-alapú, /-ek aláhúzássá alakítva). */
extern unsigned char _binary_dejavu_mono_ttf_start[];
extern unsigned char _binary_dejavu_mono_ttf_end[];
#define font_dejavu_mono _binary_dejavu_mono_ttf_start

/* stb_truetype context size: van magasabb szintű API-nk, így a teljes méretet
 * itt nem kell ismerni - csak unsigned char[] tároló kell. A font_stb.c-ben
 * lefoglalt sizeof(stbtt_fontinfo) a fontos, de mivel typedef opaque-ot
 * használunk fent, allokálunk biztosra ~512 byte-ot (a struct ~160 byte). */
static unsigned char s_fontinfo_storage[512];
static int s_font_initialized = 0;

#define GLYPH_CACHE_SLOTS 256
typedef struct {
    int valid;
    int codepoint;
    int pixel_height;
    int w, h, xoff, yoff;
    int advance;     /* pixelben, már scale-ezve */
    unsigned char *bitmap;  /* w*h bytes, alpha [0..255] */
} glyph_entry_t;

static glyph_entry_t s_cache[GLYPH_CACHE_SLOTS];
static unsigned int  s_cache_clock = 0; /* monotonic counter for naive LRU */
static unsigned int  s_cache_age[GLYPH_CACHE_SLOTS];

static stbtt_fontinfo *get_info(void) { return (stbtt_fontinfo *)s_fontinfo_storage; }

int font_init(void) {
    if (s_font_initialized) return 1;
    int off = stbtt_GetFontOffsetForIndex(font_dejavu_mono, 0);
    if (off < 0) return 0;
    if (!stbtt_InitFont(get_info(), font_dejavu_mono, off)) return 0;
    for (int i = 0; i < GLYPH_CACHE_SLOTS; i++) {
        s_cache[i].valid = 0;
        s_cache[i].bitmap = 0;
    }
    s_font_initialized = 1;
    return 1;
}

static int find_evict_slot(void) {
    /* Free slot előnyben részesítve. */
    for (int i = 0; i < GLYPH_CACHE_SLOTS; i++) {
        if (!s_cache[i].valid) return i;
    }
    /* LRU: legkisebb age. */
    int oldest = 0;
    unsigned int oa = s_cache_age[0];
    for (int i = 1; i < GLYPH_CACHE_SLOTS; i++) {
        if (s_cache_age[i] < oa) { oa = s_cache_age[i]; oldest = i; }
    }
    if (s_cache[oldest].bitmap) free(s_cache[oldest].bitmap);
    s_cache[oldest].bitmap = 0;
    s_cache[oldest].valid = 0;
    return oldest;
}

static glyph_entry_t *cache_lookup(int codepoint, int pixel_height) {
    for (int i = 0; i < GLYPH_CACHE_SLOTS; i++) {
        if (s_cache[i].valid &&
            s_cache[i].codepoint == codepoint &&
            s_cache[i].pixel_height == pixel_height) {
            s_cache_age[i] = ++s_cache_clock;
            return &s_cache[i];
        }
    }
    return 0;
}

static glyph_entry_t *cache_get(int codepoint, int pixel_height) {
    if (!s_font_initialized && !font_init()) return 0;

    glyph_entry_t *g = cache_lookup(codepoint, pixel_height);
    if (g) return g;

    int slot = find_evict_slot();
    g = &s_cache[slot];

    stbtt_fontinfo *info = get_info();
    float scale = stbtt_ScaleForPixelHeight(info, (float)pixel_height);
    int gi = stbtt_FindGlyphIndex(info, codepoint);
    if (gi == 0) {
        /* missing glyph: fallback '?' */
        if (codepoint != '?') return cache_get('?', pixel_height);
        /* '?' sincs - üres slot. */
    }

    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(info, gi, &adv, &lsb);

    int x0=0, y0=0, x1=0, y1=0;
    stbtt_GetGlyphBitmapBox(info, gi, scale, scale, &x0, &y0, &x1, &y1);

    int w = x1 - x0;
    int h = y1 - y0;
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    unsigned char *bm = 0;
    if (w > 0 && h > 0) {
        bm = (unsigned char *)malloc((uint64_t)(w * h));
        if (!bm) { g->valid = 0; return 0; }
        for (int i = 0; i < w * h; i++) bm[i] = 0;
        stbtt_MakeGlyphBitmap(info, bm, w, h, w, scale, scale, gi);
    }

    g->valid = 1;
    g->codepoint = codepoint;
    g->pixel_height = pixel_height;
    g->w = w;
    g->h = h;
    g->xoff = x0;
    g->yoff = y0;
    g->advance = (int)((float)adv * scale + 0.5f);
    g->bitmap = bm;
    s_cache_age[slot] = ++s_cache_clock;
    return g;
}

/* Alpha blend: dst és src color, src alpha [0..255]. */
static inline uint32_t blend_pixel(uint32_t dst, uint32_t src_color, unsigned int alpha) {
    if (alpha == 0)   return dst;
    if (alpha == 255) return src_color;
    unsigned int dr = (dst >> 16) & 0xFF;
    unsigned int dg = (dst >> 8)  & 0xFF;
    unsigned int db = dst & 0xFF;
    unsigned int sr = (src_color >> 16) & 0xFF;
    unsigned int sg = (src_color >> 8)  & 0xFF;
    unsigned int sb = src_color & 0xFF;
    unsigned int ia = 255 - alpha;
    unsigned int r = (sr * alpha + dr * ia) / 255;
    unsigned int g = (sg * alpha + dg * ia) / 255;
    unsigned int b = (sb * alpha + db * ia) / 255;
    return (r << 16) | (g << 8) | b;
}

int font_draw_char(uint32_t *dst, int dst_w, int dst_h,
                   int x, int y, int codepoint, int pixel_height, uint32_t color) {
    glyph_entry_t *g = cache_get(codepoint, pixel_height);
    if (!g) return 0;
    if (g->bitmap) {
        int gx0 = x + g->xoff;
        int gy0 = y + g->yoff; /* y itt a baseline, yoff jellemzően negatív felső pixel-eltolás */
        for (int yy = 0; yy < g->h; yy++) {
            int dy = gy0 + yy;
            if (dy < 0 || dy >= dst_h) continue;
            uint32_t *row = dst + (long long)dy * dst_w;
            for (int xx = 0; xx < g->w; xx++) {
                int dx = gx0 + xx;
                if (dx < 0 || dx >= dst_w) continue;
                unsigned int a = g->bitmap[yy * g->w + xx];
                if (a == 0) continue;
                row[dx] = blend_pixel(row[dx], color, a);
            }
        }
    }
    return g->advance;
}

int font_draw_text(uint32_t *dst, int dst_w, int dst_h,
                   int x, int y, const char *text, int pixel_height, uint32_t color) {
    if (!text) return 0;
    int x0 = x;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        x += font_draw_char(dst, dst_w, dst_h, x, y, (int)c, pixel_height, color);
    }
    return x - x0;
}

int font_draw_text_bold(uint32_t *dst, int dst_w, int dst_h,
                        int x, int y, const char *text, int pixel_height, uint32_t color) {
    /* Faux bold: rajzoljunk kétszer 1px offsettel. */
    int w = font_draw_text(dst, dst_w, dst_h, x, y, text, pixel_height, color);
    font_draw_text(dst, dst_w, dst_h, x + 1, y, text, pixel_height, color);
    return w + 1;
}

int font_measure_text(const char *text, int pixel_height) {
    if (!text) return 0;
    int w = 0;
    while (*text) {
        glyph_entry_t *g = cache_get((unsigned char)*text++, pixel_height);
        if (g) w += g->advance;
    }
    return w;
}

int font_char_advance(int codepoint, int pixel_height) {
    glyph_entry_t *g = cache_get(codepoint, pixel_height);
    return g ? g->advance : 0;
}

int font_ascent(int pixel_height) {
    if (!s_font_initialized && !font_init()) return pixel_height * 3 / 4;
    int a, d, lg;
    stbtt_GetFontVMetrics(get_info(), &a, &d, &lg);
    float scale = stbtt_ScaleForPixelHeight(get_info(), (float)pixel_height);
    return (int)((float)a * scale + 0.5f);
}

int font_descent(int pixel_height) {
    if (!s_font_initialized && !font_init()) return pixel_height / 4;
    int a, d, lg;
    stbtt_GetFontVMetrics(get_info(), &a, &d, &lg);
    float scale = stbtt_ScaleForPixelHeight(get_info(), (float)pixel_height);
    return (int)((float)(-d) * scale + 0.5f);
}

int font_line_height(int pixel_height) {
    if (!s_font_initialized && !font_init()) return pixel_height + 2;
    int a, d, lg;
    stbtt_GetFontVMetrics(get_info(), &a, &d, &lg);
    float scale = stbtt_ScaleForPixelHeight(get_info(), (float)pixel_height);
    return (int)((float)(a - d + lg) * scale + 0.5f);
}
