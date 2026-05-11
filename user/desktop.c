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

#define COLOR_BG_TOP      0x101020
#define COLOR_BG_BOT      0x303060
#define COLOR_TASKBAR     0x1A1A2E
#define COLOR_TASKBAR_SEP 0x3A3A5E
#define COLOR_ACCENT      0xE94560
#define COLOR_ACCENT_DIM  0x6B1F2A
#define COLOR_WIN_BG      0x1E1E2E
#define COLOR_WIN_TITLE   0x2B2B40
#define COLOR_WIN_TITLE_A 0x3D4D8A   /* active */
#define COLOR_TEXT        0xE6E6F0
#define COLOR_TEXT_DIM    0x9F9FB8
#define COLOR_FRAME       0x4A4A6E
#define COLOR_BUTTON      0x3E3E58
#define COLOR_BUTTON_HOV  0x55557A
#define COLOR_FIELD       0x14141F
#define COLOR_FOLDER      0xFFC857
#define COLOR_FILE        0x6FB1E8
#define COLOR_EXE         0x7BD389
#define COLOR_TXT         0xD8AEFF

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

/* Gradient háttér */
static void bb_draw_wallpaper(uint64_t tick) {
    (void)tick;
    for (uint32_t y = 0; y < scr_h; y++) {
        uint8_t r = 16  + (y * 32) / scr_h;
        uint8_t g = 24  + (y * 56) / scr_h;
        uint8_t b = 56  + (y * 80) / scr_h;
        uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        uint32_t *row = backbuf + (uint64_t)y * scr_w;
        for (uint32_t x = 0; x < scr_w; x++) row[x] = c;
    }
    /* "csillagok" - random pontok */
    static const uint16_t starpos[] = {
        137, 412, 891, 1023, 234, 567, 778, 1145, 99, 1199, 333, 890,
        201, 645, 1011, 1230, 178, 456, 723, 999, 1067, 88, 444, 822
    };
    for (uint32_t i = 0; i < sizeof(starpos)/sizeof(starpos[0]); i++) {
        uint32_t x = starpos[i] % scr_w;
        uint32_t y = (starpos[i] * 7 + 13) % (scr_h - TASKBAR_H);
        put_bb(x, y, 0xCCCCFF);
    }
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

static void flush_all(void) { flush_rect(0, 0, (int)scr_w, (int)scr_h); }

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

/* Mouse edge-detection */
static uint32_t g_prev_btn = 0;
static uint32_t g_prev_mx  = 0, g_prev_my = 0;

/* Start menu állapot */
static int g_start_menu_open = 0;

/* --- Window kezelők ----------------------------------------------------- */

static window_t *window_find_by_app(app_kind_t app) {
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].open && g_windows[i].app == app) return &g_windows[i];
    }
    return NULL;
}

static int window_new(app_kind_t app, const char *title, int w, int h,
                      draw_fn dr, event_fn ev, key_fn ky) {
    /* Ha már nyitva van ilyen app, csak fokuszáljuk */
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].open && g_windows[i].app == app) {
            g_focus = i;
            return i;
        }
    }
    if (g_window_count >= MAX_WINDOWS) return -1;
    int i = g_window_count++;
    window_t *win = &g_windows[i];
    win->open = 1;
    win->app = app;
    win->w = w; win->h = h;
    /* Pozíció: kaszkád */
    win->x = 60 + (i * 32) % 300;
    win->y = 50 + (i * 28) % 200;
    win->draw = dr; win->click = ev; win->key = ky;
    int j = 0;
    while (title[j] && j < 47) { win->title[j] = title[j]; j++; }
    win->title[j] = 0;
    /* Priv inicializálás 0-ra */
    for (int k = 0; k < APP_PRIV_SIZE; k++) win->priv[k] = 0;
    g_focus = i;
    return i;
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
    int bx = w->x + w->w - 20;
    int by = w->y + 4;
    return (mx >= bx && mx < bx + 16 && my >= by && my < by + 16);
}

static int window_hit_client(window_t *w, int mx, int my) {
    return (mx >= w->x && mx < w->x + w->w &&
            my >= w->y + TITLE_H && my < w->y + w->h);
}

