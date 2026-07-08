/* =============================================================================
 *  RexOS Desktop - Taskbar (bottom)
 *  Start button | running app pills | clock
 * ========================================================================== */
#include "desktop.h"

static int s_start_open = 0;
static int s_start_anim = 0;       /* 0..255 */

#define START_W 280
#define START_ITEM_H 38

/* ----- Public render -------------------------------------------------------- */
void tb_init(void) {
    s_start_open = 0;
    s_start_anim = 0;
}

static void format_clock(char *buf) {
    rtc_time_t t;
    get_time(&t);
    /* "HH:MM" */
    buf[0] = '0' + (t.hour / 10) % 10;
    buf[1] = '0' + t.hour % 10;
    buf[2] = ':';
    buf[3] = '0' + (t.minute / 10) % 10;
    buf[4] = '0' + t.minute % 10;
    buf[5] = 0;
}

static void format_date(char *buf) {
    rtc_time_t t;
    get_time(&t);
    /* "YYYY-MM-DD" */
    int year = (int)t.year;
    int yi = 0;
    if (year >= 1000) buf[yi++] = '0' + (year / 1000) % 10;
    if (year >= 100)  buf[yi++] = '0' + (year / 100)  % 10;
    if (year >= 10)   buf[yi++] = '0' + (year / 10)   % 10;
    buf[yi++] = '0' + year % 10;
    buf[yi++] = '-';
    buf[yi++] = '0' + (t.month / 10) % 10;
    buf[yi++] = '0' + t.month % 10;
    buf[yi++] = '-';
    buf[yi++] = '0' + (t.day / 10) % 10;
    buf[yi++] = '0' + t.day % 10;
    buf[yi] = 0;
}

/* ----- Geometry helpers ---------------------------------------------------- */
static void start_btn_rect(int *x, int *y, int *w, int *h) {
    *x = 6; *y = (int)g_scr_h - TASKBAR_H + 6; *w = 90; *h = TASKBAR_H - 12;
}

static int tasks_x_start(void) { return 110; }

static void task_pill_rect(int idx, int *x, int *y, int *w, int *h) {
    *w = 200;
    *h = TASKBAR_H - 12;
    *x = tasks_x_start() + idx * (*w + 6);
    *y = (int)g_scr_h - TASKBAR_H + 6;
}

static void clock_rect(int *x, int *y, int *w, int *h) {
    *w = 96;
    *h = TASKBAR_H - 12;
    *x = (int)g_scr_w - *w - 8;
    *y = (int)g_scr_h - TASKBAR_H + 6;
}

/* ----- Start menu ---------------------------------------------------------- */
static void render_start_menu(void) {
    if (s_start_anim == 0) return;
    int total_h = 0;
    /* For each app in registry: an item. Plus "Power off" footer. */
    int items = g_app_count + 1;
    total_h = 56 + items * START_ITEM_H + 12;

    int sx = 6;
    int sy = (int)g_scr_h - TASKBAR_H - total_h - 6;

    /* Animation: slide-up from bottom */
    int anim_off = (255 - s_start_anim) * 30 / 255;
    sy += anim_off;

    /* Frosted-glass panel: soft shadow, translucent rounded fill, top highlight. */
    rd_shadow(sx, sy, START_W, total_h, 10, 14);
    rd_blend_rounded(sx, sy, START_W, total_h, 10, 0x0A0E14, 224);
    rd_blend_rect(sx + 10, sy + 1, START_W - 20, 1, 0xFFFFFF, 22);
    rd_rounded_outline(sx, sy, START_W, total_h, 10, COL_WIN_BORDER);

    /* Header */
    rd_text_bold(sx + 16, sy + 24, "RexOS Menu", UI_PX_LARGE, COL_TEXT);
    rd_text(sx + 16, sy + 44, "Applications", UI_PX_SMALL, COL_TEXT_DIM);

    /* App items */
    int iy = sy + 56;
    for (int i = 0; i < g_app_count; i++) {
        const app_def_t *a = g_apps[i];
        int hovered = pt_in_rect(g_mouse.x, g_mouse.y, sx + 6, iy, START_W - 12, START_ITEM_H);
        if (hovered) {
            rd_rounded(sx + 6, iy, START_W - 12, START_ITEM_H, 6, COL_HOVER);
        }
        icon_draw(a->icon, sx + 14, iy + 9, 20, COL_ACCENT, 0);
        rd_text(sx + 44, iy + 24, a->name, UI_PX, COL_TEXT);
        iy += START_ITEM_H;
    }

    /* Footer separator */
    rd_rect(sx + 16, iy + 4, START_W - 32, 1, COL_WIN_BORDER);
    iy += 8;

    /* Power off */
    int hovered = pt_in_rect(g_mouse.x, g_mouse.y, sx + 6, iy, START_W - 12, START_ITEM_H);
    if (hovered) {
        rd_rounded(sx + 6, iy, START_W - 12, START_ITEM_H, 6, COL_HOVER);
    }
    icon_draw(ICON_POWER, sx + 14, iy + 9, 20, COL_RED, 0);
    rd_text(sx + 44, iy + 24, "Exit Desktop", UI_PX, COL_TEXT);
}

