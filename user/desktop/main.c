/* =============================================================================
 *  RexOS Desktop - Entry point and main event loop
 *
 *  Pipeline per frame:
 *    1) Pump input  (mouse, keyboard ring buffer)
 *    2) Dispatch    (taskbar -> wm -> focused window)
 *    3) Render      (wallpaper -> windows -> taskbar -> cursor)
 *    4) Blit        (back buffer -> framebuffer)
 *    5) Yield
 * ========================================================================== */
#include "desktop.h"

mouse_t g_mouse;
char g_kbd_buf[64];
int  g_kbd_head = 0;
int  g_kbd_tail = 0;
int  g_request_exit = 0;

int kbd_has(void) { return g_kbd_head != g_kbd_tail; }
char kbd_pop(void) {
    if (!kbd_has()) return 0;
    char c = g_kbd_buf[g_kbd_tail];
    g_kbd_tail = (g_kbd_tail + 1) % 64;
    return c;
}
static void kbd_push(char c) {
    int next = (g_kbd_head + 1) % 64;
    if (next == g_kbd_tail) return;  /* full */
    g_kbd_buf[g_kbd_head] = c;
    g_kbd_head = next;
}

/* ----- Wallpaper ----------------------------------------------------------- */
static void render_wallpaper(void) {
    /* Vertical gradient with accent glow at bottom-right. */
    rd_gradient_v(0, 0, (int)g_scr_w, (int)g_scr_h, COL_BG_TOP, COL_BG_BOT);
    /* Subtle radial accent in lower-right (cheap concentric blends) */
    int gx = (int)g_scr_w - 220;
    int gy = (int)g_scr_h - 280;
    for (int i = 0; i < 6; i++) {
        int radius = 60 + i * 30;
        uint8_t alpha = (uint8_t)(20 - i * 3);
        if ((int)alpha < 4) alpha = 4;
        /* Approximate a circular blend with a square aligned to center */
        rd_blend_rect(gx - radius, gy - radius, radius * 2, radius * 2,
                      COL_ACCENT_GLOW, alpha);
    }
    /* Top diagonal subtle stripe */
    rd_blend_rect(0, 0, (int)g_scr_w, 40, 0xFFFFFF, 6);
}

/* ----- Cursor (drawn last, on top of everything) --------------------------- */
static void render_cursor(int mx, int my) {
    /* Classic arrow cursor outlined for visibility on any background. */
    static const uint8_t mask[16][12] = {
        {1,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0},
        {1,2,2,2,2,2,2,2,1,0,0,0},
        {1,2,2,2,2,2,1,1,1,1,0,0},
        {1,2,2,1,2,2,1,0,0,0,0,0},
        {1,2,1,0,1,2,2,1,0,0,0,0},
        {1,1,0,0,1,2,2,1,0,0,0,0},
        {0,0,0,0,0,1,2,2,1,0,0,0},
        {0,0,0,0,0,1,2,2,1,0,0,0},
        {0,0,0,0,0,0,1,1,0,0,0,0},
    };
    for (int yy = 0; yy < 16; yy++) {
        for (int xx = 0; xx < 12; xx++) {
            uint8_t v = mask[yy][xx];
            if (v == 1) rd_pixel(mx + xx, my + yy, COL_CURSOR_BORDER);
            else if (v == 2) rd_pixel(mx + xx, my + yy, COL_CURSOR_FILL);
        }
    }
}

/* ----- Blit ---------------------------------------------------------------- */
static void blit_to_fb(void) {
    if (!g_back || !g_fb) return;
    /* Full-screen copy. pitch is in bytes; we write 32-bit pixels per row. */
    int pitch_px = (int)(g_pitch / 4);
    int sw = (int)g_scr_w;
    int sh = (int)g_scr_h;
    for (int y = 0; y < sh; y++) {
        uint32_t *src = g_back + y * sw;
        uint32_t *dst = g_fb + y * pitch_px;
        memcpy(dst, src, (uint64_t)sw * 4);
    }
}

/* ----- Pump mouse + kbd ---------------------------------------------------- */
static void pump_input(void) {
    uint32_t mx, my, mb;
    get_mouse(&mx, &my, &mb);
    g_mouse.prev_x = g_mouse.x;
    g_mouse.prev_y = g_mouse.y;
    g_mouse.prev_down = g_mouse.down;
    g_mouse.x = (int)mx;
    g_mouse.y = (int)my;
    g_mouse.down = (mb & 1) ? 1 : 0;
    g_mouse.clicked  = (g_mouse.down && !g_mouse.prev_down);
    g_mouse.released = (!g_mouse.down && g_mouse.prev_down);
    g_mouse.dx = g_mouse.x - g_mouse.prev_x;
    g_mouse.dy = g_mouse.y - g_mouse.prev_y;

    /* Drain keyboard FIFO */
    for (int i = 0; i < 32; i++) {
        char c = kbd_poll();
        if (c == 0) break;
        kbd_push(c);
    }
}

/* ----- Frame -------------------------------------------------------------- */
static void frame(void) {
    pump_input();

    /* Dispatch order: taskbar first (it can swallow), then WM. */
    tb_input();
    wm_input_frame();

    g_tick++;

    /* FPS counter: count frames between kernel ticks (PIT @ ~100 Hz). */
    static uint64_t fps_last_kernel_tick = 0;
    static int      fps_frames = 0;
    fps_frames++;
    uint64_t kt = get_ticks();
    if (fps_last_kernel_tick == 0) fps_last_kernel_tick = kt;
    uint64_t dt = kt - fps_last_kernel_tick;
    if (dt >= 50) {  /* 50 PIT ticks ≈ 0.5 s */
        g_fps = (int)((uint64_t)fps_frames * 100 / dt);
        fps_last_kernel_tick = kt;
        fps_frames = 0;
    }

    /* Advance animations, then render */
    wm_animate();

    render_wallpaper();
    wm_render_all();
    tb_render();
    render_cursor(g_mouse.x, g_mouse.y);

    blit_to_fb();
}

/* ----- Entry -------------------------------------------------------------- */
void _start(void) {
    /* Acquire framebuffer + map. */
    g_fb = (uint32_t *)get_fb(&g_scr_w, &g_scr_h, &g_pitch);
    if (!g_fb) { print("[desktop] no framebuffer\n"); exit(1); }

    /* Allocate back buffer. */
    uint64_t bb_size = g_scr_w * g_scr_h * 4;
    g_back = (uint32_t *)malloc(bb_size);
    if (!g_back) { print("[desktop] no memory for back buffer\n"); exit(1); }

    /* clear */
    for (uint64_t i = 0; i < g_scr_w * g_scr_h; i++) g_back[i] = COL_BG_BOT;

    /* Init font (TTF). */
    font_init();

    /* Init WM + taskbar. */
    wm_init();
    tb_init();

    /* Open About on first launch as a welcome. */
    wm_open(&app_about);

    print("[desktop] ready\n");

    /* Main loop. */
    while (!g_request_exit) {
        frame();
        yield();
    }

    print("[desktop] exiting\n");
    exit(0);
}