static void draw_window_frame(window_t *win, int focused) {
    /* Árnyék */
    bb_fill_rect(win->x + 3, win->y + 3, win->w, win->h, 0x000010);
    /* Háttér */
    bb_fill_rect(win->x, win->y, win->w, win->h, COLOR_WIN_BG);
    /* Címsor */
    uint32_t titlebar = focused ? COLOR_WIN_TITLE_A : COLOR_WIN_TITLE;
    bb_fill_rect(win->x, win->y, win->w, TITLE_H, titlebar);
    bb_draw_text(win->x + 8, win->y + 6, win->title, COLOR_TEXT, 2);
    /* Bezáró gomb (x) */
    bb_fill_rect(win->x + win->w - 20, win->y + 4, 16, 16, COLOR_ACCENT);
    bb_draw_text(win->x + win->w - 17, win->y + 6, "x", 0xFFFFFF, 2);
    /* Keret */
    bb_frame(win->x, win->y, win->w, win->h, COLOR_FRAME);
    bb_hline(win->x, win->y + TITLE_H, win->w, COLOR_FRAME);
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
    for (int i = 0; s->content[i]; i++) {
        char c = s->content[i];
        if (c == '\n') { yy += line_h; xx = x_start; continue; }
        if (c == '\r') continue;
        if (c == '\t') { xx += 24; continue; }
        if (xx + 6 > max_w) { yy += line_h; xx = x_start; }
        if (yy >= cy && yy < cy + ch - 8 && c >= 32 && c < 127) {
            bb_draw_char(xx, yy, c, COLOR_TEXT, 1);
        }
        xx += 6;
    }

    /* Status */
    int sy = w->y + w->h - 18;
    bb_fill_rect(w->x + 1, sy, w->w - 2, 17, 0x12121A);
    char st[64];
    sstrcpy(st, "Read-only viewer | PgUp/PgDn to scroll");
    bb_draw_text(w->x + 8, sy + 5, st, COLOR_TEXT_DIM, 1);
}

static void app_editor_key(window_t *w, char c) {
    editor_state_t *s = (editor_state_t *)w->priv;
    if (c == 'k') s->scroll = (s->scroll > 0) ? s->scroll - 1 : 0;
    if (c == 'j') s->scroll++;
    if (c == 'g') s->scroll = 0;
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
        uint32_t bg = (i == hover_idx) ? 0x303060 : 0;
        if (bg) bb_fill_rect(ix - 4, iy - 4, DT_ICON_W + 8, DT_ICON_H + 8, bg);
        bb_fill_rect(ix + 24, iy, 32, 32, g_dt_icons[i].color);
        bb_frame(ix + 24, iy, 32, 32, 0x101020);
        int tlen = sstrlen(g_dt_icons[i].label);
        int tw = tlen * 6 - 1;
        bb_draw_text(ix + (DT_ICON_W - tw) / 2, iy + 40, g_dt_icons[i].label,
                     COLOR_TEXT, 1);
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
    }
}

/* --- Tálca + Start menü --------------------------------------------------- */