/* ----- Taskbar render ------------------------------------------------------ */
void tb_render(void) {
    int tby = (int)g_scr_h - TASKBAR_H;

    /* Glassmorphism bar: translucent dark fill lets the wallpaper (already in
     * the back buffer) show through, plus a 1px top highlight for a subtle
     * "frosted glass" edge and the existing accent border underneath it. */
    rd_blend_rect(0, tby, (int)g_scr_w, TASKBAR_H, COL_TASKBAR_BG, 214);
    rd_blend_rect(0, tby, (int)g_scr_w, 1, 0xFFFFFF, 24);
    rd_rect(0, tby + 1, (int)g_scr_w, 1, COL_TASKBAR_BORDER);

    /* Start button */
    int sx, sy, sw, sh;
    start_btn_rect(&sx, &sy, &sw, &sh);
    int hov = pt_in_rect(g_mouse.x, g_mouse.y, sx, sy, sw, sh);
    uint32_t btn_col = s_start_open ? COL_TASKBAR_BTN_ACT
                       : (hov ? COL_TASKBAR_BTN_HOV : COL_TASKBAR_BTN);
    rd_rounded(sx, sy, sw, sh, 6, btn_col);
    /* "Rex" text + dot */
    int dot_x = sx + 12, dot_y = sy + sh / 2;
    rd_circle_fill(dot_x, dot_y, 4, s_start_open ? COL_TEXT_ON_ACCENT : COL_ACCENT);
    rd_text_bold(sx + 24, sy + sh / 2 + 5, "RexOS",
                 UI_PX, s_start_open ? COL_TEXT_ON_ACCENT : COL_TEXT);

    /* Task pills (one per visible window) */
    int pill_idx = 0;
    /* Iterate z-order bottom-to-top so pinned order is z-stable */
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        window_t *w = wm_get(i);
        if (!w) break;
        if (!w->used || !w->visible) continue;
        int px, py, pw, ph;
        task_pill_rect(pill_idx++, &px, &py, &pw, &ph);
        if (px + pw > (int)g_scr_w - 110) break; /* overflow */

        int p_hov = pt_in_rect(g_mouse.x, g_mouse.y, px, py, pw, ph);
        int active = w->focused && !w->minimized;
        uint32_t pcol = active ? COL_TASKBAR_BTN_ACT
                              : (p_hov ? COL_TASKBAR_BTN_HOV : COL_TASKBAR_BTN);
        rd_rounded(px, py, pw, ph, 6, pcol);
        /* Icon */
        icon_draw(w->icon, px + 8, py + 6, 20,
                  active ? COL_TEXT_ON_ACCENT : COL_TEXT, 0);
        /* Title */
        uint32_t txt = active ? COL_TEXT_ON_ACCENT : COL_TEXT;
        rd_text(px + 36, py + ph / 2 + 5, w->title, UI_PX, txt);
        /* Minimized indicator: dim left edge */
        if (w->minimized) {
            rd_rect(px, py + ph / 4, 2, ph / 2, COL_TEXT_DIM);
        } else if (active) {
            rd_rect(px, py + ph / 4, 3, ph / 2, COL_TEXT_ON_ACCENT);
        }
    }

    /* Clock */
    int cx, cy, cw, ch;
    clock_rect(&cx, &cy, &cw, &ch);
    char tbuf[8]; format_clock(tbuf);
    char dbuf[16]; format_date(dbuf);
    rd_text_bold(cx + cw - rd_text_w(tbuf, UI_PX) - 8, cy + 14, tbuf, UI_PX, COL_TEXT);
    rd_text(cx + cw - rd_text_w(dbuf, UI_PX_SMALL) - 8, cy + 28, dbuf, UI_PX_SMALL, COL_TEXT_MUTED);

    /* FPS indicator (small, left of clock) */
    char fbuf[16];
    char fnum[8];
    d_int_to_str(g_fps, fnum);
    d_strcpy(fbuf, fnum, sizeof(fbuf));
    d_strcat(fbuf, " FPS", sizeof(fbuf));
    int fps_w = rd_text_w(fbuf, UI_PX_SMALL);
    uint32_t fps_col = g_fps >= 30 ? COL_GREEN : (g_fps >= 15 ? COL_YELLOW : COL_RED);
    rd_text(cx - fps_w - 16, cy + 21, fbuf, UI_PX_SMALL, fps_col);

    /* Animate start menu */
    if (s_start_open && s_start_anim < 255) s_start_anim += 32;
    if (!s_start_open && s_start_anim > 0)  s_start_anim -= 32;
    if (s_start_anim < 0) s_start_anim = 0;
    if (s_start_anim > 255) s_start_anim = 255;
    render_start_menu();
}

