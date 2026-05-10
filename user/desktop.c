#include "libc.h"

static uint32_t *fb;
static uint64_t scr_w, scr_h, pitch;
static uint32_t pitch32;

/* --- Minimális 5x7 bitmap font (ASCII 32-126) --- */
static const uint8_t font5x7[][5] = {
  {0x00,0x00,0x00,0x00,0x00}, /* space */
  {0x00,0x00,0x5F,0x00,0x00}, /* ! */
  {0x00,0x07,0x00,0x07,0x00}, /* " */
  {0x14,0x7F,0x14,0x7F,0x14}, /* # */
  {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
  {0x23,0x13,0x08,0x64,0x62}, /* % */
  {0x36,0x49,0x55,0x22,0x50}, /* & */
  {0x00,0x05,0x03,0x00,0x00}, /* ' */
  {0x00,0x1C,0x22,0x41,0x00}, /* ( */
  {0x00,0x41,0x22,0x1C,0x00}, /* ) */
  {0x08,0x2A,0x1C,0x2A,0x08}, /* * */
  {0x08,0x08,0x3E,0x08,0x08}, /* + */
  {0x00,0x50,0x30,0x00,0x00}, /* , */
  {0x08,0x08,0x08,0x08,0x08}, /* - */
  {0x00,0x60,0x60,0x00,0x00}, /* . */
  {0x20,0x10,0x08,0x04,0x02}, /* / */
  {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
  {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
  {0x42,0x61,0x51,0x49,0x46}, /* 2 */
  {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
  {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
  {0x27,0x45,0x45,0x45,0x39}, /* 5 */
  {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
  {0x01,0x71,0x09,0x05,0x03}, /* 7 */
  {0x36,0x49,0x49,0x49,0x36}, /* 8 */
  {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
  {0x00,0x36,0x36,0x00,0x00}, /* : */
  {0x00,0x56,0x36,0x00,0x00}, /* ; */
  {0x00,0x08,0x14,0x22,0x41}, /* < */
  {0x14,0x14,0x14,0x14,0x14}, /* = */
  {0x41,0x22,0x14,0x08,0x00}, /* > */
  {0x02,0x01,0x51,0x09,0x06}, /* ? */
  {0x32,0x49,0x79,0x41,0x3E}, /* @ */
  {0x7E,0x11,0x11,0x11,0x7E}, /* A */
  {0x7F,0x49,0x49,0x49,0x36}, /* B */
  {0x3E,0x41,0x41,0x41,0x22}, /* C */
  {0x7F,0x41,0x41,0x22,0x1C}, /* D */
  {0x7F,0x49,0x49,0x49,0x41}, /* E */
  {0x7F,0x09,0x09,0x01,0x01}, /* F */
  {0x3E,0x41,0x41,0x51,0x32}, /* G */
  {0x7F,0x08,0x08,0x08,0x7F}, /* H */
  {0x00,0x41,0x7F,0x41,0x00}, /* I */
  {0x20,0x40,0x41,0x3F,0x01}, /* J */
  {0x7F,0x08,0x14,0x22,0x41}, /* K */
  {0x7F,0x40,0x40,0x40,0x40}, /* L */
  {0x7F,0x02,0x04,0x02,0x7F}, /* M */
  {0x7F,0x04,0x08,0x10,0x7F}, /* N */
  {0x3E,0x41,0x41,0x41,0x3E}, /* O */
  {0x7F,0x09,0x09,0x09,0x06}, /* P */
  {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
  {0x7F,0x09,0x19,0x29,0x46}, /* R */
  {0x46,0x49,0x49,0x49,0x31}, /* S */
  {0x01,0x01,0x7F,0x01,0x01}, /* T */
  {0x3F,0x40,0x40,0x40,0x3F}, /* U */
  {0x1F,0x20,0x40,0x20,0x1F}, /* V */
  {0x7F,0x20,0x18,0x20,0x7F}, /* W */
  {0x63,0x14,0x08,0x14,0x63}, /* X */
  {0x03,0x04,0x78,0x04,0x03}, /* Y */
  {0x61,0x51,0x49,0x45,0x43}, /* Z */
  {0x00,0x00,0x7F,0x41,0x41}, /* [ */
  {0x02,0x04,0x08,0x10,0x20}, /* \ */
  {0x41,0x41,0x7F,0x00,0x00}, /* ] */
  {0x04,0x02,0x01,0x02,0x04}, /* ^ */
  {0x40,0x40,0x40,0x40,0x40}, /* _ */
  {0x00,0x01,0x02,0x04,0x00}, /* ` */
  {0x20,0x54,0x54,0x54,0x78}, /* a */
  {0x7F,0x48,0x44,0x44,0x38}, /* b */
  {0x38,0x44,0x44,0x44,0x20}, /* c */
  {0x38,0x44,0x44,0x48,0x7F}, /* d */
  {0x38,0x54,0x54,0x54,0x18}, /* e */
  {0x08,0x7E,0x09,0x01,0x02}, /* f */
  {0x08,0x14,0x54,0x54,0x3C}, /* g */
  {0x7F,0x08,0x04,0x04,0x78}, /* h */
  {0x00,0x44,0x7D,0x40,0x00}, /* i */
  {0x20,0x40,0x44,0x3D,0x00}, /* j */
  {0x00,0x7F,0x10,0x28,0x44}, /* k */
  {0x00,0x41,0x7F,0x40,0x00}, /* l */
  {0x7C,0x04,0x18,0x04,0x78}, /* m */
  {0x7C,0x08,0x04,0x04,0x78}, /* n */
  {0x38,0x44,0x44,0x44,0x38}, /* o */
  {0x7C,0x14,0x14,0x14,0x08}, /* p */
  {0x08,0x14,0x14,0x18,0x7C}, /* q */
  {0x7C,0x08,0x04,0x04,0x08}, /* r */
  {0x48,0x54,0x54,0x54,0x20}, /* s */
  {0x04,0x3F,0x44,0x40,0x20}, /* t */
  {0x3C,0x40,0x40,0x20,0x7C}, /* u */
  {0x1C,0x20,0x40,0x20,0x1C}, /* v */
  {0x3C,0x40,0x30,0x40,0x3C}, /* w */
  {0x44,0x28,0x10,0x28,0x44}, /* x */
  {0x0C,0x50,0x50,0x50,0x3C}, /* y */
  {0x44,0x64,0x54,0x4C,0x44}, /* z */
  {0x00,0x08,0x36,0x41,0x00}, /* { */
  {0x00,0x00,0x7F,0x00,0x00}, /* | */
  {0x00,0x41,0x36,0x08,0x00}, /* } */
  {0x08,0x08,0x2A,0x1C,0x08}, /* ~ */
};

/* --- Rajzoló primitívek --- */

static inline void putpx(uint32_t x, uint32_t y, uint32_t c) {
    if (x < scr_w && y < scr_h)
        *(uint32_t *)((uint8_t *)fb + y * pitch + x * 4) = c;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c) {
    for (uint32_t dy = 0; dy < h && (y+dy) < scr_h; dy++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb + (y+dy) * pitch);
        for (uint32_t dx = 0; dx < w && (x+dx) < scr_w; dx++)
            row[x+dx] = c;
    }
}

static uint32_t blend(uint32_t bg, uint32_t fg, uint8_t alpha) {
    uint32_t rb = bg & 0xFF00FF, g = bg & 0x00FF00;
    uint32_t frb = fg & 0xFF00FF, fg_ = fg & 0x00FF00;
    rb = ((rb * (255-alpha) + frb * alpha) >> 8) & 0xFF00FF;
    g  = ((g  * (255-alpha) + fg_ * alpha) >> 8) & 0x00FF00;
    return rb | g;
}

static void draw_char(uint32_t x, uint32_t y, char ch, uint32_t color, int scale) {
    if (ch < 32 || ch > 126) return;
    const uint8_t *glyph = font5x7[ch - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        putpx(x + col*scale + sx, y + row*scale + sy, color);
            }
        }
    }
}

static void draw_text(uint32_t x, uint32_t y, const char *s, uint32_t color, int scale) {
    while (*s) {
        draw_char(x, y, *s, color, scale);
        x += 6 * scale;
        s++;
    }
}

static void draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c, int r) {
    fill_rect(x+r, y, w-2*r, h, c);
    fill_rect(x, y+r, r, h-2*r, c);
    fill_rect(x+w-r, y+r, r, h-2*r, c);
    /* corners */
    for (int cy = 0; cy < r; cy++)
        for (int cx = 0; cx < r; cx++) {
            int dx = r-1-cx, dy = r-1-cy;
            if (dx*dx + dy*dy <= r*r) {
                putpx(x+cx, y+cy, c);
                putpx(x+w-1-cx, y+cy, c);
                putpx(x+cx, y+h-1-cy, c);
                putpx(x+w-1-cx, y+h-1-cy, c);
            }
        }
}

/* --- Háttér --- */

static void draw_wallpaper(void) {
    for (uint32_t y = 0; y < scr_h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb + y * pitch);
        uint8_t r = 20 + (y * 40) / scr_h;
        uint8_t g = 30 + (y * 60) / scr_h;
        uint8_t b = 80 + (y * 100) / scr_h;
        uint32_t c = (r << 16) | (g << 8) | b;
        for (uint32_t x = 0; x < scr_w; x++) row[x] = c;
    }
}

/* --- Tálca --- */

#define TASKBAR_H 36

static void draw_taskbar(uint64_t ticks) {
    uint32_t ty = scr_h - TASKBAR_H;
    /* Sötét háttér */
    for (uint32_t y = ty; y < scr_h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb + y * pitch);
        for (uint32_t x = 0; x < scr_w; x++)
            row[x] = 0x1A1A2E;
    }
    /* Felső vonal */
    fill_rect(0, ty, scr_w, 1, 0x4A4A6E);

    /* RexOS logó */
    fill_rect(8, ty+6, 24, 24, 0xE94560);
    draw_text(10, ty+10, "R", 0xFFFFFF, 2);
    draw_text(46, ty+12, "RexOS", 0xCCCCCC, 2);

    /* Óra */
    uint64_t sec = ticks / 100;
    uint64_t min = (sec / 60) % 60;
    uint64_t hr  = (sec / 3600) % 24;
    sec = sec % 60;
    char clock[9];
    clock[0] = '0' + hr/10; clock[1] = '0' + hr%10; clock[2] = ':';
    clock[3] = '0' + min/10; clock[4] = '0' + min%10; clock[5] = ':';
    clock[6] = '0' + sec/10; clock[7] = '0' + sec%10; clock[8] = 0;
    draw_text(scr_w - 100, ty+12, clock, 0xBBBBBB, 2);
}

/* --- Ablak --- */

static void draw_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         const char *title, uint32_t title_color, int active) {
    /* Árnyék */
    fill_rect(x+4, y+4, w, h, 0x0A0A1A);
    /* Ablak test */
    fill_rect(x, y, w, h, 0x202038);
    /* Címsor */
    uint32_t tc = active ? title_color : 0x353550;
    fill_rect(x, y, w, 28, tc);
    draw_text(x+10, y+8, title, 0xFFFFFF, 2);
    /* Bezáró gomb */
    fill_rect(x+w-24, y+4, 20, 20, 0xE94560);
    draw_text(x+w-20, y+6, "x", 0xFFFFFF, 2);
    /* Keret */
    fill_rect(x, y, w, 1, 0x6A6A8E);
    fill_rect(x, y+h-1, w, 1, 0x3A3A5E);
    fill_rect(x, y, 1, h, 0x6A6A8E);
    fill_rect(x+w-1, y, 1, h, 0x3A3A5E);
}

/* --- Ablak tartalom --- */

static void draw_file_manager(uint32_t x, uint32_t y) {
    const char *files[] = {"hello.txt","test.txt","hello.elf","shell.elf","gui.elf","ls.elf","cat.elf","memtest.elf"};
    uint32_t icon_colors[] = {0x4FC3F7,0x4FC3F7,0x66BB6A,0x66BB6A,0xFFA726,0x66BB6A,0x66BB6A,0x66BB6A};
    for (int i = 0; i < 8; i++) {
        uint32_t fx = x + 16 + (i % 4) * 120;
        uint32_t fy = y + 40 + (i / 4) * 64;
        fill_rect(fx, fy, 32, 32, icon_colors[i]);
        draw_text(fx-4, fy+36, files[i], 0xCCCCCC, 1);
    }
}

static void draw_sysinfo(uint32_t x, uint32_t y, uint64_t ticks) {
    draw_text(x+16, y+36, "RexOS v0.1.0-alpha", 0x4FC3F7, 2);
    draw_text(x+16, y+60, "Architecture: x86_64", 0xAAAAAA, 1);
    draw_text(x+16, y+74, "RAM: 254 MB", 0xAAAAAA, 1);
    draw_text(x+16, y+88, "Display: 1280x800", 0xAAAAAA, 1);
    draw_text(x+16, y+102, "Scheduler: Preemptive", 0xAAAAAA, 1);
    draw_text(x+16, y+116, "User Mode: Ring 3 active", 0xAAAAAA, 1);
    draw_text(x+16, y+140, "Press ESC to exit desktop", 0x777777, 1);
}

/* --- Egér kurzor --- */

#define CURSOR_W 12
#define CURSOR_H 16

/* Egyszerű nyíl kurzor (1 = fehér, 2 = fekete keret) */
static const uint8_t cursor_bitmap[CURSOR_H][CURSOR_W] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,2,2,2,2,2,0},
    {2,1,1,2,1,1,2,0,0,0,0,0},
    {2,1,2,0,2,1,1,2,0,0,0,0},
    {2,2,0,0,2,1,1,2,0,0,0,0},
    {2,0,0,0,0,2,1,1,2,0,0,0},
    {0,0,0,0,0,2,2,2,0,0,0,0},
};