static void draw_taskbar(uint64_t ticks, int mx, int my) {
    int ty = (int)scr_h - TASKBAR_H;
    bb_fill_rect(0, ty, (int)scr_w, TASKBAR_H, COLOR_TASKBAR);
    bb_hline(0, ty, (int)scr_w, COLOR_TASKBAR_SEP);

    /* Start gomb */
    int start_x = 6, start_y = ty + 4;
    int start_w = 80, start_h = TASKBAR_H - 8;
    int hover = (mx >= start_x && mx < start_x + start_w &&
                 my >= start_y && my < start_y + start_h);
    uint32_t bg = hover ? COLOR_ACCENT : COLOR_ACCENT_DIM;
    bb_fill_rect(start_x, start_y, start_w, start_h, bg);
    bb_frame(start_x, start_y, start_w, start_h, 0x000000);
    bb_draw_text(start_x + 14, start_y + 8, "Start", 0xFFFFFF, 2);

    /* Megnyitott ablakok */
    int tx = start_x + start_w + 8;
    for (int i = 0; i < g_window_count; i++) {
        if (!g_windows[i].open) continue;
        int tbw = 120;
        uint32_t tbg = (i == g_focus) ? 0x404068 : 0x282838;
        bb_fill_rect(tx, ty + 4, tbw, TASKBAR_H - 8, tbg);
        bb_frame(tx, ty + 4, tbw, TASKBAR_H - 8, COLOR_FRAME);
        /* csak az első X karakter */
        char nb[24]; int j = 0;
        while (g_windows[i].title[j] && j < 14) { nb[j] = g_windows[i].title[j]; j++; }
        nb[j] = 0;
        bb_draw_text(tx + 8, ty + 12, nb, COLOR_TEXT, 1);
        tx += tbw + 6;
        if (tx > (int)scr_w - 160) break;
    }

    /* Óra jobbra */
    uint64_t sec = ticks / 100;
    int hr = (int)((sec / 3600) % 24);
    int mn = (int)((sec / 60) % 60);
    int sc = (int)(sec % 60);
    char clk[12], p[4];
    pad2(p, hr); sstrcpy(clk, p); sstrcat(clk, ":");
    pad2(p, mn); sstrcat(clk, p); sstrcat(clk, ":");
    pad2(p, sc); sstrcat(clk, p);
    int clk_x = (int)scr_w - 96;
    bb_fill_rect(clk_x - 6, ty + 4, 92, TASKBAR_H - 8, 0x12121A);
    bb_frame(clk_x - 6, ty + 4, 92, TASKBAR_H - 8, COLOR_FRAME);
    bb_draw_text(clk_x + 2, ty + 12, clk, 0x9FE0FF, 2);
}

static void draw_start_menu(int mx, int my) {
    if (!g_start_menu_open) return;
    int sx = 6, sy = (int)scr_h - TASKBAR_H - 28 - DT_ICON_COUNT * 28;
    int sw = 200, sh = DT_ICON_COUNT * 28 + 16;
    bb_fill_rect(sx, sy, sw, sh, 0x14141E);
    bb_frame(sx, sy, sw, sh, COLOR_FRAME);
    bb_draw_text(sx + 8, sy + 6, "Launch", COLOR_TEXT_DIM, 1);
    for (int i = 0; i < DT_ICON_COUNT; i++) {
        int iy = sy + 20 + i * 28;
        int hov = (mx >= sx && mx < sx + sw && my >= iy && my < iy + 24);
        if (hov) bb_fill_rect(sx + 2, iy, sw - 4, 24, COLOR_BUTTON_HOV);
        bb_fill_rect(sx + 10, iy + 6, 12, 12, g_dt_icons[i].color);
        bb_frame(sx + 10, iy + 6, 12, 12, 0x000000);
        bb_draw_text(sx + 30, iy + 8, g_dt_icons[i].label, COLOR_TEXT, 1);
    }
}

static int start_menu_hit(int mx, int my) {
    if (!g_start_menu_open) return -1;
    int sx = 6, sy = (int)scr_h - TASKBAR_H - 28 - DT_ICON_COUNT * 28;
    int sw = 200;
    for (int i = 0; i < DT_ICON_COUNT; i++) {
        int iy = sy + 20 + i * 28;
        if (mx >= sx && mx < sx + sw && my >= iy && my < iy + 24) return i;
    }
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

void _start() {
    fb = (uint32_t *)get_fb(&scr_w, &scr_h, &pitch);
    if (!fb) { print("desktop: no framebuffer\n"); exit(1); }

    backbuf_size = scr_w * scr_h * 4;
    backbuf = (uint32_t *)malloc(backbuf_size);
    if (!backbuf) { print("desktop: no memory for back buffer\n"); exit(1); }

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

        /* Klikk eseménykezelés (press edge) */
        if (btn_down) {
            /* Start menü hit? */
            if (g_start_menu_open) {
                int sm = start_menu_hit(mx, my);
                if (sm >= 0) {
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

        /* Renderelés ~25Hz */
        if (now - last_draw >= 4) {
            last_draw = now;

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

            flush_all();
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
