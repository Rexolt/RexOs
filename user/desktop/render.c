/* =============================================================================
 *  RexOS Desktop - Drawing primitives
 *  All primitives draw onto g_back (offscreen buffer).
 * ========================================================================== */
#include "desktop.h"

/* ----- Globals provided by main.c ----------------------------------------- */
uint32_t *g_fb     = 0;
uint32_t *g_back   = 0;
uint64_t  g_scr_w  = 0;
uint64_t  g_scr_h  = 0;
uint64_t  g_pitch  = 0;
uint64_t  g_tick   = 0;
int       g_fps    = 0;

/* Clip rect in screen coords. */
static int s_clip_x = 0, s_clip_y = 0, s_clip_w = 0, s_clip_h = 0;
static int s_clip_active = 0;

void rd_set_clip(int x, int y, int w, int h) {
    s_clip_active = 1;
    s_clip_x = x; s_clip_y = y; s_clip_w = w; s_clip_h = h;
}
void rd_clear_clip(void) { s_clip_active = 0; }

static inline int clip_xmin(void) { return s_clip_active ? s_clip_x : 0; }
static inline int clip_ymin(void) { return s_clip_active ? s_clip_y : 0; }
static inline int clip_xmax(void) {
    return s_clip_active
        ? (s_clip_x + s_clip_w < (int)g_scr_w ? s_clip_x + s_clip_w : (int)g_scr_w)
        : (int)g_scr_w;
}
static inline int clip_ymax(void) {
    return s_clip_active
        ? (s_clip_y + s_clip_h < (int)g_scr_h ? s_clip_y + s_clip_h : (int)g_scr_h)
        : (int)g_scr_h;
}

uint32_t rd_lerp_color(uint32_t a, uint32_t b, int t) {
    if (t <= 0) return a;
    if (t >= 255) return b;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int it = 255 - t;
    int r = (ar * it + br * t) / 255;
    int g = (ag * it + bg * t) / 255;
    int bch = (ab * it + bb * t) / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bch;
}

void rd_clear(uint32_t color) {
    if (!g_back) return;
    uint64_t n = g_scr_w * g_scr_h;
    uint32_t *d = g_back;
    /* rep stosl: 4 bytes per iteration, single retired uop on modern CPUs. */
    __asm__ volatile (
        "rep stosl"
        : "+D"(d), "+c"(n)
        : "a"(color)
        : "memory"
    );
}

void rd_pixel(int x, int y, uint32_t color) {
    if (!g_back) return;
    if (x < clip_xmin() || x >= clip_xmax()) return;
    if (y < clip_ymin() || y >= clip_ymax()) return;
    g_back[y * (int)g_scr_w + x] = color;
}

void rd_rect(int x, int y, int w, int h, uint32_t color) {
    if (!g_back) return;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    int cxmin = clip_xmin(), cymin = clip_ymin();
    int cxmax = clip_xmax(), cymax = clip_ymax();
    if (x0 < cxmin) x0 = cxmin;
    if (y0 < cymin) y0 = cymin;
    if (x1 > cxmax) x1 = cxmax;
    if (y1 > cymax) y1 = cymax;
    if (x1 <= x0 || y1 <= y0) return;
    int sw = (int)g_scr_w;
    int row_w = x1 - x0;
    /* Use rep stosl per row - much faster than per-pixel writes. */
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = g_back + (uint64_t)yy * sw + x0;
        uint64_t n = (uint64_t)row_w;
        __asm__ volatile (
            "rep stosl"
            : "+D"(row), "+c"(n)
            : "a"(color)
            : "memory"
        );
    }
}

void rd_rect_outline(int x, int y, int w, int h, uint32_t color) {
    rd_rect(x, y, w, 1, color);
    rd_rect(x, y + h - 1, w, 1, color);
    rd_rect(x, y, 1, h, color);
    rd_rect(x + w - 1, y, 1, h, color);
}