static uint32_t cursor_save[CURSOR_W * CURSOR_H];
static uint32_t old_mx = 0, old_my = 0;

static void save_under_cursor(uint32_t mx, uint32_t my) {
    for (int y = 0; y < CURSOR_H; y++)
        for (int x = 0; x < CURSOR_W; x++) {
            uint32_t px = mx+x, py = my+y;
            if (px < scr_w && py < scr_h)
                cursor_save[y*CURSOR_W+x] = *(uint32_t*)((uint8_t*)fb + py*pitch + px*4);
        }
}

static void restore_under_cursor(uint32_t mx, uint32_t my) {
    for (int y = 0; y < CURSOR_H; y++)
        for (int x = 0; x < CURSOR_W; x++) {
            uint32_t px = mx+x, py = my+y;
            if (px < scr_w && py < scr_h)
                *(uint32_t*)((uint8_t*)fb + py*pitch + px*4) = cursor_save[y*CURSOR_W+x];
        }
}

static void draw_cursor(uint32_t mx, uint32_t my) {
    for (int y = 0; y < CURSOR_H; y++)
        for (int x = 0; x < CURSOR_W; x++) {
            uint8_t v = cursor_bitmap[y][x];
            if (v == 1) putpx(mx+x, my+y, 0xFFFFFF);
            else if (v == 2) putpx(mx+x, my+y, 0x000000);
        }
}

