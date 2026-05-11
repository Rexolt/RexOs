/* ============================================================================
 *  Rex OS Desktop Environment v2
 *  - Több ablak támogatás, ablak fókusz / húzás / bezárás
 *  - Asztalon parancsikonok kattintással
 *  - Tálca + Start menü
 *  - Beépített alkalmazások:
 *      * Files: fájlrendszer böngésző (initrd + FAT32 / mnt)
 *      * Editor / Viewer: szövegfájl megtekintő
 *      * Calculator: 4 alapművelet
 *      * SysInfo: rendszer adatok
 *      * Clock: digitális óra, nagy számokkal
 *      * Terminal: egyszerű parancssor a desktopon belül
 *      * About RexOS
 *  - ESC: kilépés a desktopból
 * ========================================================================== */

#include "libc.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* --- Konfiguráció -------------------------------------------------------- */

#define MAX_WINDOWS    16
#define TASKBAR_H      36
#define TITLE_H        24
#define BORDER         1

/* === Modern Dark Mode paletta (Slate/Violet) ============================= */
#define COLOR_BG_TOP      0x060B14  /* Mélykék háttér teteje */
#define COLOR_BG_BOT      0x0F1A2E  /* Mélykék háttér alja */
#define COLOR_TASKBAR     0x080E1C  /* Szinte fekete tálca */
#define COLOR_TASKBAR_SEP 0x1E2D45  /* Elválasztó vonal */
#define COLOR_ACCENT      0x7C3AED  /* Violet lila */
#define COLOR_ACCENT_HOV  0x8B5CF6  /* Hover lila (világosabb) */
#define COLOR_ACCENT_DIM  0x3B1F6A  /* Sötétebb lila */
#define COLOR_WIN_BG      0x111827  /* Ablak háttér */
#define COLOR_WIN_TITLE   0x1A2438  /* Inaktív titlebar */
#define COLOR_WIN_TITLE_A 0x1E3A5F  /* Aktív titlebar */
#define COLOR_WIN_TITLE_A2 0x0F2444 /* Aktív titlebar gradient alja */
#define COLOR_TEXT        0xF1F5F9  /* Kellemes off-white szöveg */
#define COLOR_TEXT_DIM    0x64748B  /* Halványabb szöveg */
#define COLOR_FRAME       0x1E3050  /* Ablak keret */
#define COLOR_FRAME_A     0x3B5BDB  /* Aktív keret (kék) */
#define COLOR_BUTTON      0x1E293B  /* Gomb alap */
#define COLOR_BUTTON_HOV  0x334155  /* Gomb hover */
#define COLOR_FIELD       0x0A1020  /* Input mező */
#define COLOR_FOLDER      0xFBBF24  /* Amber sárga */
#define COLOR_FILE        0x60A5FA  /* Kék fájl */
#define COLOR_EXE         0x34D399  /* Smaragdzöld futtatható */
#define COLOR_TXT         0xC084FC  /* Lila szöveg */
#define COLOR_SHADOW      0x000000  /* Árnyék alap */

static uint32_t *fb;
static uint64_t scr_w, scr_h, pitch;

/* --- Bitmap font 5x7 (ugyanaz, mint korábban) ---------------------------- */

static const uint8_t font5x7[][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
  {0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},
  {0x3E,0x41,0x41,0x51,0x32},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
  {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x00,0x7F,0x41,0x41},
  {0x02,0x04,0x08,0x10,0x20},{0x41,0x41,0x7F,0x00,0x00},{0x04,0x02,0x01,0x02,0x04},
  {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
  {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
  {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
  {0x00,0x7F,0x10,0x28,0x44},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
  {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
  {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
  {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
  {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00},{0x08,0x08,0x2A,0x1C,0x08},
};

/* --- Háttér-puffer (back buffer) ----------------------------------------- */

static uint32_t *backbuf;
static uint64_t backbuf_size;

static inline void put_bb(uint32_t x, uint32_t y, uint32_t c) {
    if (x < scr_w && y < scr_h) backbuf[y * scr_w + x] = c;
}

static void bb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)scr_w) w = (int)scr_w - x;
    if (y + h > (int)scr_h) h = (int)scr_h - y;
    if (w <= 0 || h <= 0) return;
    for (int dy = 0; dy < h; dy++) {
        uint32_t *row = backbuf + (uint64_t)(y + dy) * scr_w;
        for (int dx = 0; dx < w; dx++) row[x + dx] = c;
    }
}

static void bb_hline(int x, int y, int w, uint32_t c) { bb_fill_rect(x, y, w, 1, c); }
static void bb_vline(int x, int y, int h, uint32_t c) { bb_fill_rect(x, y, 1, h, c); }

static void bb_draw_char(int x, int y, char ch, uint32_t color, int scale) {
    if (ch < 32 || ch > 126) return;
    const uint8_t *glyph = font5x7[ch - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        put_bb(x + col*scale + sx, y + row*scale + sy, color);
            }
        }
    }
}

static void bb_draw_text(int x, int y, const char *s, uint32_t color, int scale) {
    while (*s) {
        bb_draw_char(x, y, *s, color, scale);
        x += 6 * scale;
        s++;
    }
}

static void bb_frame(int x, int y, int w, int h, uint32_t c) {
    bb_hline(x, y, w, c);
    bb_hline(x, y + h - 1, w, c);
    bb_vline(x, y, h, c);
    bb_vline(x + w - 1, y, h, c);
}

/* Háttérkép puffer (deklarálás az összes wallpaper-függvény előtt) */
static uint32_t *g_wallpaper_buf  = NULL;
static int       g_wallpaper_ok   = 0;

/* Procedurális háttér az g_wallpaper_buf-ba rajzol (csak egyszer hívják). */
static void wallpaper_render_procedural(void) {
    /* Kétszínű mélykék vertikális gradiens */
    for (uint32_t y = 0; y < scr_h; y++) {
        uint8_t t = (uint8_t)(y * 255 / scr_h);
        uint32_t r = (uint8_t)(((uint32_t)6  * (255 - t) + (uint32_t)15  * t) / 255);
        uint32_t g_ch=(uint8_t)(((uint32_t)11 * (255 - t) + (uint32_t)26  * t) / 255);
        uint32_t b = (uint8_t)(((uint32_t)20 * (255 - t) + (uint32_t)46  * t) / 255);
        uint32_t c = (r << 16) | (g_ch << 8) | b;
        uint32_t *row = g_wallpaper_buf + (uint64_t)y * scr_w;
        for (uint32_t x = 0; x < scr_w; x++) row[x] = c;
    }
    /* Halvány csillagok (véletlenszerű pozíciók) */
    static const uint16_t starpos[] = {
        137,412,891,1023,234,567,778,1145,99,333,890,201,645,1011,
        178,456,723,999,1067,88,444,822,512,703,169,938,275,1188,61,849
    };
    for (uint32_t i = 0; i < sizeof(starpos)/sizeof(starpos[0]); i++) {
        uint32_t sx = starpos[i] % scr_w;
        uint32_t sy = (starpos[i] * 7 + 13) % (scr_h > TASKBAR_H ? scr_h - TASKBAR_H : 1);
        uint8_t br = 40 + (uint8_t)((starpos[i] * 3) % 80);
        g_wallpaper_buf[sy * scr_w + sx] = ((uint32_t)br << 16) | ((uint32_t)br << 8) | (uint32_t)(br + 30);
    }
    /* Halvány kékeslila vinjett a sarokba (inline blend, alpha_blend nélkül) */
    for (uint32_t vy = 0; vy < scr_h / 3; vy++) {
        for (uint32_t vx = 0; vx < scr_w / 4; vx++) {
            uint32_t d = vx * vx / 4 + vy * vy / 2;
            if (d < 30000) {
                uint32_t a2 = (30000u - d) * 30u / 30000u;
                uint32_t *ppx = g_wallpaper_buf + vy * scr_w + vx;
                uint32_t fg = 0x4C1D95u, bg = *ppx, ia = 255u - a2;
                uint32_t rb = (((fg & 0xFF00FFu) * a2 + (bg & 0xFF00FFu) * ia) >> 8) & 0xFF00FFu;
                uint32_t gv = (((fg & 0x00FF00u) * a2 + (bg & 0x00FF00u) * ia) >> 8) & 0x00FF00u;
                *ppx = rb | gv;
            }
        }
    }
}

/* Háttér másolása a backbufba (minden frame elején). */
static void bb_draw_wallpaper(uint64_t tick) {
    (void)tick;
    if (!g_wallpaper_buf) return;
    uint64_t bytes = (uint64_t)scr_w * scr_h * 4;
    uint8_t *dst = (uint8_t *)backbuf;
    uint8_t *src = (uint8_t *)g_wallpaper_buf;
    /* Gyors másolás 8-bájtos blokkokban */
    uint64_t words = bytes / 8;
    uint64_t *d64 = (uint64_t *)dst, *s64 = (uint64_t *)src;
    for (uint64_t i = 0; i < words; i++) d64[i] = s64[i];
}

/* ============================================================================
 *  MODERN GRAFIKUS PRIMITÍVEK
 * ========================================================================== */

/* Forward declaration (definíció lejjebb van) */
static void flush_rect(int x, int y, int w, int h);

/* --- Alpha blending ------------------------------------------------------ */

/* Gyors alpha blend: fg-t a=0..255 opacitással kever bg-re.
 * A két csatorna (RB és G) egyszerre kezelhető 32-bit szélességű trükkel. */
static inline uint32_t alpha_blend(uint32_t fg, uint32_t bg, uint8_t a) {
    uint32_t ia = 255u - a;
    uint32_t rb = (((fg & 0xFF00FFu) * a + (bg & 0xFF00FFu) * ia) >> 8) & 0xFF00FFu;
    uint32_t g  = (((fg & 0x00FF00u) * a + (bg & 0x00FF00u) * ia) >> 8) & 0x00FF00u;
    return rb | g;
}

/* Alpha-blendelt téglalapkitöltés a back bufferbe.
 * alpha=255 teljesen átlátszatlan, alpha=0 láthatatlan. */
static void bb_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    if (alpha == 255) { bb_fill_rect(x, y, w, h, color); return; }
    if (alpha == 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)scr_w) w = (int)scr_w - x;
    if (y + h > (int)scr_h) h = (int)scr_h - y;
    if (w <= 0 || h <= 0) return;
    for (int dy = 0; dy < h; dy++) {
        uint32_t *row = backbuf + (uint64_t)(y + dy) * scr_w;
        for (int dx = 0; dx < w; dx++) {
            row[x + dx] = alpha_blend(color, row[x + dx], alpha);
        }
    }
}

/* --- Lekerekített sarkok ------------------------------------------------- */

/* Egypixeles lekerekítési maszk: az adott sarokhoz tartozó pixelt beleteszi,
 * ha belül van a köríven. */
static void bb_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t c) {
    if (r <= 0 || w <= 0 || h <= 0) { bb_fill_rect(x, y, w, h, c); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    /* Középső vízszintes sáv (teljes) */
    bb_fill_rect(x, y + r, w, h - 2 * r, c);
    /* Felső és alsó vízszintes sáv (sarkok nélkül) */
    bb_fill_rect(x + r, y,         w - 2 * r, r, c);
    bb_fill_rect(x + r, y + h - r, w - 2 * r, r, c);

    /* 4 sarok: körív alapú pixelezés */
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            int ndx = r - 1 - dx;
            int ndy = r - 1 - dy;
            int dist2 = ndx * ndx + ndy * ndy;
            if (dist2 <= r * r) {
                put_bb(x + dx,           y + dy,           c); /* bal-felső */
                put_bb(x + w - 1 - dx,   y + dy,           c); /* jobb-felső */
                put_bb(x + dx,           y + h - 1 - dy,   c); /* bal-alsó */
                put_bb(x + w - 1 - dx,   y + h - 1 - dy,   c); /* jobb-alsó */
            }
        }
    }
}

static void bb_fill_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t c, uint8_t a) {
    if (a == 255) { bb_fill_rounded_rect(x, y, w, h, r, c); return; }
    if (a == 0 || w <= 0 || h <= 0) return;
    if (r <= 0) { bb_fill_rect_alpha(x, y, w, h, c, a); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    bb_fill_rect_alpha(x,     y + r, w,         h - 2 * r, c, a);
    bb_fill_rect_alpha(x + r, y,     w - 2 * r, r,         c, a);
    bb_fill_rect_alpha(x + r, y + h - r, w - 2 * r, r,     c, a);

    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            int ndx = r - 1 - dx, ndy = r - 1 - dy;
            if (ndx * ndx + ndy * ndy <= r * r) {
                int bx, by;
                /* bal-felső */ bx = x + dx; by = y + dy;
                if (bx < (int)scr_w && by < (int)scr_h && bx >= 0 && by >= 0)
                    backbuf[by * scr_w + bx] = alpha_blend(c, backbuf[by * scr_w + bx], a);
                /* jobb-felső */ bx = x + w - 1 - dx;
                if (bx < (int)scr_w && bx >= 0)
                    backbuf[by * scr_w + bx] = alpha_blend(c, backbuf[by * scr_w + bx], a);
                /* bal-alsó */ bx = x + dx; by = y + h - 1 - dy;
                if (bx < (int)scr_w && by < (int)scr_h && bx >= 0 && by >= 0)
                    backbuf[by * scr_w + bx] = alpha_blend(c, backbuf[by * scr_w + bx], a);
                /* jobb-alsó */ bx = x + w - 1 - dx;
                if (bx < (int)scr_w && by < (int)scr_h && bx >= 0)
                    backbuf[by * scr_w + bx] = alpha_blend(c, backbuf[by * scr_w + bx], a);
            }
        }
    }
}

/* --- Lineáris gradiens (vertical) --------------------------------------- */

static void bb_fill_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)scr_w) w = (int)scr_w - x;
    if (y + h > (int)scr_h) h = (int)scr_h - y;
    if (w <= 0 || h <= 0) return;
    for (int dy = 0; dy < h; dy++) {
        uint8_t t = (h > 1) ? (uint8_t)(dy * 255 / (h - 1)) : 0;
        uint32_t c = alpha_blend(c2, c1, t);  /* c1 a tetején, c2 alul */
        uint32_t *row = backbuf + (uint64_t)(y + dy) * scr_w;
        for (int dx = 0; dx < w; dx++) row[x + dx] = c;
    }
}

/* --- Drop shadow (könnyű, 3 rétegű) ------------------------------------- */

/* Hatékony "slot" árnyék: csak a jobb és alsó élt rajzolja meg,
 * 3 rétegben csökkenő átlátszósággal. */