void rd_blend_rect(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    if (!g_back) return;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    int cxmin = clip_xmin(), cymin = clip_ymin();
    int cxmax = clip_xmax(), cymax = clip_ymax();
    if (x0 < cxmin) x0 = cxmin;
    if (y0 < cymin) y0 = cymin;
    if (x1 > cxmax) x1 = cxmax;
    if (y1 > cymax) y1 = cymax;
    if (x1 <= x0 || y1 <= y0) return;
    if (alpha == 0) return;
    int sw = (int)g_scr_w;
    if (alpha == 255) {
        /* Fully opaque: same as rect. */
        int row_w = x1 - x0;
        for (int yy = y0; yy < y1; yy++) {
            uint32_t *row = g_back + (uint64_t)yy * sw + x0;
            uint64_t n = (uint64_t)row_w;
            __asm__ volatile ("rep stosl" : "+D"(row), "+c"(n) : "a"(color) : "memory");
        }
        return;
    }
    /* Pre-multiply the source channels (color * alpha) once. */
    uint32_t a = alpha, ia = 256 - alpha; /* approximation 256 instead of 255 */
    uint32_t pr = ((color >> 16) & 0xFF) * a;
    uint32_t pg = ((color >> 8)  & 0xFF) * a;
    uint32_t pb = (color & 0xFF) * a;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = g_back + (uint64_t)yy * sw;
        for (int xx = x0; xx < x1; xx++) {
            uint32_t d = row[xx];
            uint32_t r = (pr + ((d >> 16) & 0xFF) * ia) >> 8;
            uint32_t g = (pg + ((d >> 8)  & 0xFF) * ia) >> 8;
            uint32_t b = (pb + (d & 0xFF) * ia) >> 8;
            row[xx] = (r << 16) | (g << 8) | b;
        }
    }
}

void rd_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bot) {
    if (h <= 0) return;
    for (int i = 0; i < h; i++) {
        int t = (i * 255) / (h - 1 > 0 ? h - 1 : 1);
        rd_rect(x, y + i, w, 1, rd_lerp_color(top, bot, t));
    }
}

void rd_gradient_h(int x, int y, int w, int h, uint32_t left, uint32_t right) {
    if (w <= 0) return;
    for (int i = 0; i < w; i++) {
        int t = (i * 255) / (w - 1 > 0 ? w - 1 : 1);
        rd_rect(x + i, y, 1, h, rd_lerp_color(left, right, t));
    }
}

/* Filled rounded rect (anti-aliased corners using simple coverage). */
void rd_rounded(int x, int y, int w, int h, int r, uint32_t color) {
    if (r <= 0) { rd_rect(x, y, w, h, color); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    /* Center band (full width, between corner zones) */
    rd_rect(x, y + r, w, h - 2 * r, color);
    /* Top and bottom bands (between corners) */
    rd_rect(x + r, y, w - 2 * r, r, color);
    rd_rect(x + r, y + h - r, w - 2 * r, r, color);

    /* Four corners with circular distance test (1px AA via half-coverage). */
    int cx_tl = x + r,         cy_tl = y + r;
    int cx_tr = x + w - r - 1, cy_tr = y + r;
    int cx_bl = x + r,         cy_bl = y + h - r - 1;
    int cx_br = x + w - r - 1, cy_br = y + h - r - 1;
    int r2_in  = (r - 1) * (r - 1);
    int r2_out = r * r;
    for (int dy = -r; dy < r; dy++) {
        for (int dx = -r; dx < r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 < r2_in) {
                /* fully inside */
                if (dx <= 0 && dy <= 0) rd_pixel(cx_tl + dx, cy_tl + dy, color);
                if (dx >= 0 && dy <= 0) rd_pixel(cx_tr + dx, cy_tr + dy, color);
                if (dx <= 0 && dy >= 0) rd_pixel(cx_bl + dx, cy_bl + dy, color);
                if (dx >= 0 && dy >= 0) rd_pixel(cx_br + dx, cy_br + dy, color);
            } else if (d2 < r2_out) {
                /* edge: 50% blend for soft corner */
                if (dx <= 0 && dy <= 0) {
                    int px = cx_tl + dx, py = cy_tl + dy;
                    rd_blend_rect(px, py, 1, 1, color, 160);
                }
                if (dx >= 0 && dy <= 0) {
                    int px = cx_tr + dx, py = cy_tr + dy;
                    rd_blend_rect(px, py, 1, 1, color, 160);
                }
                if (dx <= 0 && dy >= 0) {
                    int px = cx_bl + dx, py = cy_bl + dy;
                    rd_blend_rect(px, py, 1, 1, color, 160);
                }
                if (dx >= 0 && dy >= 0) {
                    int px = cx_br + dx, py = cy_br + dy;
                    rd_blend_rect(px, py, 1, 1, color, 160);
                }
            }
        }
    }
}

void rd_rounded_outline(int x, int y, int w, int h, int r, uint32_t color) {
    if (r <= 0) { rd_rect_outline(x, y, w, h, color); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    /* Top/bottom horizontal segments */
    rd_rect(x + r, y, w - 2 * r, 1, color);
    rd_rect(x + r, y + h - 1, w - 2 * r, 1, color);
    /* Left/right vertical segments */
    rd_rect(x, y + r, 1, h - 2 * r, color);
    rd_rect(x + w - 1, y + r, 1, h - 2 * r, color);
    /* Corner arcs (1px) */
    int cx_tl = x + r,         cy_tl = y + r;
    int cx_tr = x + w - r - 1, cy_tr = y + r;
    int cx_bl = x + r,         cy_bl = y + h - r - 1;
    int cx_br = x + w - r - 1, cy_br = y + h - r - 1;
    int r2_in  = (r - 1) * (r - 1);
    int r2_out = r * r;
    for (int dy = -r; dy <= 0; dy++) {
        for (int dx = -r; dx <= 0; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2_in && d2 < r2_out) {
                rd_pixel(cx_tl + dx, cy_tl + dy, color);
                rd_pixel(cx_tr - dx, cy_tr + dy, color);
                rd_pixel(cx_bl + dx, cy_bl - dy, color);
                rd_pixel(cx_br - dx, cy_br - dy, color);
            }
        }
    }
}

