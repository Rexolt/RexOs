/* =============================================================================
 *  RexOS Desktop - Window Manager
 *  Floating windows with drag, focus, close/min, z-ordering.
 * ========================================================================== */
#include "desktop.h"

/* Static window pool. Z-order is implicit via s_z[] (front == higher index). */
static window_t s_pool[WM_MAX_WINDOWS];
static int      s_z[WM_MAX_WINDOWS];   /* indices into s_pool, [0..s_z_count) */
static int      s_z_count = 0;
static int      s_next_id = 1;

void wm_init(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        s_pool[i].used = 0;
        s_pool[i].state = 0;
    }
    s_z_count = 0;
}

window_t *wm_focused(void) {
    if (s_z_count == 0) return 0;
    int idx = s_z[s_z_count - 1];
    return &s_pool[idx];
}

void wm_focus(window_t *w) {
    if (!w || !w->used) return;
    /* Find in z-order and move to top */
    int idx = (int)(w - s_pool);
    int found = -1;
    for (int i = 0; i < s_z_count; i++) {
        if (s_z[i] == idx) { found = i; break; }
    }
    if (found < 0) return;
    /* Mark all others unfocused */
    for (int i = 0; i < s_z_count; i++) s_pool[s_z[i]].focused = 0;
    /* Shift left and append */
    for (int i = found; i < s_z_count - 1; i++) s_z[i] = s_z[i + 1];
    s_z[s_z_count - 1] = idx;
    w->focused = 1;
    if (w->minimized) w->minimized = 0;
}

window_t *wm_open(const app_def_t *app) {
    if (!app) return 0;
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!s_pool[i].used) { slot = i; break; }
    }
    if (slot < 0) return 0;

    window_t *w = &s_pool[slot];
    w->used = 1;
    w->visible = 1;
    w->minimized = 0;
    w->id = s_next_id++;
    w->app = app;
    w->state = 0;
    w->dragging = 0;
    w->icon = app->icon;
    /* Title from app name */
    int i = 0;
    while (app->name[i] && i < 63) { w->title[i] = app->name[i]; i++; }
    w->title[i] = 0;

    w->w = app->default_w;
    w->h = app->default_h;
    /* Stagger position */
    int stagger = (s_next_id * 24) % 200;
    w->x = 60 + stagger;
    w->y = 60 + stagger;
    /* Clamp into screen */
    if (w->x + w->w > (int)g_scr_w - 20) w->x = (int)g_scr_w - 20 - w->w;
    if (w->y + w->h > (int)g_scr_h - TASKBAR_H - 20) w->y = (int)g_scr_h - TASKBAR_H - 20 - w->h;
    if (w->x < 8) w->x = 8;
    if (w->y < 8) w->y = 8;

    /* Append to z-order */
    s_z[s_z_count++] = slot;
    /* Lifecycle hook */
    if (app->on_open) app->on_open(w);

    /* Focus the new window */
    wm_focus(w);
    return w;
}

void wm_close(window_t *w) {
    if (!w || !w->used) return;
    if (w->app && w->app->on_close) w->app->on_close(w);
    /* free per-app state if allocated */
    if (w->state) { free(w->state); w->state = 0; }
    /* remove from z order */
    int idx = (int)(w - s_pool);
    for (int i = 0; i < s_z_count; i++) {
        if (s_z[i] == idx) {
            for (int j = i; j < s_z_count - 1; j++) s_z[j] = s_z[j + 1];
            s_z_count--;
            break;
        }
    }
    w->used = 0;
    w->visible = 0;
    /* refocus topmost */
    if (s_z_count > 0) {
        for (int i = 0; i < s_z_count; i++) s_pool[s_z[i]].focused = 0;
        s_pool[s_z[s_z_count - 1]].focused = 1;
    }
}

void wm_toggle_minimize(window_t *w) {
    if (!w || !w->used) return;
    if (w->minimized) {
        w->minimized = 0;
        wm_focus(w);
    } else {
        w->minimized = 1;
        w->focused = 0;
    }
}

int wm_count_visible(void) {
    int c = 0;
    for (int i = 0; i < s_z_count; i++) {
        window_t *w = &s_pool[s_z[i]];
        if (w->visible && !w->minimized) c++;
    }
    return c;
}

window_t *wm_get(int idx) {
    if (idx < 0 || idx >= s_z_count) return 0;
    return &s_pool[s_z[idx]];
}

void wm_content_rect(window_t *w, int *cx, int *cy, int *cw, int *ch) {
    *cx = w->x + WIN_BORDER_W;
    *cy = w->y + WIN_TITLEBAR_H;
    *cw = w->w - 2 * WIN_BORDER_W;
    *ch = w->h - WIN_TITLEBAR_H - WIN_BORDER_W;
    if (*cw < 0) *cw = 0;
    if (*ch < 0) *ch = 0;
}

window_t *wm_topmost_at(int x, int y) {
    /* iterate top-down */
    for (int i = s_z_count - 1; i >= 0; i--) {
        window_t *w = &s_pool[s_z[i]];
        if (!w->visible || w->minimized) continue;
        if (pt_in_rect(x, y, w->x, w->y, w->w, w->h)) return w;
    }
    return 0;
}

/* ----- Title bar buttons --------------------------------------------------- */
/* Three buttons at right of titlebar: minimize, maximize, close. */
static void titlebar_btn_rects(window_t *w, int *bx, int *by, int *bs, int *gap) {
    *bs = 14;
    *gap = 8;
    *by = w->y + (WIN_TITLEBAR_H - *bs) / 2;
    *bx = w->x + w->w - WIN_BORDER_W - *bs - 12;
}