static void bb_draw_shadow(int x, int y, int w, int h) {
    bb_fill_rect_alpha(x + w,     y + 4, 4, h,     COLOR_SHADOW, 90);
    bb_fill_rect_alpha(x + 4, y + h,     w, 4,     COLOR_SHADOW, 90);
    bb_fill_rect_alpha(x + w + 1, y + 5, 2, h - 2, COLOR_SHADOW, 50);
    bb_fill_rect_alpha(x + 5, y + h + 1, w - 2, 2, COLOR_SHADOW, 50);
}

/* --- Dirty rectangle rendszer ------------------------------------------- */

static int g_dirty_x1 = 0, g_dirty_y1 = 0;
static int g_dirty_x2 = 0, g_dirty_y2 = 0;

/* Előző frame flush területe.
 * Szükséges: ha egy ablak elmozdul, a RÉGI pozícióját is frissíteni kell
 * a framebufferben (különben ott marad a "szellemkép"). */
static int g_prev_flush_x1 = 0, g_prev_flush_y1 = 0;
static int g_prev_flush_x2 = 0, g_prev_flush_y2 = 0;

static void dirty_reset(void) {
    g_dirty_x1 = (int)scr_w;
    g_dirty_y1 = (int)scr_h;
    g_dirty_x2 = 0;
    g_dirty_y2 = 0;
}

static void dirty_mark(int x, int y, int w, int h) {
    int x2 = x + w, y2 = y + h;
    if (x  < g_dirty_x1) g_dirty_x1 = x;
    if (y  < g_dirty_y1) g_dirty_y1 = y;
    if (x2 > g_dirty_x2) g_dirty_x2 = x2;
    if (y2 > g_dirty_y2) g_dirty_y2 = y2;
}

static void dirty_flush(void) {
    if (g_dirty_x1 >= g_dirty_x2 || g_dirty_y1 >= g_dirty_y2) return;
    int cx = g_dirty_x1 < 0 ? 0 : g_dirty_x1;
    int cy = g_dirty_y1 < 0 ? 0 : g_dirty_y1;
    int cx2 = g_dirty_x2 > (int)scr_w ? (int)scr_w : g_dirty_x2;
    int cy2 = g_dirty_y2 > (int)scr_h ? (int)scr_h : g_dirty_y2;
    flush_rect(cx, cy, cx2 - cx, cy2 - cy);
}

/* --- BMP háttérkép betöltő (24/32bpp) ----------------------------------- */

/* BMP beolvasása és scaler a wallpaper_buf-ba. */
static uint32_t bmp_row_stride(int bpp, int width) {
    return (uint32_t)(((bpp * width + 31) / 32) * 4);
}

static void load_bmp_wallpaper(const char *path) {
    int fd = open(path);
    if (fd < 0) return;

    /* BMP fejléc: minimum 54 byte */
    uint8_t hdr[54];
    if (read(fd, hdr, 54) != 54) { close(fd); return; }
    if (hdr[0] != 'B' || hdr[1] != 'M') { close(fd); return; }

    uint32_t px_off  = (uint32_t)(hdr[10] | (hdr[11]<<8) | (hdr[12]<<16) | (hdr[13]<<24));
    int32_t  bmp_w   = (int32_t)(hdr[18] | (hdr[19]<<8) | (hdr[20]<<16) | (hdr[21]<<24));
    int32_t  bmp_h   = (int32_t)(hdr[22] | (hdr[23]<<8) | (hdr[24]<<16) | (hdr[25]<<24));
    uint16_t bpp     = (uint16_t)(hdr[28] | (hdr[29]<<8));

    if ((bpp != 24 && bpp != 32) || bmp_w <= 0) { close(fd); return; }

    int top_down = 0;
    if (bmp_h < 0) { bmp_h = -bmp_h; top_down = 1; }
    if (bmp_w == 0 || bmp_h == 0) { close(fd); return; }

    uint32_t stride = bmp_row_stride(bpp, bmp_w);
    uint32_t img_bytes = stride * (uint32_t)bmp_h;

    uint8_t *raw = (uint8_t *)malloc(img_bytes);
    if (!raw) { close(fd); return; }

    /* Seek to pixel data: pozícionálás (seek syscall) */
    seek(fd, px_off);
    /* Olvassuk be a pixel adatokat blokkokban */
    uint32_t total = 0;
    while (total < img_bytes) {
        uint32_t chunk = img_bytes - total;
        if (chunk > 4096) chunk = 4096;
        int r = read(fd, (char *)raw + total, chunk);
        if (r <= 0) break;
        total += (uint32_t)r;
    }
    close(fd);
    if (total < img_bytes) { free(raw); return; }

    /* Allokálás ha nincs még */
    if (!g_wallpaper_buf) {
        g_wallpaper_buf = (uint32_t *)malloc(scr_w * scr_h * 4);
        if (!g_wallpaper_buf) { free(raw); return; }
    }

    /* Nearest-neighbor méretezés a képernyőre */
    for (uint32_t sy = 0; sy < scr_h; sy++) {
        int src_y = (int)((uint64_t)sy * (uint32_t)bmp_h / scr_h);
        /* BMP alapból alulról felfelé tárol (ha top_down=0) */
        int real_y = top_down ? src_y : (bmp_h - 1 - src_y);
        const uint8_t *src_row = raw + (uint64_t)real_y * stride;

        uint32_t *dst_row = g_wallpaper_buf + sy * scr_w;
        for (uint32_t sx = 0; sx < scr_w; sx++) {
            int src_x = (int)((uint64_t)sx * (uint32_t)bmp_w / scr_w);
            const uint8_t *px;
            if (bpp == 24) {
                px = src_row + src_x * 3;
                /* BMP: BGR sorrendben tárolja! */
                dst_row[sx] = ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | px[0];
            } else {
                px = src_row + src_x * 4;
                dst_row[sx] = ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | px[0];
            }
        }
    }

    free(raw);
    g_wallpaper_ok = 1;
}

/* --- Front buffer flush ------------------------------------------------- */

static void flush_rect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)scr_w) w = (int)scr_w - x;
    if (y + h > (int)scr_h) h = (int)scr_h - y;
    if (w <= 0 || h <= 0) return;
    for (int dy = 0; dy < h; dy++) {
        uint32_t *src = backbuf + (uint64_t)(y + dy) * scr_w + x;
        uint8_t  *dst_byte = (uint8_t *)fb + (uint64_t)(y + dy) * pitch + x * 4;
        uint32_t *dst = (uint32_t *)dst_byte;
        for (int dx = 0; dx < w; dx++) dst[dx] = src[dx];
    }
}

/* --- Egér kurzor (közvetlen FB-re rajzol az utolsó lépésben) ------------- */

#define CURSOR_W 12
#define CURSOR_H 16
static const uint8_t cursor_bm[CURSOR_H][CURSOR_W] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},{2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},{2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},{2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},{2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},{2,1,1,1,1,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,2,2,2,2,2,0},{2,1,1,2,1,1,2,0,0,0,0,0},
    {2,1,2,0,2,1,1,2,0,0,0,0},{2,2,0,0,2,1,1,2,0,0,0,0},
    {2,0,0,0,0,2,1,1,2,0,0,0},{0,0,0,0,0,2,2,2,0,0,0,0},
};

static void draw_cursor_to_fb(uint32_t mx, uint32_t my) {
    for (int y = 0; y < CURSOR_H; y++) {
        if ((uint32_t)(my + y) >= scr_h) break;
        uint8_t  *dst_byte = (uint8_t *)fb + (uint64_t)(my + y) * pitch + mx * 4;
        uint32_t *dst = (uint32_t *)dst_byte;
        for (int x = 0; x < CURSOR_W; x++) {
            if ((uint32_t)(mx + x) >= scr_w) break;
            uint8_t v = cursor_bm[y][x];
            if (v == 1) dst[x] = 0xFFFFFF;
            else if (v == 2) dst[x] = 0x000000;
        }
    }
}

/* --- Általános helper-függvények ---------------------------------------- */

static int sstrcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}
static int sstrcmp_n(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] || a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}
static int sstrlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void sstrcpy(char *d, const char *s) { while ((*d++ = *s++)); }
static void sstrcat(char *d, const char *s) {
    while (*d) d++;
    while ((*d++ = *s++));
}

static void utoa10(uint64_t v, char *out) {
    char tmp[32]; int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    int o = 0;
    while (i) out[o++] = tmp[--i];
    out[o] = 0;
}

static void itoa10s(int64_t v, char *out) {
    if (v < 0) { out[0] = '-'; utoa10((uint64_t)(-v), out + 1); return; }
    utoa10((uint64_t)v, out);
}

/* Pad zero formátum: 2 számjegy */
static void pad2(char *out, int v) {
    out[0] = '0' + (v / 10) % 10;
    out[1] = '0' + (v % 10);
    out[2] = 0;
}

/* --- Window framework --------------------------------------------------- */

typedef enum {
    APP_FILES = 1,
    APP_EDITOR,
    APP_CALC,
    APP_SYSINFO,
    APP_CLOCK,
    APP_TERMINAL,
    APP_ABOUT,
    APP_HARDWARE,
    APP_INSTALLER,
    APP_SNAKE,
    APP_BROWSER,
} app_kind_t;

typedef struct window window_t;

typedef void (*draw_fn)(window_t *w);
typedef void (*event_fn)(window_t *w, int x, int y, int btn);
typedef void (*key_fn)(window_t *w, char key);

#define APP_PRIV_SIZE 4096

struct window {
    int       x, y, w, h;
    int       open;
    app_kind_t app;
    char      title[48];
    draw_fn   draw;
    event_fn  click;
    key_fn    key;
    /* Alkalmazás-specifikus állapot (max 4 KB) */
    char      priv[APP_PRIV_SIZE];
};

static window_t g_windows[MAX_WINDOWS];
static int      g_window_count = 0;
static int      g_focus = -1;

/* Drag állapot */
static int g_drag_idx = -1;
static int g_drag_dx, g_drag_dy;

/* Ikon hover animáció: per-ikon fényesség (0..220), lassú fade */
static uint8_t g_icon_hover_alpha[16]; /* max DT_ICON_COUNT */
#define HOVER_FADE_IN  35
#define HOVER_FADE_OUT 25
#define HOVER_MAX      200

/* Mouse edge-detection */
static uint32_t g_prev_btn = 0;
static uint32_t g_prev_mx  = 0, g_prev_my = 0;

/* Start menu állapot */
static int g_start_menu_open = 0;

/* Leállítás kérés flag */
static int g_shutdown_requested = 0;

/* --- Power ikon pixel-art rajzolás (9×10 px) ---------------------------- */
static void bb_power_icon(int ox, int oy, uint32_t c) {
    /* Körív (felső rés nélkül) */
    put_bb(ox+1,oy+3,c); put_bb(ox+7,oy+3,c);
    put_bb(ox+0,oy+4,c); put_bb(ox+8,oy+4,c);
    put_bb(ox+0,oy+5,c); put_bb(ox+8,oy+5,c);
    put_bb(ox+0,oy+6,c); put_bb(ox+8,oy+6,c);
    put_bb(ox+1,oy+7,c); put_bb(ox+7,oy+7,c);
    put_bb(ox+2,oy+8,c); put_bb(ox+3,oy+8,c);
    put_bb(ox+4,oy+8,c); put_bb(ox+5,oy+8,c); put_bb(ox+6,oy+8,c);
    put_bb(ox+2,oy+2,c); put_bb(ox+6,oy+2,c);
    /* Függőleges vonal felülről */
    put_bb(ox+4,oy+0,c); put_bb(ox+4,oy+1,c);
    put_bb(ox+4,oy+2,c); put_bb(ox+4,oy+3,c);
    put_bb(ox+4,oy+4,c);
}

/* --- Window kezelők ----------------------------------------------------- */

static window_t *window_find_by_app(app_kind_t app) {
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].open && g_windows[i].app == app) return &g_windows[i];
    }
    return NULL;
}

static int window_new(app_kind_t app, const char *title, int w, int h,
                      draw_fn dr, event_fn ev, key_fn ky) {
    /* Singleton: ha már nyitva van ugyanez az app, csak fokuszáljuk. */
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].open && g_windows[i].app == app) {
            g_focus = i;
            return i;
        }
    }

    /* Slotkeresés: először zárt (újrahasznosítható) slotot keresünk,
     * hogy ne nőjön a g_window_count a végtelenségig. */
    int slot = -1;
    for (int i = 0; i < g_window_count; i++) {
        if (!g_windows[i].open) { slot = i; break; }
    }
    if (slot < 0) {
        /* Nincs szabad zárt slot: bővítjük a tömböt. */
        if (g_window_count >= MAX_WINDOWS) return -1;
        slot = g_window_count++;
    }

    window_t *win = &g_windows[slot];
    win->open = 1;
    win->app  = app;
    win->w = w; win->h = h;
    /* Kaszkád pozíció az aktív ablakok száma alapján */
    int active = 0;
    for (int i = 0; i < g_window_count; i++) if (g_windows[i].open) active++;
    win->x = 60 + (active * 28) % 280;
    win->y = 44 + (active * 24) % 180;
    win->draw = dr; win->click = ev; win->key = ky;
    int j = 0;
    while (title[j] && j < 47) { win->title[j] = title[j]; j++; }
    win->title[j] = 0;
    for (int k = 0; k < APP_PRIV_SIZE; k++) win->priv[k] = 0;
    g_focus = slot;
    return slot;
}

static void window_close(int idx) {
    if (idx < 0 || idx >= g_window_count) return;
    g_windows[idx].open = 0;
    if (g_focus == idx) {
        g_focus = -1;
        for (int i = g_window_count - 1; i >= 0; i--) {
            if (g_windows[i].open) { g_focus = i; break; }
        }
    }
}

static int window_hit_titlebar(window_t *w, int mx, int my) {
    return (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + TITLE_H);
}

static int window_hit_close(window_t *w, int mx, int my) {
    /* draw_window_frame: bb_fill_rounded_rect(x+w-22, y+5, 17, 15, ...) */
    int bx = w->x + w->w - 22;
    int by = w->y + 5;
    return (mx >= bx && mx < bx + 17 && my >= by && my < by + 15);
}

static int window_hit_client(window_t *w, int mx, int my) {
    return (mx >= w->x && mx < w->x + w->w &&
            my >= w->y + TITLE_H && my < w->y + w->h);
}