/* Alpha-blended filled rounded rect. Same geometry as rd_rounded, but every
 * span is composited onto the back buffer with `alpha`. Corner pixels get an
 * extra ~63% coverage factor for a soft edge. Used by the glass panels and by
 * the layered soft shadow below. */
void rd_blend_rounded(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha) {
    if (alpha == 0) return;
    if (r <= 0) { rd_blend_rect(x, y, w, h, color, alpha); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    /* Center + top/bottom bands. */
    rd_blend_rect(x, y + r, w, h - 2 * r, color, alpha);
    rd_blend_rect(x + r, y, w - 2 * r, r, color, alpha);
    rd_blend_rect(x + r, y + h - r, w - 2 * r, r, color, alpha);

    /* Corner quarter-discs. */
    int cx_tl = x + r,         cy_tl = y + r;
    int cx_tr = x + w - r - 1, cy_tr = y + r;
    int cx_bl = x + r,         cy_bl = y + h - r - 1;
    int cx_br = x + w - r - 1, cy_br = y + h - r - 1;
    int r2_in  = (r - 1) * (r - 1);
    int r2_out = r * r;
    uint8_t edge_a = (uint8_t)((alpha * 160) / 255);
    for (int dy = -r; dy < r; dy++) {
        for (int dx = -r; dx < r; dx++) {
            int d2 = dx * dx + dy * dy;
            uint8_t a;
            if (d2 < r2_in)      a = alpha;
            else if (d2 < r2_out) a = edge_a;
            else continue;
            if (dx <= 0 && dy <= 0) rd_blend_rect(cx_tl + dx, cy_tl + dy, 1, 1, color, a);
            if (dx >= 0 && dy <= 0) rd_blend_rect(cx_tr + dx, cy_tr + dy, 1, 1, color, a);
            if (dx <= 0 && dy >= 0) rd_blend_rect(cx_bl + dx, cy_bl + dy, 1, 1, color, a);
            if (dx >= 0 && dy >= 0) rd_blend_rect(cx_br + dx, cy_br + dy, 1, 1, color, a);
        }
    }
}

/* Soft drop shadow: several concentric rounded layers, each larger and fainter
 * than the last, nudged downward so the light appears to come from above. The
 * window body drawn on top hides the dark inner layers, leaving only a smooth
 * feathered halo around the edges. */
void rd_shadow(int x, int y, int w, int h, int r, int spread) {
    if (spread <= 0) return;
    int ydrop = spread / 3;          /* shadow falls slightly downward */
    /* Step of 2 keeps the layer count (and per-corner blend cost) modest while
     * still overlapping enough to read as a continuous gradient. */
    for (int s = spread; s >= 1; s -= 2) {
        /* Inner layers (small s) darkest; outer layers fade toward the edge. */
        int a = 34 - (s * 30) / spread;
        if (a < 3) a = 3;
        rd_blend_rounded(x - s, y - s + ydrop, w + 2 * s, h + 2 * s,
                         r + s, 0x000000, (uint8_t)a);
    }
}

void rd_circle_fill(int cx, int cy, int r, uint32_t color) {
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2) rd_pixel(cx + dx, cy + dy, color);
        }
    }
}

void rd_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int err = adx - ady;
    int x = x0, y = y0;
    for (;;) {
        rd_pixel(x, y, color);
        if (x == x1 && y == y1) break;
        int e2 = err * 2;
        if (e2 > -ady) { err -= ady; x += sx; }
        if (e2 <  adx) { err += adx; y += sy; }
    }
}

/* Text wrappers - draw onto g_back through font subsystem. */
void rd_text(int x, int y_baseline, const char *s, int px, uint32_t color) {
    if (!s) return;
    font_draw_text(g_back, (int)g_scr_w, (int)g_scr_h, x, y_baseline, s, px, color);
}
void rd_text_bold(int x, int y_baseline, const char *s, int px, uint32_t color) {
    if (!s) return;
    font_draw_text_bold(g_back, (int)g_scr_w, (int)g_scr_h, x, y_baseline, s, px, color);
}
int rd_text_w(const char *s, int px) {
    return font_measure_text(s, px);
}