static int titlebar_hit(window_t *w, int mx, int my, int *which) {
    int bx, by, bs, gap;
    titlebar_btn_rects(w, &bx, &by, &bs, &gap);
    /* close (rightmost) */
    if (pt_in_rect(mx, my, bx, by, bs, bs)) { *which = 2; return 1; }
    /* maximize */
    int mxbx = bx - bs - gap;
    if (pt_in_rect(mx, my, mxbx, by, bs, bs)) { *which = 1; return 1; }
    /* minimize */
    int mibx = mxbx - bs - gap;
    if (pt_in_rect(mx, my, mibx, by, bs, bs)) { *which = 0; return 1; }
    return 0;
}

/* ----- Per-window rendering ------------------------------------------------ */
static void render_window(window_t *w) {
    if (!w->used || !w->visible || w->minimized) return;

    /* Soft drop shadow */
    rd_shadow(w->x, w->y, w->w, w->h, WIN_RADIUS, WIN_SHADOW_SPREAD);

    uint32_t border_col = w->focused ? COL_WIN_BORDER_FOCUS : COL_WIN_BORDER;
    uint32_t titlebar_col = w->focused ? COL_TITLEBAR : COL_TITLEBAR_INACTIVE;
    uint32_t title_text = w->focused ? COL_TITLEBAR_TEXT : COL_TITLEBAR_TEXT_INACTIVE;

    /* Window body (rounded rect) - single color, full window */
    rd_rounded(w->x, w->y, w->w, w->h, WIN_RADIUS, COL_WIN_BG);
    /* Border */
    rd_rounded_outline(w->x, w->y, w->w, w->h, WIN_RADIUS, border_col);

    /* Titlebar (clip to top portion of rounded rect) */
    rd_set_clip(w->x, w->y, w->w, WIN_TITLEBAR_H);
    rd_rounded(w->x, w->y, w->w, WIN_TITLEBAR_H + WIN_RADIUS, WIN_RADIUS, titlebar_col);
    /* Bottom border line of titlebar */
    rd_rect(w->x + 1, w->y + WIN_TITLEBAR_H - 1, w->w - 2, 1, COL_WIN_BORDER);
    rd_clear_clip();

    /* Icon + title text */
    int icon_size = 16;
    int icon_x = w->x + 12;
    int icon_y = w->y + (WIN_TITLEBAR_H - icon_size) / 2;
    icon_draw(w->icon, icon_x, icon_y, icon_size, title_text, 0);
    int tx = icon_x + icon_size + 8;
    int ty = w->y + WIN_TITLEBAR_H / 2 + 4;
    rd_text(tx, ty, w->title, UI_PX, title_text);

    /* Buttons */
    int bx, by, bs, gap;
    titlebar_btn_rects(w, &bx, &by, &bs, &gap);
    /* close */
    rd_rounded(bx, by, bs, bs, 3, COL_BTN_CLOSE);
    /* maximize */
    rd_rounded(bx - bs - gap, by, bs, bs, 3, COL_BTN_MAX);
    /* minimize */
    rd_rounded(bx - 2 * (bs + gap), by, bs, bs, 3, COL_BTN_MIN);

    /* Render content via app callback - clip to content area */
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    rd_set_clip(cx, cy, cw, ch);
    if (w->app && w->app->on_render) w->app->on_render(w);
    rd_clear_clip();
}

void wm_render_all(void) {
    /* Bottom-to-top */
    for (int i = 0; i < s_z_count; i++) {
        render_window(&s_pool[s_z[i]]);
    }
}

/* ----- Input dispatch ------------------------------------------------------ */
void wm_input_frame(void) {
    /* Drag continues regardless of cursor position */
    window_t *focused = wm_focused();
    if (focused && focused->dragging) {
        if (g_mouse.down) {
            focused->x = g_mouse.x - focused->drag_off_x;
            focused->y = g_mouse.y - focused->drag_off_y;
            /* Clamp */
            if (focused->y < 0) focused->y = 0;
            if (focused->x < -focused->w + 80) focused->x = -focused->w + 80;
            if (focused->x > (int)g_scr_w - 80) focused->x = (int)g_scr_w - 80;
            if (focused->y > (int)g_scr_h - TASKBAR_H - 16) focused->y = (int)g_scr_h - TASKBAR_H - 16;
            return;
        } else {
            focused->dragging = 0;
        }
    }

    if (g_mouse.clicked) {
        /* Find topmost window under cursor */
        window_t *w = wm_topmost_at(g_mouse.x, g_mouse.y);
        if (w) {
            if (w != wm_focused()) wm_focus(w);

            /* Titlebar buttons? */
            if (g_mouse.y < w->y + WIN_TITLEBAR_H) {
                int which = -1;
                if (titlebar_hit(w, g_mouse.x, g_mouse.y, &which)) {
                    if (which == 2) { wm_close(w); return; }
                    if (which == 1) {
                        /* Maximize / restore */
                        if (w->w < (int)g_scr_w - 16) {
                            w->x = 8; w->y = 8;
                            w->w = (int)g_scr_w - 16;
                            w->h = (int)g_scr_h - TASKBAR_H - 16;
                        } else {
                            w->w = w->app->default_w;
                            w->h = w->app->default_h;
                            w->x = 60; w->y = 60;
                        }
                        return;
                    }
                    if (which == 0) { wm_toggle_minimize(w); return; }
                }
                /* Otherwise start dragging */
                w->dragging = 1;
                w->drag_off_x = g_mouse.x - w->x;
                w->drag_off_y = g_mouse.y - w->y;
                return;
            }
        }
    }

    /* Forward keyboard / clicks to focused window content */
    window_t *fw = wm_focused();
    if (fw && fw->app && fw->app->on_event) fw->app->on_event(fw);
}