static void draw_window_frame(window_t *win, int focused) {
    int x = win->x, y = win->y, w = win->w, h = win->h;
    int r = 8; /* lekerekítési sugár */

    /* Dirty mark: az egész ablak területe + árnyék */
    dirty_mark(x, y, w + 6, h + 6);

    /* Árnyék (vékony, jobb és alsó él) */
    bb_draw_shadow(x, y, w, h);

    /* Ablak háttér: lekerekített sarokkal */
    bb_fill_rounded_rect(x, y, w, h, r, COLOR_WIN_BG);

    /* Titlebar: gradiens (aktív = kék, inaktív = sötétszürke) */
    if (focused) {
        bb_fill_gradient_v(x, y, w, TITLE_H, COLOR_WIN_TITLE_A, COLOR_WIN_TITLE_A2);
    } else {
        bb_fill_rounded_rect(x, y, w, TITLE_H, r, COLOR_WIN_TITLE);
        /* Kiegészítés: az alsó sarkok ne legyenek lekerekítve (titlebar közép) */
        bb_fill_rect(x, y + r, w, TITLE_H - r, COLOR_WIN_TITLE);
    }

    /* Titlebar szöveg */
    int tlen = sstrlen(win->title);
    (void)tlen;
    bb_draw_text(x + 12, y + 7, win->title, COLOR_TEXT, 2);

    /* Bezáró gomb: lekerekített piros pill */
    int bx = x + w - 22, by = y + 5;
    bb_fill_rounded_rect(bx, by, 17, 15, 7, COLOR_ACCENT);
    bb_draw_text(bx + 5, by + 4, "x", 0xFFFFFF, 1);

    /* Elválasztó vonal titlebar alatt */
    bb_hline(x, y + TITLE_H, w, focused ? COLOR_FRAME_A : COLOR_FRAME);

    /* Ablak keret (1px) */
    uint32_t frame_c = focused ? COLOR_FRAME_A : COLOR_FRAME;
    /* Csak 4 oldal, lekerekített saroknál a keret a kör köré kerül - egyszerűsítve: */
    bb_hline(x + r, y,         w - 2 * r, frame_c);
    bb_hline(x + r, y + h - 1, w - 2 * r, frame_c);
    bb_vline(x,         y + r, h - 2 * r, frame_c);
    bb_vline(x + w - 1, y + r, h - 2 * r, frame_c);
}

/* --- VFS olvasó segéd: egy fájl tartalmát kmalloc-olt bufferbe olvassa --- */

static int read_file_into(const char *path, char *out, int max_len) {
    int fd = open(path);
    if (fd < 0) return -1;
    int total = 0;
    char buf[256];
    int r;
    while ((r = read(fd, buf, 256)) > 0 && total < max_len - 1) {
        int take = r;
        if (total + take >= max_len - 1) take = max_len - 1 - total;
        for (int i = 0; i < take; i++) out[total + i] = buf[i];
        total += take;
    }
    out[total] = 0;
    return total;
}

/* =============================================================================
 *  APP: ABOUT
 * ========================================================================== */

static void app_about_draw(window_t *w) {
    int cx = w->x + 20, cy = w->y + TITLE_H + 16;
    bb_draw_text(cx, cy, "RexOS v0.2.0-alpha", 0x9FE0FF, 3); cy += 28;
    bb_draw_text(cx, cy, "A hobby x86_64 operating system", COLOR_TEXT, 1); cy += 14;
    bb_draw_text(cx, cy, "written from scratch in C.",       COLOR_TEXT, 1); cy += 24;

    bb_draw_text(cx, cy, "Features:",                COLOR_ACCENT, 2); cy += 22;
    const char *feats[] = {
        " * x86_64 long mode kernel",
        " * Preemptive scheduler + user mode (Ring 3)",
        " * Virtual memory with per-process PML4",
        " * Kernel heap (kmalloc/kfree, coalescing)",
        " * ELF loader, syscalls, libc",
        " * PS/2 keyboard + mouse drivers",
        " * Framebuffer console + this desktop",
        " * VFS with mount points",
        " * PCI bus enumeration",
        " * AHCI SATA + legacy ATA PIO (block layer)",
        " * FAT32 read-only fs (initrd + disk)",
        NULL,
    };
    for (int i = 0; feats[i]; i++) {
        bb_draw_text(cx, cy, feats[i], COLOR_TEXT_DIM, 1);
        cy += 14;
    }
    cy += 8;
    bb_draw_text(cx, cy, "Press the close button or ESC to exit.", COLOR_TEXT_DIM, 1);
}

/* =============================================================================
 *  APP: HARDWARE  (PCI + block device lista)
 * ========================================================================== */

static const char *pci_class_label(uint8_t c, uint8_t s) {
    switch (c) {
        case 0x01:
            switch (s) {
                case 0x01: return "IDE";
                case 0x06: return "SATA AHCI";
                case 0x08: return "NVMe";
                default:   return "Storage";
            }
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x06:
            switch (s) {
                case 0x00: return "Host bridge";
                case 0x01: return "ISA bridge";
                case 0x04: return "PCI bridge";
                default:   return "Bridge";
            }
        case 0x0C:
            switch (s) {
                case 0x03: return "USB";
                default:   return "Serial bus";
            }
        default: return "Other";
    }
}

static void hex4(uint16_t v, char *out) {
    const char *H = "0123456789ABCDEF";
    out[0] = H[(v >> 12) & 0xF];
    out[1] = H[(v >>  8) & 0xF];
    out[2] = H[(v >>  4) & 0xF];
    out[3] = H[ v        & 0xF];
    out[4] = 0;
}
static void hex2(uint8_t v, char *out) {
    const char *H = "0123456789ABCDEF";
    out[0] = H[(v >> 4) & 0xF];
    out[1] = H[ v       & 0xF];
    out[2] = 0;
}

static void app_hardware_draw(window_t *w) {
    int cx = w->x + 14, cy = w->y + TITLE_H + 12;
    bb_draw_text(cx, cy, "Hardware Inventory", 0x9FE0FF, 2); cy += 24;

    bb_draw_text(cx, cy, "Block devices", COLOR_ACCENT, 1); cy += 14;
    int nblk = block_dev_count();
    if (nblk == 0) {
        bb_draw_text(cx + 10, cy, "(none)", COLOR_TEXT_DIM, 1); cy += 14;
    }
    for (int i = 0; i < nblk; i++) {
        block_info_t bi;
        if (block_dev_info(i, &bi) != 0) continue;
        char line[96];
        sstrcpy(line, "  ");
        sstrcat(line, bi.name);
        sstrcat(line, "  ");
        char nb[24];
        uint64_t mb = (bi.sector_count * bi.sector_size) / (1024 * 1024);
        utoa10(mb, nb);
        sstrcat(line, nb);
        sstrcat(line, " MB  ");
        sstrcat(line, bi.writable ? "[RW]" : "[RO]");
        bb_draw_text(cx + 6, cy, line, COLOR_TEXT, 1); cy += 13;
    }

    cy += 6;
    bb_draw_text(cx, cy, "PCI devices", COLOR_ACCENT, 1); cy += 14;
    int npci = pci_dev_count();
    for (int i = 0; i < npci; i++) {
        pci_info_t pi;
        if (pci_dev_info(i, &pi) != 0) continue;

        char line[128];
        char s[8];
        sstrcpy(line, "  ");
        hex2(pi.bus,  s); sstrcat(line, s); sstrcat(line, ":");
        hex2(pi.dev,  s); sstrcat(line, s); sstrcat(line, ".");
        s[0] = '0' + (pi.func & 0x7); s[1] = 0;
        sstrcat(line, s);
        sstrcat(line, "  ");
        hex4(pi.vendor, s); sstrcat(line, s); sstrcat(line, ":");
        hex4(pi.device, s); sstrcat(line, s);
        sstrcat(line, "  ");
        sstrcat(line, pci_class_label(pi.class_code, pi.subclass));
        bb_draw_text(cx + 6, cy, line, COLOR_TEXT_DIM, 1); cy += 13;
        if (cy > w->y + w->h - 20) break;
    }
}

/* =============================================================================
 *  APP: INSTALLER  (MVP: elérhető target-ek és placeholder)
 * ========================================================================== */

typedef struct {
    int selected;         /* kiválasztott block device index */
    int confirm_stage;    /* 0 = normál, 1 = megerősítés */
    int status_msg;       /* 0 = nincs; 1 = ok; 2 = hiba */
    int btn_install_x, btn_install_y, btn_install_w, btn_install_h;
    int dev_list_x, dev_list_y, dev_list_w, dev_row_h;
} installer_state_t;

static void app_installer_draw(window_t *w) {
    installer_state_t *st = (installer_state_t *)w->priv;

    int cx = w->x + 20, cy = w->y + TITLE_H + 14;
    bb_draw_text(cx, cy, "RexOS Installer", 0x9FE0FF, 2); cy += 24;
    bb_draw_text(cx, cy,
        "Select a target disk. The installer will create a FAT32",
        COLOR_TEXT_DIM, 1); cy += 13;
    bb_draw_text(cx, cy,
        "partition and copy the kernel + initrd + bootloader.",
        COLOR_TEXT_DIM, 1); cy += 18;

    bb_draw_text(cx, cy, "Available disks:", COLOR_ACCENT, 1); cy += 16;

    st->dev_list_x = cx + 6;
    st->dev_list_y = cy;
    st->dev_list_w = w->w - 50;
    st->dev_row_h  = 20;

    int nblk = block_dev_count();
    if (nblk == 0) {
        bb_draw_text(cx + 10, cy, "(no block devices detected)",
                     COLOR_TEXT_DIM, 1);
        cy += 20;
    }
    for (int i = 0; i < nblk; i++) {
        block_info_t bi;
        if (block_dev_info(i, &bi) != 0) continue;

        uint32_t bg = (i == st->selected) ? 0x2A4A78 : 0x1F2A40;
        bb_fill_rect(st->dev_list_x, cy - 2, st->dev_list_w, st->dev_row_h - 2, bg);

        char line[96];
        sstrcpy(line, "  ");
        sstrcat(line, bi.name);
        sstrcat(line, "   ");
        char nb[24];
        uint64_t mb = (bi.sector_count * bi.sector_size) / (1024 * 1024);
        utoa10(mb, nb);
        sstrcat(line, nb);
        sstrcat(line, " MB   ");
        sstrcat(line, bi.writable ? "[writable]" : "[READ-ONLY - cannot install]");
        bb_draw_text(st->dev_list_x + 4, cy + 2, line, COLOR_TEXT, 1);
        cy += st->dev_row_h;
    }

    cy += 16;
    bb_hline(cx, cy, w->w - 40, COLOR_FRAME); cy += 10;

    /* Install gomb */
    st->btn_install_x = cx;
    st->btn_install_y = cy;
    st->btn_install_w = 160;
    st->btn_install_h = 28;

    uint32_t btn_bg = 0x2E7D32;
    if (st->confirm_stage == 1) btn_bg = 0xC62828;
    bb_fill_rect(st->btn_install_x, st->btn_install_y,
                 st->btn_install_w, st->btn_install_h, btn_bg);
    bb_frame(st->btn_install_x, st->btn_install_y,
             st->btn_install_w, st->btn_install_h, COLOR_FRAME);

    const char *lbl = (st->confirm_stage == 1)
        ? "Click to CONFIRM"
        : "Install RexOS";
    bb_draw_text(st->btn_install_x + 18, st->btn_install_y + 8,
                 lbl, 0xFFFFFF, 1);

    cy += 36;

    /* Státusz */
    if (st->status_msg == 1) {
        bb_draw_text(cx, cy,
                     "NOTE: disk writes require FAT32 write support",
                     0xFFD54F, 1); cy += 13;
        bb_draw_text(cx, cy,
                     "(planned in next iteration). Install stubbed.",
                     0xFFD54F, 1);
    } else if (st->status_msg == 2) {
        bb_draw_text(cx, cy, "ERROR: no writable target available.",
                     0xFF6B6B, 1);
    } else {
        bb_draw_text(cx, cy,
                     "Tip: USB & persistent storage support coming soon.",
                     COLOR_TEXT_DIM, 1);
    }
}

static void app_installer_event(window_t *w, int x, int y, int btn) {
    if (!(btn & 1)) return;
    installer_state_t *st = (installer_state_t *)w->priv;

    /* Device list kattintás */
    if (x >= st->dev_list_x && x < st->dev_list_x + st->dev_list_w) {
        int dy = y - st->dev_list_y;
        if (dy >= 0) {
            int idx = dy / st->dev_row_h;
            if (idx >= 0 && idx < block_dev_count()) {
                st->selected = idx;
                st->confirm_stage = 0;
                st->status_msg = 0;
                return;
            }
        }
    }

    /* Install gomb */
    if (x >= st->btn_install_x && x < st->btn_install_x + st->btn_install_w &&
        y >= st->btn_install_y && y < st->btn_install_y + st->btn_install_h) {
        block_info_t bi;
        if (block_dev_info(st->selected, &bi) != 0 || !bi.writable) {
            st->status_msg = 2;
            st->confirm_stage = 0;
            return;
        }
        if (st->confirm_stage == 0) {
            st->confirm_stage = 1;
            st->status_msg = 0;
        } else {
            /* MVP: FAT32 write még nincs implementálva — placeholder */
            st->confirm_stage = 0;
            st->status_msg = 1;
        }
    }
}

/* =============================================================================
 *  APP: SYSINFO  (frissül időben)
 * ========================================================================== */