/* --- Main --- */

void _start() {
    fb = (uint32_t *)get_fb(&scr_w, &scr_h, &pitch);
    if (!fb) { print("desktop: no framebuffer\n"); exit(1); }
    pitch32 = pitch / 4;

    draw_wallpaper();
    /* Első rajzolás */
    draw_taskbar(0);
    draw_window(60, 40, 520, 280, "File Manager", 0x1565C0, 1);
    draw_file_manager(60, 40);
    draw_window(620, 80, 420, 240, "System Info", 0x2E7D32, 0);
    draw_sysinfo(620, 80, 0);

    /* Kurzor pozíció lekérdezése (a kernel középre inicializálja) */
    uint32_t mx0, my0, mb0;
    get_mouse(&mx0, &my0, &mb0);
    old_mx = mx0; old_my = my0;
    save_under_cursor(old_mx, old_my);
    draw_cursor(old_mx, old_my);

    /* Main loop */
    uint64_t last_tick = 0;
    int running = 1;
    while (running) {
        uint64_t now = get_ticks();

        /* Egér lekérdezése */
        uint32_t mx, my, mb;
        get_mouse(&mx, &my, &mb);

        /* Kurzor mozgatás: restore old, save new, draw new */
        if (mx != old_mx || my != old_my) {
            restore_under_cursor(old_mx, old_my);
            save_under_cursor(mx, my);
            draw_cursor(mx, my);
            old_mx = mx; old_my = my;
        }

        /* Óra frissítés ~500ms-ként (csak a tálca-részt rajzoljuk újra) */
        if (now - last_tick >= 50) {
            last_tick = now;
            restore_under_cursor(old_mx, old_my);
            draw_taskbar(now);
            save_under_cursor(old_mx, old_my);
            draw_cursor(old_mx, old_my);
        }

        char key = kbd_poll();
        if (key == 27) running = 0; /* ESC */
        yield();
    }

    /* Képernyő törlése kilépéskor */
    for (uint32_t y = 0; y < scr_h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb + y * pitch);
        for (uint32_t x = 0; x < scr_w; x++) row[x] = 0;
    }
    exit(0);
}

