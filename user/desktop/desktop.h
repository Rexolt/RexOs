/* =============================================================================
 *  RexOS Desktop - Master header
 *  Modern dark-mode desktop environment with window manager + apps.
 * ========================================================================== */
#pragma once
#include <stdint.h>
#include "../libc.h"
#include "../font.h"
#include "theme.h"

/* ----- Forward declarations ------------------------------------------------ */
struct window;
typedef struct window window_t;

/* ----- Global frame buffer state ------------------------------------------- */
extern uint32_t *g_fb;        /* live framebuffer (mapped from kernel) */
extern uint32_t *g_back;      /* offscreen back buffer (CPU side) */
extern uint64_t  g_scr_w;
extern uint64_t  g_scr_h;
extern uint64_t  g_pitch;     /* in bytes */
extern uint64_t  g_tick;      /* current tick (ms-ish, monotonically increasing) */
extern int       g_fps;       /* measured frames per second */

/* ----- Mouse state --------------------------------------------------------- */
typedef struct {
    int x, y;
    int prev_x, prev_y;
    int down;       /* current LMB held */
    int prev_down;  /* previous frame */
    int clicked;    /* press edge this frame */
    int released;   /* release edge this frame */
    int dx, dy;     /* delta since last frame */
} mouse_t;

extern mouse_t g_mouse;

/* ----- Keyboard ring buffer ------------------------------------------------ */
extern char g_kbd_buf[64];
extern int  g_kbd_head;
extern int  g_kbd_tail;

int  kbd_has(void);
char kbd_pop(void);

/* ----- Render primitives (render.c) ---------------------------------------- */
void rd_clear(uint32_t color);
void rd_pixel(int x, int y, uint32_t color);
void rd_rect(int x, int y, int w, int h, uint32_t color);
void rd_rect_outline(int x, int y, int w, int h, uint32_t color);
void rd_rounded(int x, int y, int w, int h, int r, uint32_t color);
void rd_rounded_outline(int x, int y, int w, int h, int r, uint32_t color);
void rd_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bot);
void rd_gradient_h(int x, int y, int w, int h, uint32_t left, uint32_t right);
void rd_shadow(int x, int y, int w, int h, int r, int spread);
void rd_line(int x0, int y0, int x1, int y1, uint32_t color);
void rd_circle_fill(int cx, int cy, int r, uint32_t color);
void rd_blend_rect(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
void rd_blend_rounded(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
uint32_t rd_lerp_color(uint32_t a, uint32_t b, int t /* 0..255 */);

/* Convenience text drawing (uses font subsystem). All draw on g_back. */
void rd_text(int x, int y_baseline, const char *s, int px, uint32_t color);
void rd_text_bold(int x, int y_baseline, const char *s, int px, uint32_t color);
int  rd_text_w(const char *s, int px);

/* Set / clear clip rect (intersected w/ screen). NULL clears. */
void rd_set_clip(int x, int y, int w, int h);
void rd_clear_clip(void);

/* ----- Icons (icons.c) ----------------------------------------------------- */
typedef enum {
    ICON_FILES, ICON_TERMINAL, ICON_CALC, ICON_CLOCK,
    ICON_SYSMON, ICON_NOTES, ICON_ABOUT, ICON_SETTINGS,
    ICON_POWER, ICON_FOLDER, ICON_FILE, ICON_BACK,
    ICON_CLOSE, ICON_MINIMIZE, ICON_MAXIMIZE,
    ICON_BROWSER, ICON_RELOAD, ICON_HOME, ICON_FORWARD,
    ICON_LOCK, ICON_GLOBE,
    ICON_COUNT
} icon_id_t;

void icon_draw(icon_id_t id, int x, int y, int size, uint32_t fg, uint32_t bg);

/* ----- Window manager (wm.c) ---------------------------------------------- */

#define WM_MAX_WINDOWS 16

typedef struct app_def app_def_t;

struct window {
    int           id;
    int           used;
    int           visible;
    int           minimized;
    int           x, y, w, h;
    int           drag_off_x, drag_off_y;
    int           dragging;
    char          title[64];
    icon_id_t     icon;
    const app_def_t *app;
    void         *state;     /* per-app state (allocated) */
    int           focused;
    /* Open/close/minimize animation. anim 0..255 (255 = fully shown).
     * closing=1 means "run anim to 0 then tear down". */
    int           anim;
    int           closing;
};

void     wm_init(void);
window_t *wm_open(const app_def_t *app);  /* opens new instance */
void     wm_close(window_t *w);
void     wm_focus(window_t *w);
void     wm_toggle_minimize(window_t *w);
void     wm_animate(void);       /* advance per-window animations one frame */
void     wm_render_all(void);
void     wm_input_frame(void);   /* dispatches mouse/kbd to topmost focused */
window_t *wm_topmost_at(int x, int y);
int      wm_count_visible(void);
window_t *wm_get(int idx);       /* iterate; NULL when out of range */
window_t *wm_focused(void);

/* Window content area (inside titlebar+border). */
void wm_content_rect(window_t *w, int *cx, int *cy, int *cw, int *ch);

/* ----- App framework ------------------------------------------------------- */

struct app_def {
    const char *name;
    icon_id_t   icon;
    int         default_w, default_h;
    /* Lifecycle: */
    void  (*on_open)(window_t *w);
    void  (*on_close)(window_t *w);
    void  (*on_render)(window_t *w);          /* draw in content rect */
    int   (*on_event)(window_t *w);           /* return 1 if handled */
};

/* App registry (apps.c) */
extern const app_def_t *g_apps[];
extern int g_app_count;

/* Built-in app definitions. */
extern const app_def_t app_files;
extern const app_def_t app_terminal;
extern const app_def_t app_calc;
extern const app_def_t app_clock;
extern const app_def_t app_sysmon;
extern const app_def_t app_about;
extern const app_def_t app_notes;
extern const app_def_t app_browser;

/* ----- Taskbar (taskbar.c) ------------------------------------------------- */
#define TASKBAR_H 44

void tb_init(void);
void tb_render(void);
void tb_input(void);

/* ----- Tiny string helpers (used by apps) ---------------------------------- */
int  d_strlen(const char *s);
void d_strcpy(char *dst, const char *src, int max);
void d_strcat(char *dst, const char *src, int max);
int  d_strcmp(const char *a, const char *b);
int  d_strncmp(const char *a, const char *b, int n);
void d_int_to_str(int v, char *buf);
void d_uint_to_str(uint64_t v, char *buf);
void d_hex_to_str(uint64_t v, char *buf, int width);

/* ----- Hit-testing helpers ------------------------------------------------- */
static inline int pt_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}