static void app_sysinfo_draw(window_t *w) {
    int cx = w->x + 20, cy = w->y + TITLE_H + 14;
    char buf[64];

    bb_draw_text(cx, cy, "System Information", 0x9FE0FF, 2); cy += 22;
    bb_hline(cx, cy, w->w - 40, COLOR_FRAME); cy += 8;

    bb_draw_text(cx, cy, "OS:", COLOR_TEXT_DIM, 1);
    bb_draw_text(cx + 90, cy, "RexOS 0.2.0-alpha", COLOR_TEXT, 1); cy += 14;

    bb_draw_text(cx, cy, "Arch:", COLOR_TEXT_DIM, 1);
    bb_draw_text(cx + 90, cy, "x86_64 (AMD64)", COLOR_TEXT, 1); cy += 14;

    bb_draw_text(cx, cy, "Display:", COLOR_TEXT_DIM, 1);
    utoa10(scr_w, buf); int o = sstrlen(buf);
    buf[o++] = 'x';
    utoa10(scr_h, buf + o);
    bb_draw_text(cx + 90, cy, buf, COLOR_TEXT, 1); cy += 14;

    uint64_t ticks = get_ticks();
    uint64_t sec = ticks / 100;
    bb_draw_text(cx, cy, "Uptime:", COLOR_TEXT_DIM, 1);
    char up[64];
    char p[8];
    pad2(p, (int)((sec / 3600) % 24)); sstrcpy(up, p); sstrcat(up, ":");
    pad2(p, (int)((sec / 60) % 60));   sstrcat(up, p); sstrcat(up, ":");
    pad2(p, (int)(sec % 60));          sstrcat(up, p);
    sstrcat(up, "   ("); utoa10(sec, p); sstrcat(up, p); sstrcat(up, " s)");
    bb_draw_text(cx + 90, cy, up, COLOR_TEXT, 1); cy += 14;

    bb_draw_text(cx, cy, "Ticks:", COLOR_TEXT_DIM, 1);
    utoa10(ticks, buf);
    bb_draw_text(cx + 90, cy, buf, COLOR_TEXT, 1); cy += 14;

    cy += 6;
    bb_hline(cx, cy, w->w - 40, COLOR_FRAME); cy += 8;
    bb_draw_text(cx, cy, "Storage", 0x9FE0FF, 2); cy += 22;

    bb_draw_text(cx, cy, "Initrd /", COLOR_TEXT_DIM, 1);
    bb_draw_text(cx + 90, cy, "tarfs (read-only)", COLOR_TEXT, 1); cy += 14;

    int nblk = block_dev_count();
    int npci = pci_dev_count();

    char nb[32];
    bb_draw_text(cx, cy, "Block devices:", COLOR_TEXT_DIM, 1);
    utoa10((uint64_t)nblk, nb);
    bb_draw_text(cx + 110, cy, nb, COLOR_TEXT, 1); cy += 14;

    bb_draw_text(cx, cy, "PCI devices:", COLOR_TEXT_DIM, 1);
    utoa10((uint64_t)npci, nb);
    bb_draw_text(cx + 110, cy, nb, COLOR_TEXT, 1); cy += 14;

    if (nblk > 0) {
        block_info_t bi;
        if (block_dev_info(0, &bi) == 0) {
            char line[80];
            sstrcpy(line, "  -> ");
            sstrcat(line, bi.name);
            sstrcat(line, " (");
            uint64_t mb = (bi.sector_count * bi.sector_size) / (1024 * 1024);
            utoa10(mb, nb);
            sstrcat(line, nb);
            sstrcat(line, " MB, ");
            sstrcat(line, bi.writable ? "RW" : "RO");
            sstrcat(line, ")");
            bb_draw_text(cx, cy, line, COLOR_TEXT_DIM, 1); cy += 14;
        }
    }

    cy += 6;
    bb_hline(cx, cy, w->w - 40, COLOR_FRAME); cy += 8;
    bb_draw_text(cx, cy, "Tip: Open 'Hardware' for PCI/disk details,",
                 COLOR_TEXT_DIM, 1); cy += 14;
    bb_draw_text(cx, cy, "or 'Installer' to install RexOS to disk.",
                 COLOR_TEXT_DIM, 1);
}

/* =============================================================================
 *  APP: CLOCK
 * ========================================================================== */

static const uint8_t big_digits[10][5] = {
    /* 5 oszlop, 7 sor */
    {0x3E,0x51,0x49,0x45,0x3E},
    {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},
    {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1E},
};