/* ----- Taskbar input ------------------------------------------------------- */
extern int g_request_exit;

void tb_input(void) {
    if (!g_mouse.clicked) return;

    /* Click outside: close start menu */
    if (s_start_open) {
        int items = g_app_count + 1;
        int total_h = 56 + items * START_ITEM_H + 12;
        int sx = 6;
        int sy = (int)g_scr_h - TASKBAR_H - total_h - 6;

        /* Hit start menu items? */
        if (pt_in_rect(g_mouse.x, g_mouse.y, sx, sy, START_W, total_h)) {
            int iy = sy + 56;
            for (int i = 0; i < g_app_count; i++) {
                if (pt_in_rect(g_mouse.x, g_mouse.y, sx + 6, iy, START_W - 12, START_ITEM_H)) {
                    wm_open(g_apps[i]);
                    s_start_open = 0;
                    return;
                }
                iy += START_ITEM_H;
            }
            iy += 8;
            if (pt_in_rect(g_mouse.x, g_mouse.y, sx + 6, iy, START_W - 12, START_ITEM_H)) {
                g_request_exit = 1;
                s_start_open = 0;
                return;
            }
            return;  /* swallow click */
        } else {
            s_start_open = 0;
        }
    }

    /* Click on start button */
    int sx, sy, sw, sh;
    start_btn_rect(&sx, &sy, &sw, &sh);
    if (pt_in_rect(g_mouse.x, g_mouse.y, sx, sy, sw, sh)) {
        s_start_open = !s_start_open;
        return;
    }

    /* Click on task pills */
    int pill_idx = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        window_t *w = wm_get(i);
        if (!w) break;
        if (!w->used || !w->visible) continue;
        int px, py, pw, ph;
        task_pill_rect(pill_idx++, &px, &py, &pw, &ph);
        if (pt_in_rect(g_mouse.x, g_mouse.y, px, py, pw, ph)) {
            if (w->focused && !w->minimized) wm_toggle_minimize(w);
            else { w->minimized = 0; wm_focus(w); }
            return;
        }
    }
}