static void draw_huge_digit(int x, int y, int d, uint32_t color, int scale) {
    if (d < 0 || d > 9) return;
    for (int col = 0; col < 5; col++) {
        uint8_t bits = big_digits[d][col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                bb_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_huge_colon(int x, int y, uint32_t color, int scale) {
    bb_fill_rect(x, y + 2 * scale, scale, scale, color);
    bb_fill_rect(x, y + 4 * scale, scale, scale, color);
}

static void app_clock_draw(window_t *w) {
    uint64_t ticks = get_ticks();
    uint64_t sec = ticks / 100;
    int hr = (int)((sec / 3600) % 24);
    int mn = (int)((sec / 60) % 60);
    int sc = (int)(sec % 60);

    int scale = 8;
    int digit_w = 5 * scale;
    int colon_w = scale * 2;
    int total_w = digit_w*6 + colon_w*2 + scale*5;
    int cx = w->x + (w->w - total_w) / 2;
    int cy = w->y + TITLE_H + (w->h - TITLE_H - 7 * scale) / 2;

    uint32_t col = 0x9FE0FF;
    draw_huge_digit(cx, cy, hr / 10, col, scale); cx += digit_w + scale;
    draw_huge_digit(cx, cy, hr % 10, col, scale); cx += digit_w + scale;
    draw_huge_colon(cx, cy, col, scale);          cx += colon_w + scale;
    draw_huge_digit(cx, cy, mn / 10, col, scale); cx += digit_w + scale;
    draw_huge_digit(cx, cy, mn % 10, col, scale); cx += digit_w + scale;
    draw_huge_colon(cx, cy, col, scale);          cx += colon_w + scale;
    draw_huge_digit(cx, cy, sc / 10, col, scale); cx += digit_w + scale;
    draw_huge_digit(cx, cy, sc % 10, col, scale);

    bb_draw_text(w->x + 20, w->y + w->h - 30,
                 "Uptime clock - tics-from-boot, 100Hz PIT",
                 COLOR_TEXT_DIM, 1);
}

/* =============================================================================
 *  APP: CALCULATOR
 * ========================================================================== */

typedef struct {
    char     display[24];
    int64_t  acc;
    int64_t  cur;
    char     op;        /* '+', '-', '*', '/' vagy 0 */
    int      fresh;     /* 1 = következő számjegyhez új cur */
    int      hover_btn; /* mely gomb fölött az egér */
} calc_state_t;

static const char *calc_btn_labels[20] = {
    "7","8","9","/",
    "4","5","6","*",
    "1","2","3","-",
    "0","C","=","+",
    "BS","","",""
};

static int calc_btn_grid_y(window_t *w, int idx) {
    return w->y + TITLE_H + 70 + (idx / 4) * 44;
}
static int calc_btn_grid_x(window_t *w, int idx) {
    return w->x + 20 + (idx % 4) * 56;
}

static void calc_update_display(calc_state_t *s) {
    itoa10s(s->cur, s->display);
}

static void app_calc_draw(window_t *w) {
    calc_state_t *s = (calc_state_t *)w->priv;
    if (!s->display[0]) sstrcpy(s->display, "0");

    /* Kijelző */
    int dx = w->x + 16, dy = w->y + TITLE_H + 14;
    bb_fill_rect(dx, dy, w->w - 32, 44, COLOR_FIELD);
    bb_frame(dx, dy, w->w - 32, 44, COLOR_FRAME);
    int len = sstrlen(s->display);
    int tw = len * 6 * 3;
    int textx = dx + (w->w - 32 - tw) - 8;
    bb_draw_text(textx, dy + 12, s->display, 0x9FE0FF, 3);

    if (s->op) {
        char opb[2] = { s->op, 0 };
        bb_draw_text(dx + 8, dy + 4, opb, COLOR_ACCENT, 2);
        char abuf[24];
        itoa10s(s->acc, abuf);
        bb_draw_text(dx + 24, dy + 4, abuf, COLOR_TEXT_DIM, 1);
    }

    /* Gombok 4x4 + BS */
    for (int i = 0; i < 17; i++) {
        const char *lab = calc_btn_labels[i];
        if (!lab[0]) continue;
        int bx = calc_btn_grid_x(w, i);
        int by = calc_btn_grid_y(w, i);
        int bw = 48, bh = 36;
        uint32_t bg = COLOR_BUTTON;
        uint32_t fg = COLOR_TEXT;
        if (lab[0] == '+' || lab[0] == '-' || lab[0] == '*' || lab[0] == '/') {
            bg = 0x3E2E58; fg = 0xFFD080;
        }
        if (lab[0] == '=') { bg = COLOR_ACCENT; fg = 0xFFFFFF; }
        if (lab[0] == 'C') { bg = COLOR_ACCENT_DIM; fg = 0xFFFFFF; }
        if (s->hover_btn == i) bg = COLOR_BUTTON_HOV;
        bb_fill_rect(bx, by, bw, bh, bg);
        bb_frame(bx, by, bw, bh, COLOR_FRAME);
        int lw = sstrlen(lab) * 6 * 2 - 2;
        bb_draw_text(bx + (bw - lw) / 2, by + 11, lab, fg, 2);
    }
}

static void calc_press(window_t *w, int idx) {
    calc_state_t *s = (calc_state_t *)w->priv;
    const char *lab = calc_btn_labels[idx];
    if (!lab[0]) return;
    char c = lab[0];

    if (c >= '0' && c <= '9') {
        if (s->fresh) { s->cur = 0; s->fresh = 0; }
        if (s->cur > 999999999999LL) return;
        if (s->cur < 0) {
            s->cur = s->cur * 10 - (c - '0');
        } else {
            s->cur = s->cur * 10 + (c - '0');
        }
        calc_update_display(s);
        return;
    }
    if (c == 'C') {
        s->acc = 0; s->cur = 0; s->op = 0; s->fresh = 0;
        calc_update_display(s);
        return;
    }
    if (c == 'B') { /* BS */
        s->cur = s->cur / 10;
        calc_update_display(s);
        return;
    }
    if (c == '+' || c == '-' || c == '*' || c == '/') {
        if (s->op == 0) { s->acc = s->cur; }
        else {
            /* alkalmazzuk az előzőt */
            if      (s->op == '+') s->acc = s->acc + s->cur;
            else if (s->op == '-') s->acc = s->acc - s->cur;
            else if (s->op == '*') s->acc = s->acc * s->cur;
            else if (s->op == '/') s->acc = s->cur ? s->acc / s->cur : 0;
        }
        s->op = c;
        s->cur = 0;
        s->fresh = 1;
        calc_update_display(s);
        sstrcpy(s->display, "0");
        return;
    }
    if (c == '=') {
        if (s->op == '+') s->acc = s->acc + s->cur;
        if (s->op == '-') s->acc = s->acc - s->cur;
        if (s->op == '*') s->acc = s->acc * s->cur;
        if (s->op == '/') s->acc = s->cur ? s->acc / s->cur : 0;
        s->cur = s->acc;
        s->op = 0;
        s->acc = 0;
        s->fresh = 1;
        calc_update_display(s);
        return;
    }
}

static void app_calc_click(window_t *w, int mx, int my, int btn) {
    if (!btn) return;
    calc_state_t *s = (calc_state_t *)w->priv;
    s->hover_btn = -1;
    for (int i = 0; i < 17; i++) {
        if (!calc_btn_labels[i][0]) continue;
        int bx = calc_btn_grid_x(w, i);
        int by = calc_btn_grid_y(w, i);
        if (mx >= bx && mx < bx + 48 && my >= by && my < by + 36) {
            calc_press(w, i);
            return;
        }
    }
}

static void app_calc_key(window_t *w, char c) {
    int idx = -1;
    switch (c) {
        case '7': idx = 0; break; case '8': idx = 1; break;
        case '9': idx = 2; break; case '/': idx = 3; break;
        case '4': idx = 4; break; case '5': idx = 5; break;
        case '6': idx = 6; break; case '*': idx = 7; break;
        case '1': idx = 8; break; case '2': idx = 9; break;
        case '3': idx = 10; break; case '-': idx = 11; break;
        case '0': idx = 12; break; case 'c': case 'C': idx = 13; break;
        case '=': case '\n': idx = 14; break;
        case '+': idx = 15; break;
        case '\b': idx = 16; break;
    }
    if (idx >= 0) calc_press(w, idx);
}

/* =============================================================================
 *  APP: FILES  (fájlkezelő initrd + /mnt)
 * ========================================================================== */

typedef struct {
    char  cwd[128];           /* aktuális könyvtár "/" vagy "/mnt" stb. */
    char  entries[64][64];    /* max 64 bejegyzés a könyvtárban */
    int   is_dir[64];         /* heuristika: ha név nincs .-pontnal vagy /mnt-en mappa */
    int   entry_count;
    int   scroll;
    int   selected;
    char  status[80];
    int   loaded;
} files_state_t;

static void files_load(files_state_t *s) {
    s->entry_count = 0;
    s->selected = -1;
    s->scroll = 0;
    int fd = open(s->cwd);
    if (fd < 0) {
        sstrcpy(s->status, "Cannot open: ");
        sstrcat(s->status, s->cwd);
        s->loaded = 1;
        return;
    }
    dirent_t d;
    while (s->entry_count < 64 && getdents(fd, &d) > 0) {
        int j = 0;
        while (d.name[j] && j < 63) {
            s->entries[s->entry_count][j] = d.name[j];
            j++;
        }
        s->entries[s->entry_count][j] = 0;
        /* heuristika: ha kiterjesztés nélküli név -> mappa, kivéve gyökérben ahol felülírjuk */
        int has_dot = 0;
        for (int k = 0; d.name[k]; k++) if (d.name[k] == '.') has_dot = 1;
        s->is_dir[s->entry_count] = !has_dot;
        /* mountpoint hint */
        if (sstrcmp(d.name, "mnt") == 0) s->is_dir[s->entry_count] = 1;
        s->entry_count++;
    }
    sstrcpy(s->status, "Loaded ");
    char nb[16]; utoa10(s->entry_count, nb);
    sstrcat(s->status, nb);
    sstrcat(s->status, " entries from ");
    sstrcat(s->status, s->cwd);
    s->loaded = 1;
}

static void files_init(files_state_t *s) {
    sstrcpy(s->cwd, "/");
    files_load(s);
}

/* Útvonal: cwd + "/" + name (vagy ha cwd már "/" akkor csak / + name) */
static void path_join(char *out, const char *cwd, const char *name) {
    sstrcpy(out, cwd);
    int l = sstrlen(out);
    if (l == 0 || out[l-1] != '/') sstrcat(out, "/");
    sstrcat(out, name);
}

/* parent dir: levágja az utolsó komponenst */
static void path_parent(char *cwd) {
    int l = sstrlen(cwd);
    if (l <= 1) { sstrcpy(cwd, "/"); return; }
    if (cwd[l-1] == '/') { cwd[l-1] = 0; l--; }
    while (l > 0 && cwd[l-1] != '/') { cwd[l-1] = 0; l--; }
    if (l > 1 && cwd[l-1] == '/') { cwd[l-1] = 0; }
    if (l == 0) sstrcpy(cwd, "/");
}

/* Forward: editor megnyitása fájllal */
static void open_editor_with_file(const char *path);

static void app_files_draw(window_t *w) {
    files_state_t *s = (files_state_t *)w->priv;
    if (!s->loaded) files_init(s);

    /* Toolbar */
    int tx = w->x + 8, ty = w->y + TITLE_H + 6;
    bb_fill_rect(tx, ty, w->w - 16, 26, COLOR_WIN_TITLE);
    bb_frame(tx, ty, w->w - 16, 26, COLOR_FRAME);

    /* "Up" gomb */
    bb_fill_rect(tx + 4, ty + 4, 40, 18, COLOR_BUTTON);
    bb_frame(tx + 4, ty + 4, 40, 18, COLOR_FRAME);
    bb_draw_text(tx + 11, ty + 8, "Up", COLOR_TEXT, 1);
    /* Refresh */
    bb_fill_rect(tx + 50, ty + 4, 64, 18, COLOR_BUTTON);
    bb_frame(tx + 50, ty + 4, 64, 18, COLOR_FRAME);
    bb_draw_text(tx + 57, ty + 8, "Refresh", COLOR_TEXT, 1);
    /* Path */
    bb_draw_text(tx + 124, ty + 9, "Path:", COLOR_TEXT_DIM, 1);
    bb_draw_text(tx + 160, ty + 9, s->cwd, 0x9FE0FF, 1);

    /* Lista */
    int lx = w->x + 8, ly = w->y + TITLE_H + 38;
    int lw = w->w - 16, lh = w->h - TITLE_H - 38 - 22;
    bb_fill_rect(lx, ly, lw, lh, COLOR_FIELD);
    bb_frame(lx, ly, lw, lh, COLOR_FRAME);

    int row_h = 22;
    int visible = lh / row_h;
    if (visible > 64) visible = 64;
    for (int i = 0; i < visible && (i + s->scroll) < s->entry_count; i++) {
        int idx = i + s->scroll;
        int ry = ly + 2 + i * row_h;
        uint32_t bg = (idx == s->selected) ? 0x404060 : COLOR_FIELD;
        bb_fill_rect(lx + 2, ry, lw - 4, row_h - 2, bg);

        /* Ikon */
        uint32_t icon = COLOR_FILE;
        if (s->is_dir[idx]) icon = COLOR_FOLDER;
        else {
            /* Kiterjesztés alapján szín */
            const char *n = s->entries[idx];
            int el = sstrlen(n);
            if (el > 4 && n[el-4] == '.' && n[el-3] == 'e' && n[el-2] == 'l' && n[el-1] == 'f')
                icon = COLOR_EXE;
            else if (el > 4 && (n[el-4] == '.') &&
                     ((n[el-3] == 't' && n[el-2] == 'x' && n[el-1] == 't') ||
                      (n[el-3] == 'T' && n[el-2] == 'X' && n[el-1] == 'T')))
                icon = COLOR_TXT;
        }
        bb_fill_rect(lx + 6, ry + 4, 12, 12, icon);
        bb_frame(lx + 6, ry + 4, 12, 12, 0x202020);
        bb_draw_text(lx + 24, ry + 6, s->entries[idx], COLOR_TEXT, 1);

        /* type label */
        const char *tlab = s->is_dir[idx] ? "<DIR>" : "file";
        bb_draw_text(lx + lw - 80, ry + 6, tlab, COLOR_TEXT_DIM, 1);
    }

    /* Status bar */
    int sy = w->y + w->h - 18;
    bb_fill_rect(w->x + 1, sy, w->w - 2, 17, 0x12121A);
    bb_draw_text(w->x + 8, sy + 5, s->status, COLOR_TEXT_DIM, 1);
}

static void app_files_click(window_t *w, int mx, int my, int btn) {
    if (!btn) return;
    files_state_t *s = (files_state_t *)w->priv;

    /* Toolbar gombok */
    int tx = w->x + 8, ty = w->y + TITLE_H + 6;
    /* Up */
    if (mx >= tx + 4 && mx < tx + 44 && my >= ty + 4 && my < ty + 22) {
        path_parent(s->cwd);
        files_load(s);
        return;
    }
    /* Refresh */
    if (mx >= tx + 50 && mx < tx + 114 && my >= ty + 4 && my < ty + 22) {
        files_load(s);
        return;
    }

    /* Lista */
    int lx = w->x + 8, ly = w->y + TITLE_H + 38;
    int lw = w->w - 16, lh = w->h - TITLE_H - 38 - 22;
    int row_h = 22;
    if (mx >= lx && mx < lx + lw && my >= ly && my < ly + lh) {
        int idx = (my - ly - 2) / row_h + s->scroll;
        if (idx >= 0 && idx < s->entry_count) {
            if (s->selected == idx) {
                /* double-click style: 2nd click = open */
                char p[160];
                path_join(p, s->cwd, s->entries[idx]);
                if (s->is_dir[idx]) {
                    sstrcpy(s->cwd, p);
                    files_load(s);
                } else {
                    int el = sstrlen(s->entries[idx]);
                    /* ELF? */
                    if (el > 4 && s->entries[idx][el-4] == '.' &&
                        s->entries[idx][el-3] == 'e' && s->entries[idx][el-2] == 'l' &&
                        s->entries[idx][el-1] == 'f') {
                        int pid = spawn(p);
                        if (pid < 0) {
                            sstrcpy(s->status, "spawn failed for ");
                            sstrcat(s->status, p);
                        } else {
                            sstrcpy(s->status, "Launched ");
                            sstrcat(s->status, p);
                        }
                    } else {
                        open_editor_with_file(p);
                    }
                }
            } else {
                s->selected = idx;
            }
        }
    }
}

/* =============================================================================
 *  APP: EDITOR / VIEWER
 * ========================================================================== */

typedef struct {
    char path[128];
    char content[3200];   /* legalább ennyi belefér a 4 KB priv-be */
    int  loaded;
    int  scroll;
    int  cursor;
    int  insert_mode;
} editor_state_t;

static editor_state_t g_editor_pending;
static int            g_editor_pending_flag = 0;

static void open_editor_with_file(const char *path) {
    sstrcpy(g_editor_pending.path, path);
    g_editor_pending_flag = 1;
}

static void app_editor_draw(window_t *w) {
    editor_state_t *s = (editor_state_t *)w->priv;
    if (!s->loaded) {
        if (s->path[0] == 0) sstrcpy(s->path, "/README.TXT");
        int n = read_file_into(s->path, s->content, 3200);
        if (n < 0) sstrcpy(s->content, "(file not found - select a file in Files)");
        s->loaded = 1;
    }

    /* Toolbar */
    int tx = w->x + 8, ty = w->y + TITLE_H + 6;
    bb_fill_rect(tx, ty, w->w - 16, 22, COLOR_WIN_TITLE);
    bb_frame(tx, ty, w->w - 16, 22, COLOR_FRAME);
    bb_draw_text(tx + 6, ty + 7, "File:", COLOR_TEXT_DIM, 1);
    bb_draw_text(tx + 44, ty + 7, s->path, 0x9FE0FF, 1);

    /* Content area */
    int cx = w->x + 8, cy = w->y + TITLE_H + 34;
    int cw = w->w - 16, ch = w->h - TITLE_H - 34 - 22;
    bb_fill_rect(cx, cy, cw, ch, COLOR_FIELD);
    bb_frame(cx, cy, cw, ch, COLOR_FRAME);

    /* Sor-tördelés + scroll */
    int line_h = 12;
    int x_start = cx + 8;
    int yy = cy + 6 - s->scroll * line_h;
    int xx = x_start;
    int max_w = cx + cw - 8;
    int i = 0;
    while (1) {
        if (i == s->cursor && yy >= cy && yy < cy + ch - 8) {
            bb_fill_rect(xx, yy, 6, 12, 0x666666);
        }
        if (!s->content[i]) break;
        char c = s->content[i];
        if (c == '\n') { yy += line_h; xx = x_start; i++; continue; }
        if (c == '\r') { i++; continue; }
        if (c == '\t') { xx += 24; i++; continue; }
        if (xx + 6 > max_w) { yy += line_h; xx = x_start; }
        if (yy >= cy && yy < cy + ch - 8 && c >= 32 && c < 127) {
            bb_draw_char(xx, yy, c, COLOR_TEXT, 1);
        }
        xx += 6;
        i++;
    }

    /* Status */
    int sy = w->y + w->h - 18;
    bb_fill_rect(w->x + 1, sy, w->w - 2, 17, 0x12121A);
    char st[64];
    sstrcpy(st, s->insert_mode ? "INSERT MODE | press ESC to exit" : "COMMAND MODE | w/a/s/d move, i insert, m save");
    bb_draw_text(w->x + 8, sy + 5, st, COLOR_TEXT_DIM, 1);
}

static void app_editor_key(window_t *w, char c) {
    editor_state_t *s = (editor_state_t *)w->priv;
    int len = sstrlen(s->content);
    if (c == 27) { s->insert_mode = 0; return; }
    if (!s->insert_mode) {
        if (c == 'i') s->insert_mode = 1;
        if (c == 'a' && s->cursor > 0) s->cursor--;
        if (c == 'd' && s->cursor < len) s->cursor++;
        if (c == 'w') {
            int p = s->cursor;
            while (p > 0 && s->content[p-1] != '\n') p--;
            if (p > 0) {
                int col = s->cursor - p;
                int prev = p - 1;
                while (prev > 0 && s->content[prev-1] != '\n') prev--;
                int new_col = 0;
                while (new_col < col && prev + new_col < p - 1) new_col++;
                s->cursor = prev + new_col;
            }
        }
        if (c == 's') {
            int p = s->cursor;
            while (p > 0 && s->content[p-1] != '\n') p--;
            int col = s->cursor - p;
            int nxt = s->cursor;
            while (nxt < len && s->content[nxt] != '\n') nxt++;
            if (nxt < len) {
                nxt++;
                int new_col = 0;
                while (new_col < col && nxt + new_col < len && s->content[nxt+new_col] != '\n') new_col++;
                s->cursor = nxt + new_col;
            }
        }
        if (c == 'k') s->scroll = (s->scroll > 0) ? s->scroll - 1 : 0;
        if (c == 'j') s->scroll++;
        if (c == 'm') {
            int fd = open_ex(s->path, O_RDWR | O_CREAT);
            if (fd >= 0) {
                write_file(fd, s->content, len);
                close(fd);
            }
        }
    } else {
        if (c == '\b') {
            if (s->cursor > 0) {
                for (int i = s->cursor - 1; i <= len; i++) s->content[i] = s->content[i + 1];
                s->cursor--;
            }
        } else if (c == '\n' || (c >= 32 && c < 127)) {
            if (len < 3198) {
                for (int i = len; i >= s->cursor; i--) s->content[i + 1] = s->content[i];
                s->content[s->cursor] = c;
                s->cursor++;
            }
        }
    }
}

/* =============================================================================
 *  APP: SNAKE
 * ========================================================================== */

typedef struct {
    int snake_x[200], snake_y[200];
    int len;
    int dir_x, dir_y;
    int food_x, food_y;
    int score;
    int game_over;
    uint64_t last_move;
} snake_state_t;

static void app_snake_draw(window_t *w) {
    snake_state_t *s = (snake_state_t *)w->priv;
    int cx = w->x + 10, cy = w->y + TITLE_H + 10;
    int gw = 20, gh = 15, cs = 14;
    
    if (s->len == 0) {
        s->len = 3; s->snake_x[0] = 10; s->snake_y[0] = 7;
        for(int i=1;i<3;i++){ s->snake_x[i]=10-i; s->snake_y[i]=7; }
        s->dir_x = 1; s->dir_y = 0;
        s->food_x = 15; s->food_y = 7;
        s->last_move = get_ticks();
    }
    
    uint64_t now = get_ticks();
    if (!s->game_over && now - s->last_move > 15) {
        s->last_move = now;
        int nx = s->snake_x[0] + s->dir_x;
        int ny = s->snake_y[0] + s->dir_y;
        if (nx < 0 || nx >= gw || ny < 0 || ny >= gh) s->game_over = 1;
        for (int i = 0; i < s->len; i++) if (nx == s->snake_x[i] && ny == s->snake_y[i]) s->game_over = 1;
        if (!s->game_over) {
            for (int i = s->len; i > 0; i--) {
                s->snake_x[i] = s->snake_x[i-1];
                s->snake_y[i] = s->snake_y[i-1];
            }
            s->snake_x[0] = nx; s->snake_y[0] = ny;
            if (nx == s->food_x && ny == s->food_y) {
                if (s->len < 199) s->len++;
                s->score += 10;
                s->food_x = (now * 7) % gw; s->food_y = (now * 11) % gh;
            }
        }
    }
    
    char sb[32]; sstrcpy(sb, "Score: "); utoa10(s->score, sb + 7);
    bb_draw_text(cx, cy, sb, COLOR_TEXT, 1); cy += 16;
    bb_fill_rect(cx, cy, gw * cs, gh * cs, 0x222233);
    bb_frame(cx, cy, gw * cs, gh * cs, COLOR_FRAME);
    
    bb_fill_rect(cx + s->food_x * cs + 1, cy + s->food_y * cs + 1, cs - 2, cs - 2, 0xFF4444);
    for (int i = 0; i < s->len; i++) {
        uint32_t col = (i == 0) ? 0x55FF55 : 0x22CC22;
        bb_fill_rect(cx + s->snake_x[i] * cs + 1, cy + s->snake_y[i] * cs + 1, cs - 2, cs - 2, col);
    }
    
    if (s->game_over) bb_draw_text(cx + 40, cy + 100, "GAME OVER (press r to restart)", 0xFF0000, 1);
    else bb_draw_text(cx, cy + gh * cs + 8, "Use w/a/s/d to move", COLOR_TEXT_DIM, 1);
}

static void app_snake_key(window_t *w, char c) {
    snake_state_t *s = (snake_state_t *)w->priv;
    if (c == 'r') { s->len = 0; s->game_over = 0; s->score = 0; return; }
    if (c == 'w' && s->dir_y == 0) { s->dir_x = 0; s->dir_y = -1; }
    if (c == 's' && s->dir_y == 0) { s->dir_x = 0; s->dir_y = 1; }
    if (c == 'a' && s->dir_x == 0) { s->dir_x = -1; s->dir_y = 0; }
    if (c == 'd' && s->dir_x == 0) { s->dir_x = 1; s->dir_y = 0; }
}

/* =============================================================================
 *  APP: TERMINAL (egyszerű, beépített parancsokkal)
 * ========================================================================== */

#define TERM_LINES   30
#define TERM_COLS    72
#define TERM_INPUT   80

typedef struct {
    char  lines[TERM_LINES][TERM_COLS + 1];
    int   line_count;
    char  input[TERM_INPUT];
    int   input_len;
    char  cwd[128];
} term_state_t;

static void term_push_line(term_state_t *s, const char *line) {
    if (s->line_count >= TERM_LINES) {
        /* scroll */
        for (int i = 0; i < TERM_LINES - 1; i++) {
            for (int j = 0; j <= TERM_COLS; j++) {
                s->lines[i][j] = s->lines[i+1][j];
            }
        }
        s->line_count = TERM_LINES - 1;
    }
    int j = 0;
    while (line[j] && j < TERM_COLS) {
        s->lines[s->line_count][j] = line[j];
        j++;
    }
    s->lines[s->line_count][j] = 0;
    s->line_count++;
}

static void term_init(term_state_t *s) {
    s->line_count = 0;
    s->input_len = 0;
    sstrcpy(s->cwd, "/");
    term_push_line(s, "RexOS Terminal v1");
    term_push_line(s, "Type 'help' for available commands.");
    term_push_line(s, "");
}

static void term_exec(term_state_t *s, const char *line) {
    if (!line[0]) return;
    char prompt[160];
    sstrcpy(prompt, s->cwd);
    sstrcat(prompt, " $ ");
    sstrcat(prompt, line);
    term_push_line(s, prompt);

    if (sstrcmp(line, "help") == 0) {
        term_push_line(s, "Commands:");
        term_push_line(s, "  help         - show help");
        term_push_line(s, "  clear        - clear the terminal");
        term_push_line(s, "  ls [path]    - list directory");
        term_push_line(s, "  cd <path>    - change directory");
        term_push_line(s, "  cat <file>   - print file contents");
        term_push_line(s, "  pwd          - print working directory");
        term_push_line(s, "  run <prog>   - spawn an ELF program");
        term_push_line(s, "  uptime       - show uptime");
        return;
    }
    if (sstrcmp(line, "clear") == 0) {
        s->line_count = 0;
        return;
    }
    if (sstrcmp(line, "pwd") == 0) {
        term_push_line(s, s->cwd);
        return;
    }
    if (sstrcmp(line, "uptime") == 0) {
        uint64_t sec = get_ticks() / 100;
        char up[64], pp[16];
        pad2(pp, (int)((sec / 3600) % 24)); sstrcpy(up, "up "); sstrcat(up, pp); sstrcat(up, ":");
        pad2(pp, (int)((sec / 60) % 60));   sstrcat(up, pp); sstrcat(up, ":");
        pad2(pp, (int)(sec % 60));          sstrcat(up, pp);
        term_push_line(s, up);
        return;
    }
    if (line[0] == 'l' && line[1] == 's' && (line[2] == 0 || line[2] == ' ')) {
        char path[128];
        if (line[2] == ' ') {
            const char *p = &line[3]; while (*p == ' ') p++;
            if (*p == '/') sstrcpy(path, p);
            else path_join(path, s->cwd, p);
        } else {
            sstrcpy(path, s->cwd);
        }
        int fd = open(path);
        if (fd < 0) {
            char e[160]; sstrcpy(e, "ls: cannot open "); sstrcat(e, path);
            term_push_line(s, e);
            return;
        }
        dirent_t d;
        while (getdents(fd, &d) > 0) {
            char buf[160]; sstrcpy(buf, "  "); sstrcat(buf, d.name);
            term_push_line(s, buf);
        }
        return;
    }
    if (line[0] == 'c' && line[1] == 'd' && line[2] == ' ') {
        const char *p = &line[3]; while (*p == ' ') p++;
        if (sstrcmp(p, "..") == 0) { path_parent(s->cwd); return; }
        char np[128];
        if (*p == '/') sstrcpy(np, p);
        else path_join(np, s->cwd, p);
        int fd = open(np);
        if (fd < 0) {
            char e[160]; sstrcpy(e, "cd: not a directory: "); sstrcat(e, np);
            term_push_line(s, e);
            return;
        }
        sstrcpy(s->cwd, np);
        return;
    }
    if (line[0] == 'c' && line[1] == 'a' && line[2] == 't' && line[3] == ' ') {
        const char *p = &line[4]; while (*p == ' ') p++;
        char np[128];
        if (*p == '/') sstrcpy(np, p);
        else path_join(np, s->cwd, p);
        char buf[1024];
        int n = read_file_into(np, buf, 1024);
        if (n < 0) {
            char e[160]; sstrcpy(e, "cat: cannot open "); sstrcat(e, np);
            term_push_line(s, e);
            return;
        }
        /* sor-bontás */
        char tmp[TERM_COLS + 1]; int o = 0;
        for (int i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\n' || o == TERM_COLS) {
                tmp[o] = 0; term_push_line(s, tmp); o = 0;
                if (ch == '\n') continue;
            }
            if (ch >= 32 && ch < 127) tmp[o++] = ch;
        }
        if (o) { tmp[o] = 0; term_push_line(s, tmp); }
        return;
    }
    if (line[0] == 'r' && line[1] == 'u' && line[2] == 'n' && line[3] == ' ') {
        const char *p = &line[4]; while (*p == ' ') p++;
        char np[128];
        if (*p == '/') sstrcpy(np, p);
        else path_join(np, s->cwd, p);
        int pid = spawn(np);
        if (pid < 0) {
            char e[160]; sstrcpy(e, "run: spawn failed for "); sstrcat(e, np);
            term_push_line(s, e);
        } else {
            char m[64]; sstrcpy(m, "Spawned PID "); char nn[16]; utoa10(pid, nn);
            sstrcat(m, nn); term_push_line(s, m);
        }
        return;
    }
    {
        char e[160]; sstrcpy(e, "unknown command: "); sstrcat(e, line);
        term_push_line(s, e);
    }
}

static void app_term_draw(window_t *w) {
    term_state_t *s = (term_state_t *)w->priv;
    if (!s->cwd[0]) term_init(s);

    int cx = w->x + 6, cy = w->y + TITLE_H + 6;
    int cw = w->w - 12, ch = w->h - TITLE_H - 12;
    bb_fill_rect(cx, cy, cw, ch, 0x080812);
    bb_frame(cx, cy, cw, ch, COLOR_FRAME);

    int line_h = 12;
    int max_lines = (ch - 18) / line_h;
    int start = s->line_count - max_lines;
    if (start < 0) start = 0;
    int row = 0;
    for (int i = start; i < s->line_count; i++) {
        bb_draw_text(cx + 6, cy + 4 + row * line_h, s->lines[i], 0xCFE7B0, 1);
        row++;
    }
    /* Prompt sor */
    char p[160];
    sstrcpy(p, s->cwd);
    sstrcat(p, " $ ");
    int plen = sstrlen(p);
    bb_draw_text(cx + 6, cy + ch - 14, p, 0xFFD080, 1);
    /* Input */
    char ib[TERM_INPUT + 1];
    int j = 0;
    for (; j < s->input_len && j < TERM_INPUT; j++) ib[j] = s->input[j];
    ib[j] = 0;
    bb_draw_text(cx + 6 + plen * 6, cy + ch - 14, ib, 0xFFFFFF, 1);
    /* Cursor */
    int cur_x = cx + 6 + (plen + s->input_len) * 6;
    bb_fill_rect(cur_x, cy + ch - 14, 5, 8, 0xFFFFFF);
}

static void app_term_key(window_t *w, char c) {
    term_state_t *s = (term_state_t *)w->priv;
    if (!s->cwd[0]) term_init(s);
    if (c == '\n') {
        s->input[s->input_len] = 0;
        term_exec(s, s->input);
        s->input_len = 0;
        return;
    }
    if (c == '\b') {
        if (s->input_len > 0) s->input_len--;
        return;
    }
    if (c >= 32 && c < 127 && s->input_len < TERM_INPUT - 1) {
        s->input[s->input_len++] = c;
    }
}

/* =============================================================================
 *  Asztali ikonok
 * ========================================================================== */

/* =============================================================================
 *  APP: REXBROWSER - Interaktív HTTP Böngésző
 * ========================================================================== */

#define BROWSER_URL_MAX  256
#define BROWSER_BUF_MAX  3700

typedef struct {
    char    url[BROWSER_URL_MAX];   /* pl. "example.com/path" */
    char    content[BROWSER_BUF_MAX]; /* nyers HTTP válasz szövege (HTML) */
    int     url_focused;            /* 1 ha a URL bar aktív */
    int     url_len;
    int     scroll;                 /* tartalom görgetése soronként */
    int     loading;                /* 1 = betöltés folyamatban */
    int     error;                  /* 1 = kapcsolati hiba */
    char    status[64];             /* státus szöveg */
} browser_state_t;

/* Egyszerű string segédek (libc nélkül) */
static void b_strcpy(char *d, const char *s) { while((*d++=*s++)); }
static int b_strncmp(const char *a, const char *b, int n) {
    for(int i=0;i<n;i++) { if(a[i]!=b[i]) return a[i]-b[i]; if(!a[i]) return 0; } return 0;
}

/* HTTP GET küldése */
static void browser_do_fetch(browser_state_t *st) {
    st->loading = 1;
    st->error   = 0;
    st->content[0] = 0;
    b_strcpy(st->status, "Connecting...");

    /* URL parse: "host/path" vagy "http://host/path" */
    const char *url = st->url;
    if (b_strncmp(url, "http://", 7) == 0) url += 7;
    if (b_strncmp(url, "https://", 8) == 0) {
        b_strcpy(st->status, "HTTPS not supported yet (no TLS)");
        st->error = 1; st->loading = 0;
        return;
    }

    /* Szétválasztjuk host és path részre */
    char host[128]; char path[128];
    int hi = 0, pi = 0;
    int in_path = 0;
    for (int i = 0; url[i] && hi < 127; i++) {
        if (!in_path && url[i] == '/') { in_path = 1; path[pi++] = url[i]; continue; }
        if (in_path) path[pi++] = url[i];
        else         host[hi++] = url[i];
    }
    host[hi] = 0;
    if (pi == 0) { path[0] = '/'; path[1] = 0; }
    else          path[pi] = 0;

    /* TCP kapcsolat: port 80 */
    uint64_t sock = net_connect(host, 80);
    if (sock == (uint64_t)-1 || sock == 0) {
        b_strcpy(st->status, "Connection failed (DNS/TCP error)");
        st->error = 1; st->loading = 0;
        return;
    }

    b_strcpy(st->status, "Sending HTTP GET...");

    /* HTTP/1.0 kérés összeállítása */
    char req[512];
    int rlen = 0;
    /* "GET /path HTTP/1.0\r\nHost: host\r\n\r\n" */
    const char *method = "GET ";
    for(int i=0;method[i];i++) req[rlen++]=method[i];
    for(int i=0;path[i];i++)   req[rlen++]=path[i];
    const char *ver = " HTTP/1.0\r\nHost: ";
    for(int i=0;ver[i];i++) req[rlen++]=ver[i];
    for(int i=0;host[i];i++) req[rlen++]=host[i];
    const char *end = "\r\nConnection: close\r\n\r\n";
    for(int i=0;end[i];i++) req[rlen++]=end[i];

    net_send(sock, req, rlen);
    b_strcpy(st->status, "Waiting for response...");

    /* Fogadjuk a választ */
    int total = 0;
    char chunk[512];
    int bytes;
    while ((bytes = net_recv(sock, chunk, 511)) > 0 && total < BROWSER_BUF_MAX - 1) {
        int copy = bytes;
        if (total + copy >= BROWSER_BUF_MAX - 1) copy = BROWSER_BUF_MAX - 1 - total;
        for(int i=0;i<copy;i++) st->content[total++] = chunk[i];
    }
    st->content[total] = 0;
    /* net_close(sock); -- Jelenleg még nincs, de a szerver lezárja a kapcsolatot (Connection: close) */

    /* HTTP fejléc kihagyása: keressük a \r\n\r\n-t */
    char *body = st->content;
    for (int i = 0; i < total - 3; i++) {
        if (body[i]=='\r'&&body[i+1]=='\n'&&body[i+2]=='\r'&&body[i+3]=='\n') {
            body = body + i + 4;
            break;
        }
    }
    /* Ha a body nem az elejétől indul, toljuk előre */
    if (body != st->content) {
        int blen = 0;
        char *p = body;
        while(*p) { st->content[blen++] = *p++; }
        st->content[blen] = 0;
    }

    st->scroll = 0;
    st->loading = 0;
    if (total == 0) {
        b_strcpy(st->status, "Empty response");
        st->error = 1;
    } else {
        b_strcpy(st->status, "OK");
    }
}

/* Nyers szöveg renderelése HTML tagek kihagyásával és alap formázással */
static void browser_render_text(window_t *w, browser_state_t *st) {
    int cx = w->x + 8;
    int cy = w->y + TITLE_H + 50; /* fejléc + URL bar alá */
    int max_y = w->y + w->h - 10;
    int line_h = 10;
    int col = 0;
    int max_col = (w->w - 24) / 6;

    int skip_lines = st->scroll;
    int cur_line = 0;

    const char *p = st->content;
    int in_tag = 0;
    int skip_content = 0; /* 1: style, 2: script */

    while (*p && cy < max_y) {
        char c = *p;

        /* Tag kezelés */
        if (c == '<') {
            in_tag = 1;
            /* Speciális tartalom-kihagyás detektálása */
            if (sstrcmp_n(p, "<style", 6) == 0) skip_content = 1;
            if (sstrcmp_n(p, "<script", 7) == 0) skip_content = 2;
            
            /* Lezáró tagek detektálása a tartalom-visszakapcsoláshoz */
            if (skip_content == 1 && sstrcmp_n(p, "</style>", 8) == 0) { skip_content = 0; p += 8; in_tag = 0; continue; }
            if (skip_content == 2 && sstrcmp_n(p, "</script>", 9) == 0) { skip_content = 0; p += 9; in_tag = 0; continue; }

            /* Alap formázó tagek után sortörés */
            if (sstrcmp_n(p, "<p", 2) == 0 || sstrcmp_n(p, "<div", 4) == 0 || 
                sstrcmp_n(p, "<h1", 3) == 0 || sstrcmp_n(p, "<br", 3) == 0 ||
                sstrcmp_n(p, "<li", 3) == 0 || sstrcmp_n(p, "</p", 3) == 0) {
                if (col > 0) {
                    col = 0;
                    if (cur_line >= skip_lines) cy += line_h; else cur_line++;
                }
            }
            p++;
            continue;
        }
        if (c == '>') { in_tag = 0; p++; continue; }
        
        if (in_tag) { p++; continue; }
        if (skip_content) { p++; continue; }

        /* Megjelenítendő karakter */
        if (c == '\n' || col >= max_col) {
            col = 0;
            if (cur_line >= skip_lines) cy += line_h; else cur_line++;
            p++;
            if (c == '\n') continue;
        }
        if (c == '\r' || c == '\t') { p++; continue; }

        if (cur_line >= skip_lines) {
            if (cy < max_y && col < max_col) {
                char buf[2] = { c, 0 };
                bb_draw_text(cx + col * 6, cy, buf, 0xCDD6F4, 1);
                col++;
            }
        } else {
            col++;
        }
        p++;
    }
}

static void app_browser_draw(window_t *w) {
    browser_state_t *st = (browser_state_t *)w->priv;

    /* Háttér */
    bb_fill_rect(w->x, w->y + TITLE_H, w->w, w->h - TITLE_H, 0x1E1E2E);

    /* --- URL bar ---------------------------------------- */
    uint32_t bar_col = st->url_focused ? 0x45475A : 0x313244;
    bb_fill_rounded_rect(w->x + 8, w->y + TITLE_H + 8, w->w - 16, 22, 4, bar_col);

    /* Lock ikon (egyszerű szöveg) */
    bb_draw_text(w->x + 14, w->y + TITLE_H + 15, "http://", 0x6C7086, 1);

    char display_url[BROWSER_URL_MAX + 8];
    int dlen = 0;
    const char *u = st->url;
    while(*u && dlen < 60) display_url[dlen++] = *u++;
    if (st->url_focused) display_url[dlen++] = '_'; /* kurzor */
    display_url[dlen] = 0;
    bb_draw_text(w->x + 14 + 6*7, w->y + TITLE_H + 15, display_url, 0xCDD6F4, 1);

    /* GO gomb */
    uint32_t go_col = 0x89B4FA;
    bb_fill_rounded_rect(w->x + w->w - 42, w->y + TITLE_H + 8, 34, 22, 4, go_col);
    bb_draw_text(w->x + w->w - 34, w->y + TITLE_H + 15, "GO", 0x1E1E2E, 1);

    /* --- Status bar ------------------------------------- */
    bb_fill_rect(w->x, w->y + TITLE_H + 34, w->w, 12, 0x181825);
    uint32_t sc = st->error ? 0xFF5555 : (st->loading ? 0xF9E2AF : 0xA6E3A1);
    bb_draw_text(w->x + 8, w->y + TITLE_H + 37, st->status, sc, 1);

    /* --- Tartalom --------------------------------------- */
    if (st->loading) {
        bb_draw_text(w->x + w->w/2 - 42, w->y + TITLE_H + 60, "Loading...", 0x89B4FA, 2);
    } else if (st->error) {
        bb_draw_text(w->x + 10, w->y + TITLE_H + 60, "Could not load page.", 0xFF5555, 1);
        bb_draw_text(w->x + 10, w->y + TITLE_H + 74, st->status, 0x6C7086, 1);
    } else if (st->content[0] == 0) {
        int cx = w->x + w->w / 2 - 80;
        int cy = w->y + TITLE_H + 70;
        bb_draw_text(cx, cy,      "RexBrowser v0.1", 0xCBA6F7, 2);
        bb_draw_text(cx - 20, cy + 24, "Type a URL and press GO or Enter", 0x6C7086, 1);
        bb_draw_text(cx - 20, cy + 36, "TCP/IP stack: ACTIVE", 0xA6E3A1, 1);
    } else {
        browser_render_text(w, st);

        /* Görgetés hint */
        bb_draw_text(w->x + w->w - 80, w->y + TITLE_H + 37, "UP/DOWN", 0x45475A, 1);
    }
}

static void app_browser_click(window_t *w, int x, int y, int btn) {
    browser_state_t *st = (browser_state_t *)w->priv;
    (void)btn;

    /* GO gomb */
    if (x >= w->x + w->w - 42 && x <= w->x + w->w - 8 &&
        y >= w->y + TITLE_H + 8 && y <= w->y + TITLE_H + 30) {
        browser_do_fetch(st);
        dirty_mark(w->x, w->y, w->w, w->h);
        return;
    }

    /* URL bar */
    if (x >= w->x + 8 && x <= w->x + w->w - 50 &&
        y >= w->y + TITLE_H + 8 && y <= w->y + TITLE_H + 30) {
        st->url_focused = 1;
    } else {
        st->url_focused = 0;
    }
    dirty_mark(w->x, w->y, w->w, w->h);
}

static void app_browser_key(window_t *w, char key) {
    browser_state_t *st = (browser_state_t *)w->priv;

    if (!st->url_focused) {
        /* Tartalom görgetése */
        if (key == 'u' || key == 'U') { if (st->scroll > 0) st->scroll--; dirty_mark(w->x, w->y, w->w, w->h); return; }
        if (key == 'd' || key == 'D') { st->scroll++; dirty_mark(w->x, w->y, w->w, w->h); return; }
        return;
    }

    if (key == '\n' || key == '\r') {
        st->url_focused = 0;
        browser_do_fetch(st);
        dirty_mark(w->x, w->y, w->w, w->h);
        return;
    }
    if (key == '\b' && st->url_len > 0) {
        st->url_len--;
        st->url[st->url_len] = 0;
        dirty_mark(w->x, w->y + TITLE_H, w->w, 40);
        return;
    }
    if (key >= 32 && key < 127 && st->url_len < BROWSER_URL_MAX - 1) {
        st->url[st->url_len++] = key;
        st->url[st->url_len] = 0;
        dirty_mark(w->x, w->y + TITLE_H, w->w, 40);
    }
}


typedef struct {
    const char *label;
    app_kind_t  app;
    uint32_t    color;
} desktop_icon_t;

static const desktop_icon_t g_dt_icons[] = {
    { "Files",     APP_FILES,    COLOR_FOLDER },
    { "Editor",    APP_EDITOR,   COLOR_TXT    },
    { "Calc",      APP_CALC,     0xA0E0FF     },
    { "Terminal",  APP_TERMINAL, 0xCFE7B0     },
    { "SysInfo",   APP_SYSINFO,  0xFFD080     },
    { "Hardware",  APP_HARDWARE, 0xB0CFFF     },
    { "Install",   APP_INSTALLER,0x81C784     },
    { "Clock",     APP_CLOCK,    0x9FE0FF     },
    { "About",     APP_ABOUT,    COLOR_ACCENT },
    { "Snake",     APP_SNAKE,    0x55FF55     },
    { "Browser",   APP_BROWSER,  0x3B82F6     },
};
#define DT_ICON_COUNT ((int)(sizeof(g_dt_icons) / sizeof(g_dt_icons[0])))

#define DT_ICON_W 80
#define DT_ICON_H 70
#define DT_ICON_PAD_X 16
#define DT_ICON_PAD_Y 16

static void desktop_icon_pos(int idx, int *out_x, int *out_y) {
    *out_x = DT_ICON_PAD_X;
    *out_y = DT_ICON_PAD_Y + idx * (DT_ICON_H + 12);
}

static void draw_desktop_icons(int hover_idx) {
    for (int i = 0; i < DT_ICON_COUNT; i++) {
        int ix, iy;
        desktop_icon_pos(i, &ix, &iy);

        /* Hover animáció: alpha lassan közelít a célértékhez */
        uint8_t target = (i == hover_idx) ? HOVER_MAX : 0;
        if (g_icon_hover_alpha[i] < target) {
            int v = (int)g_icon_hover_alpha[i] + HOVER_FADE_IN;
            g_icon_hover_alpha[i] = (v > target) ? target : (uint8_t)v;
        } else if (g_icon_hover_alpha[i] > target) {
            int v = (int)g_icon_hover_alpha[i] - HOVER_FADE_OUT;
            g_icon_hover_alpha[i] = (v < 0) ? 0 : (uint8_t)v;
        }

        /* Ha valamelyik ikon animálódik, dirty-nek jelöljük */
        if (g_icon_hover_alpha[i] > 0) {
            dirty_mark(ix - 6, iy - 6, DT_ICON_W + 12, DT_ICON_H + 12);
        }

        /* Hover háttér: lekerekített, semi-transparent */
        if (g_icon_hover_alpha[i] > 0) {
            bb_fill_rounded_rect_alpha(ix - 4, iy - 4,
                                       DT_ICON_W + 8, DT_ICON_H + 8,
                                       8, 0x312E81, g_icon_hover_alpha[i]);
        }

        /* Ikon szimbólum: lekerekített négyzetben */
        uint32_t ic = g_dt_icons[i].color;
        bb_fill_rounded_rect(ix + 20, iy + 4, 36, 36, 6, 0x1E293B);
        /* Kis fény az ikon tetején */
        bb_fill_rect_alpha(ix + 21, iy + 5, 34, 8, 0xFFFFFF, 15);
        bb_fill_rounded_rect(ix + 24, iy + 8, 28, 28, 4, ic);

        /* Felirat: árnyékkal */
        int tlen = sstrlen(g_dt_icons[i].label);
        int tw = tlen * 6;
        int tx = ix + (DT_ICON_W - tw) / 2;
        bb_draw_text(tx + 1, iy + 47, g_dt_icons[i].label, 0x000000, 1); /* árnyék */
        bb_draw_text(tx,     iy + 46, g_dt_icons[i].label, COLOR_TEXT, 1);
    }
}

static int desktop_icon_hit(int mx, int my) {
    for (int i = 0; i < DT_ICON_COUNT; i++) {
        int ix, iy;
        desktop_icon_pos(i, &ix, &iy);
        if (mx >= ix - 4 && mx < ix + DT_ICON_W + 4 &&
            my >= iy - 4 && my < iy + DT_ICON_H + 4) return i;
    }
    return -1;
}

/* --- Alkalmazás indítás dispatcher ---------------------------------------- */

static void launch_app(app_kind_t app) {
    switch (app) {
        case APP_FILES:
            window_new(APP_FILES, "Files", 520, 360,
                       app_files_draw, app_files_click, NULL);
            break;
        case APP_EDITOR:
            window_new(APP_EDITOR, "Text Viewer", 600, 400,
                       app_editor_draw, NULL, app_editor_key);
            break;
        case APP_CALC:
            window_new(APP_CALC, "Calculator", 260, 280,
                       app_calc_draw, app_calc_click, app_calc_key);
            break;
        case APP_SYSINFO:
            window_new(APP_SYSINFO, "System Information", 480, 340,
                       app_sysinfo_draw, NULL, NULL);
            break;
        case APP_CLOCK:
            window_new(APP_CLOCK, "Clock", 540, 200,
                       app_clock_draw, NULL, NULL);
            break;
        case APP_TERMINAL:
            window_new(APP_TERMINAL, "Terminal", 600, 360,
                       app_term_draw, NULL, app_term_key);
            break;
        case APP_ABOUT:
            window_new(APP_ABOUT, "About RexOS", 460, 340,
                       app_about_draw, NULL, NULL);
            break;
        case APP_HARDWARE:
            window_new(APP_HARDWARE, "Hardware", 560, 420,
                       app_hardware_draw, NULL, NULL);
            break;
        case APP_INSTALLER: {
            int idx = window_new(APP_INSTALLER, "RexOS Installer", 540, 400,
                       app_installer_draw, app_installer_event, NULL);
            if (idx >= 0) {
                installer_state_t *st = (installer_state_t *)g_windows[idx].priv;
                st->selected = 0;
                st->confirm_stage = 0;
                st->status_msg = 0;
            }
            break;
        }
        case APP_SNAKE:
            window_new(APP_SNAKE, "Snake", 300, 290,
                       app_snake_draw, NULL, app_snake_key);
            break;
        case APP_BROWSER: {
            int idx = window_new(APP_BROWSER, "RexBrowser", 680, 500,
                       app_browser_draw, app_browser_click, app_browser_key);
            if (idx >= 0) {
                browser_state_t *bs = (browser_state_t *)g_windows[idx].priv;
                bs->url[0]     = 0;
                bs->url_len    = 0;
                bs->url_focused = 1;
                bs->content[0] = 0;
                bs->loading    = 0;
                bs->error      = 0;
                bs->scroll     = 0;
                /* Alapértelmezett státus */
                const char *ready = "TCP/IP Stack active. Type a URL and press Enter.";
                int ri = 0;
                while(ready[ri] && ri < 63) { bs->status[ri] = ready[ri]; ri++; }
                bs->status[ri] = 0;
            }
            break;
        }
    }
}

/* --- Tálca + Start menü --------------------------------------------------- */

static void draw_taskbar(uint64_t ticks, int mx, int my) {
    int ty = (int)scr_h - TASKBAR_H;

    /* Dirty rect: a teljes tálca sáv */
    dirty_mark(0, ty - 1, (int)scr_w, TASKBAR_H + 1);

    /* Glassmorphism alap: semi-transparent sötét réteg */
    bb_fill_rect_alpha(0, ty, (int)scr_w, TASKBAR_H, 0x040A14, 220);
    /* Felső elválasztó vonal (kékeslila glow) */
    bb_fill_rect_alpha(0, ty, (int)scr_w, 1, COLOR_ACCENT, 80);
    bb_fill_rect_alpha(0, ty + 1, (int)scr_w, 1, COLOR_ACCENT, 30);

    /* Start gomb: lekerekített pill */
    int start_x = 6, start_y = ty + 5;
    int start_w = 80, start_h = TASKBAR_H - 10;
    int hover = (mx >= start_x && mx < start_x + start_w &&
                 my >= start_y && my < start_y + start_h);
    uint32_t sbg = hover ? COLOR_ACCENT_HOV : COLOR_ACCENT;
    bb_fill_rounded_rect(start_x, start_y, start_w, start_h, 6, sbg);
    /* Tetején egy halvány fehér fény-csík */
    bb_fill_rect_alpha(start_x + 2, start_y + 1, start_w - 4, 4, 0xFFFFFF, 30);
    bb_draw_text(start_x + 8, start_y + 8, "Rex", 0xFFFFFF, 2);

    /* Megnyitott ablakok */
    int tx = start_x + start_w + 10;
    for (int i = 0; i < g_window_count; i++) {
        if (!g_windows[i].open) continue;
        int tbw = 110;
        int is_focused = (i == g_focus);
        /* Aktív ablak: accent csíkkal */
        bb_fill_rounded_rect_alpha(tx, ty + 5, tbw, TASKBAR_H - 10, 4,
                                   is_focused ? 0x1E3A5F : 0x111827, 200);
        if (is_focused) {
            bb_fill_rect(tx + 4, ty + TASKBAR_H - 5, tbw - 8, 2, COLOR_ACCENT);
        }
        char nb[16]; int j = 0;
        while (g_windows[i].title[j] && j < 12) { nb[j] = g_windows[i].title[j]; j++; }
        nb[j] = 0;
        bb_draw_text(tx + 8, ty + 13, nb,
                     is_focused ? COLOR_TEXT : COLOR_TEXT_DIM, 1);
        tx += tbw + 5;
        if (tx > (int)scr_w - 120) break;
    }

    /* Power gomb: piros pill a jobb oldalon */
    int pw = 30, ph = TASKBAR_H - 10;
    int px = (int)scr_w - pw - 4;
    int py = ty + 5;
    int pow_hover = (mx >= px && mx < px + pw && my >= py && my < py + ph);
    uint32_t pow_bg  = pow_hover ? 0xDC2626 : 0x7F1D1D;  /* piros hover */
    bb_fill_rounded_rect(px, py, pw, ph, 5, pow_bg);
    bb_fill_rect_alpha(px + 1, py + 1, pw - 2, 3, 0xFFFFFF, 20);
    /* Power ikon: viszonylag kicsi a gombhoz képest */
    bb_power_icon(px + 10, py + (ph - 10) / 2, 0xFFFFFF);

    /* Óra: valódi RTC idő használata */
    rtc_time_t rt;
    get_time(&rt);
    char clk[12], p[4];
    pad2(p, rt.hour); sstrcpy(clk, p); sstrcat(clk, ":");
    pad2(p, rt.minute); sstrcat(clk, p); sstrcat(clk, ":");
    pad2(p, rt.second); sstrcat(clk, p);
    
    int clk_x = px - 96;
    bb_fill_rounded_rect(clk_x - 4, ty + 5, 88, TASKBAR_H - 10, 5, 0x0F1A2E);
    bb_fill_rect_alpha(clk_x - 4, ty + 5, 88, 1, 0x60A5FA, 60);
    bb_draw_text(clk_x + 4, ty + 13, clk, 0x60A5FA, 2);
}

static void draw_start_menu(int mx, int my) {
    if (!g_start_menu_open) return;
    int sx = 6, sy = (int)scr_h - TASKBAR_H - 20 - DT_ICON_COUNT * 30 - 42;
    int sw = 210, sh = DT_ICON_COUNT * 30 + 28 + 42;  /* +42: elválasztó + Shutdown */

    dirty_mark(sx - 2, sy - 2, sw + 4, sh + 4);

    /* Panel háttér: glassmorphism */
    bb_fill_rounded_rect_alpha(sx, sy, sw, sh, 10, 0x080E1C, 230);
    bb_fill_rect_alpha(sx + 1, sy + 1, sw - 2, 1, 0xFFFFFF, 20);
    /* Keret */
    bb_hline(sx, sy, sw, COLOR_FRAME);
    bb_hline(sx, sy + sh - 1, sw, COLOR_FRAME);
    bb_vline(sx, sy, sh, COLOR_FRAME);
    bb_vline(sx + sw - 1, sy, sh, COLOR_FRAME);

    /* Fejléc */
    bb_fill_rect_alpha(sx, sy, sw, 22, COLOR_ACCENT, 30);
    bb_draw_text(sx + 10, sy + 6, "Applications", COLOR_TEXT_DIM, 1);
    bb_hline(sx, sy + 22, sw, COLOR_FRAME);

    for (int i = 0; i < DT_ICON_COUNT; i++) {
        int iy = sy + 26 + i * 30;
        int hov = (mx >= sx && mx < sx + sw && my >= iy && my < iy + 26);
        if (hov) {
            bb_fill_rounded_rect_alpha(sx + 4, iy + 2, sw - 8, 24, 4,
                                       COLOR_ACCENT, 80);
        }
        bb_fill_rounded_rect(sx + 12, iy + 7, 12, 12, 3, g_dt_icons[i].color);
        bb_draw_text(sx + 32, iy + 9, g_dt_icons[i].label,
                     hov ? 0xFFFFFF : COLOR_TEXT, 1);
    }

    /* Elválasztó + Shutdown gomb */
    int sep_y = sy + 26 + DT_ICON_COUNT * 30 + 4;
    bb_hline(sx + 8, sep_y, sw - 16, COLOR_FRAME);
    int shut_y = sep_y + 6;
    int shut_hov = (mx >= sx && mx < sx + sw && my >= shut_y && my < shut_y + 26);
    if (shut_hov) {
        bb_fill_rounded_rect_alpha(sx + 4, shut_y + 2, sw - 8, 24, 4, 0x991B1B, 180);
    }
    bb_power_icon(sx + 13, shut_y + 8, shut_hov ? 0xFFFFFF : 0xFF6B6B);
    bb_draw_text(sx + 32, shut_y + 9, "Shutdown",
                 shut_hov ? 0xFFFFFF : 0xFF6B6B, 1);
}

static int start_menu_hit(int mx, int my) {
    if (!g_start_menu_open) return -1;
    /* Koordináták meg kell egyezzenek a draw_start_menu-val */
    int sx = 6, sy = (int)scr_h - TASKBAR_H - 20 - DT_ICON_COUNT * 30 - 42;
    int sw = 210;
    for (int i = 0; i < DT_ICON_COUNT; i++) {
        int iy = sy + 26 + i * 30;
        if (mx >= sx && mx < sx + sw && my >= iy && my < iy + 26) return i;
    }
    /* Shutdown gomb */
    int sep_y = sy + 26 + DT_ICON_COUNT * 30 + 4;
    int shut_y = sep_y + 6;
    if (mx >= sx && mx < sx + sw && my >= shut_y && my < shut_y + 26) return -2;
    return -1;
}

static int taskbar_hit_window(int mx, int my) {
    int ty = (int)scr_h - TASKBAR_H;
    if (my < ty) return -2;
    int tx = 6 + 80 + 8;
    for (int i = 0; i < g_window_count; i++) {
        if (!g_windows[i].open) continue;
        if (mx >= tx && mx < tx + 120 && my >= ty + 4 && my < ty + TASKBAR_H - 4) return i;
        tx += 126;
    }
    return -1;
}

static int taskbar_hit_start(int mx, int my) {
    int ty = (int)scr_h - TASKBAR_H;
    return (mx >= 6 && mx < 86 && my >= ty + 4 && my < ty + TASKBAR_H - 4);
}

/* --- Main ---------------------------------------------------------------- */

void _start(void) {
    fb = (uint32_t *)get_fb(&scr_w, &scr_h, &pitch);
    if (!fb) { print("desktop: no framebuffer\n"); exit(1); }

    backbuf_size = scr_w * scr_h * 4;
    backbuf = (uint32_t *)malloc(backbuf_size);
    if (!backbuf) { print("desktop: no memory for back buffer\n"); exit(1); }

    /* Wallpaper inicializálás */
    g_wallpaper_buf = (uint32_t *)malloc(scr_w * scr_h * 4);
    if (g_wallpaper_buf) {
        /* Előbb procedurális fallback, majd felülírja BMP ha van */
        wallpaper_render_procedural();
        g_wallpaper_ok = 1;
        /* BMP keresés: initrd és FAT32 /mnt is */
        load_bmp_wallpaper("wallpaper.bmp");
        if (!g_wallpaper_ok) load_bmp_wallpaper("/mnt/wallpaper.bmp");
    }

    /* Ikon hover állapotok nullázása */
    for (int i = 0; i < 16; i++) g_icon_hover_alpha[i] = 0;

    /* Initial sysinfo window */
    launch_app(APP_ABOUT);
    launch_app(APP_FILES);

    uint64_t last_draw = 0;
    int running = 1;
    int prev_hover_icon = -1;

    while (running) {
        uint64_t now = get_ticks();

        uint32_t mx, my, mb;
        get_mouse(&mx, &my, &mb);
        int btn_down  = (mb & 1) && !(g_prev_btn & 1);   /* press edge */
        int btn_up    = !(mb & 1) && (g_prev_btn & 1);   /* release edge */
        int btn_held  = (mb & 1) ? 1 : 0;

        /* Pending editor megnyitás (Files -> Editor) */
        if (g_editor_pending_flag) {
            launch_app(APP_EDITOR);
            window_t *ew = window_find_by_app(APP_EDITOR);
            if (ew) {
                editor_state_t *es = (editor_state_t *)ew->priv;
                sstrcpy(es->path, g_editor_pending.path);
                es->loaded = 0;
                es->scroll = 0;
            }
            g_editor_pending_flag = 0;
        }

        /* Drag követés */
        if (g_drag_idx >= 0) {
            if (btn_held) {
                g_windows[g_drag_idx].x = (int)mx - g_drag_dx;
                g_windows[g_drag_idx].y = (int)my - g_drag_dy;
                if (g_windows[g_drag_idx].x < 0) g_windows[g_drag_idx].x = 0;
                if (g_windows[g_drag_idx].y < 0) g_windows[g_drag_idx].y = 0;
            } else {
                g_drag_idx = -1;
            }
        }

        /* Leállítás: ha kértem, rajzolunk egy leállási képernyőt, majd kilépünk */
        if (g_shutdown_requested) {
            bb_fill_rect(0, 0, (int)scr_w, (int)scr_h, 0x000000);
            bb_fill_rounded_rect((int)scr_w/2-160, (int)scr_h/2-40, 320, 80, 10, 0x0F172A);
            bb_hline((int)scr_w/2-160, (int)scr_h/2-40, 320, 0x334155);
            bb_power_icon((int)scr_w/2-16, (int)scr_h/2-28, 0x7C3AED);
            bb_draw_text((int)scr_w/2-52, (int)scr_h/2-4,  "Shutting down...", 0xF1F5F9, 2);
            bb_draw_text((int)scr_w/2-48, (int)scr_h/2+20, "It is safe to power off.", 0x64748B, 1);
            flush_rect(0, 0, (int)scr_w, (int)scr_h);
            /* Rövid várakozás (kb. 1 másodperc) */
            uint64_t t0 = get_ticks();
            while (get_ticks() - t0 < 100) yield();
            exit(0);
        }

        /* Klikk eseménykezelés (press edge) */
        if (btn_down) {
            /* Tálca power gomb */
            int tb_ty = (int)scr_h - TASKBAR_H;
            int pw = 30, px2 = (int)scr_w - pw - 4;
            int py2 = tb_ty + 5;
            if (mx >= (uint32_t)px2 && mx < (uint32_t)(px2 + pw) &&
                my >= (uint32_t)py2 && my < (uint32_t)(py2 + TASKBAR_H - 10)) {
                g_shutdown_requested = 1;
                goto skip_other;
            }

            /* Start menü hit? */
            if (g_start_menu_open) {
                int sm = start_menu_hit(mx, my);
                if (sm == -2) {
                    /* Shutdown kattintott */
                    g_shutdown_requested = 1;
                    g_start_menu_open = 0;
                    goto skip_other;
                } else if (sm >= 0) {
                    launch_app(g_dt_icons[sm].app);
                    g_start_menu_open = 0;
                } else if (!taskbar_hit_start(mx, my)) {
                    g_start_menu_open = 0;
                }
            }
            /* Start gomb? */
            if (taskbar_hit_start(mx, my)) {
                g_start_menu_open = !g_start_menu_open;
                goto skip_other;
            }
            /* Tálca ablak? */
            int twi = taskbar_hit_window(mx, my);
            if (twi >= 0) {
                g_focus = twi;
                goto skip_other;
            }

            /* Asztali ikon? — csak ha nincs ablak fölötte */
            int dt_i = desktop_icon_hit(mx, my);
            int hits_window = 0;
            for (int i = g_window_count - 1; i >= 0; i--) {
                if (!g_windows[i].open) continue;
                window_t *win = &g_windows[i];
                if ((int)mx >= win->x && (int)mx < win->x + win->w &&
                    (int)my >= win->y && (int)my < win->y + win->h) {
                    hits_window = 1; break;
                }
            }
            if (dt_i >= 0 && !hits_window) {
                /* double-click-szerű: ha ugyanaz az ikon mint múltkor, indít */
                if (prev_hover_icon == dt_i) {
                    launch_app(g_dt_icons[dt_i].app);
                    prev_hover_icon = -1;
                } else {
                    prev_hover_icon = dt_i;
                }
                goto skip_other;
            }
            /* Ablakok (felülről lefelé) */
            for (int i = g_window_count - 1; i >= 0; i--) {
                if (!g_windows[i].open) continue;
                window_t *win = &g_windows[i];
                if (window_hit_close(win, mx, my)) {
                    window_close(i);
                    /* compact tömb -- nem szükséges; open=0 elég */
                    goto skip_other;
                }
                if (window_hit_titlebar(win, mx, my)) {
                    g_focus = i;
                    g_drag_idx = i;
                    g_drag_dx = (int)mx - win->x;
                    g_drag_dy = (int)my - win->y;
                    goto skip_other;
                }
                if (window_hit_client(win, mx, my)) {
                    g_focus = i;
                    if (win->click) win->click(win, (int)mx, (int)my, 1);
                    goto skip_other;
                }
            }
        }
    skip_other:
        (void)btn_up;

        /* Billentyűzet -> fókuszált ablak */
        char k = kbd_poll();
        if (k == 27) { running = 0; }
        else if (k != 0) {
            if (g_focus >= 0 && g_windows[g_focus].open && g_windows[g_focus].key) {
                g_windows[g_focus].key(&g_windows[g_focus], k);
            }
        }

        /* Rendelés ~25Hz */
        if (now - last_draw >= 4) {
            last_draw = now;

            /* --- Dirty rect inicializálás ----------------------------------------
             * Az előző frame flush területét BELEÉPJÜK az új dirty rectbe.
             * Ez biztosítja, hogy az elmozdulés régi pozíciója is frissül a
             * framebufferben (a backbufban már helyül van a háttérkep). */
            dirty_reset();
            if (g_prev_flush_x2 > g_prev_flush_x1) {
                dirty_mark(g_prev_flush_x1, g_prev_flush_y1,
                           g_prev_flush_x2 - g_prev_flush_x1,
                           g_prev_flush_y2 - g_prev_flush_y1);
            }

            bb_draw_wallpaper(now);

            int hover_icon = desktop_icon_hit((int)mx, (int)my);
            draw_desktop_icons(hover_icon);

            /* Ablakok rajzolása (alulról felfelé) */
            for (int i = 0; i < g_window_count; i++) {
                if (!g_windows[i].open) continue;
                draw_window_frame(&g_windows[i], i == g_focus);
                if (g_windows[i].draw) g_windows[i].draw(&g_windows[i]);
            }

            draw_taskbar(now, (int)mx, (int)my);
            draw_start_menu((int)mx, (int)my);

            /* A taskbar és a kurzor területe mindig dirty */
            dirty_mark(0, (int)scr_h - TASKBAR_H - 1, (int)scr_w, TASKBAR_H + 1);
            dirty_mark((int)g_prev_mx, (int)g_prev_my, CURSOR_W + 2, CURSOR_H + 2);
            dirty_mark((int)mx, (int)my, CURSOR_W + 2, CURSOR_H + 2);

            /* Mentjük a mostani dirty rect-et a következő frame számára
             * (a dirty_flush után, a reset előtt). */
            g_prev_flush_x1 = g_dirty_x1; g_prev_flush_y1 = g_dirty_y1;
            g_prev_flush_x2 = g_dirty_x2; g_prev_flush_y2 = g_dirty_y2;

            dirty_flush();
            dirty_reset();
            draw_cursor_to_fb(mx, my);
        }

        g_prev_btn = mb;
        g_prev_mx = mx; g_prev_my = my;

        yield();
    }

    /* Képernyő törlése kilépéskor */
    for (uint64_t y = 0; y < scr_h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb + y * pitch);
        for (uint64_t x = 0; x < scr_w; x++) row[x] = 0;
    }
    free(backbuf);
    exit(0);
}
