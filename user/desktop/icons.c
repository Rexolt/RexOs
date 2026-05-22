/* =============================================================================
 *  RexOS Desktop - Procedural icons
 *  Each icon is rendered into a (size x size) box at (x,y) with fg color.
 * ========================================================================== */
#include "desktop.h"

/* All icons assume they fit inside a `size x size` box. Scale geometry to size.
 * Use simple rect/line/circle primitives so they look crisp at any UI size.
 * Convention: `size` typically 16-32 px. */

static void icon_files(int x, int y, int s, uint32_t fg) {
    /* Folder shape: rounded rectangle with a small tab on top. */
    int pad = s / 6;
    int tab_w = s / 2;
    int tab_h = s / 8;
    /* Tab */
    rd_rounded(x + pad, y + pad, tab_w, tab_h + 2, 2, fg);
    /* Body */
    rd_rounded(x + pad, y + pad + tab_h, s - 2 * pad, s - 2 * pad - tab_h, 3, fg);
}

static void icon_terminal(int x, int y, int s, uint32_t fg) {
    /* Window with prompt > and cursor */
    rd_rounded_outline(x + 1, y + 2, s - 2, s - 4, 2, fg);
    /* Top bar dots */
    rd_rect(x + 3, y + 4, 2, 1, fg);
    /* Prompt > */
    int px = x + s / 4;
    int py = y + s / 2 + 1;
    rd_line(px, py - 2, px + 3, py, fg);
    rd_line(px + 3, py, px, py + 2, fg);
    /* Cursor */
    rd_rect(px + 6, py - 1, s / 4, 2, fg);
}

static void icon_calc(int x, int y, int s, uint32_t fg) {
    rd_rounded_outline(x + 2, y + 1, s - 4, s - 2, 2, fg);
    /* Display */
    rd_rect(x + 4, y + 3, s - 8, 3, fg);
    /* Buttons grid 3x3 */
    int cell = (s - 10) / 4;
    if (cell < 1) cell = 1;
    int gx = x + 4, gy = y + 8;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        rd_rect(gx + j * (cell + 1), gy + i * (cell + 1), cell, cell, fg);
    }
}

static void icon_clock(int x, int y, int s, uint32_t fg) {
    int cx = x + s / 2, cy = y + s / 2;
    int r = s / 2 - 2;
    /* Outline ring */
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 >= (r - 1) * (r - 1)) rd_pixel(cx + dx, cy + dy, fg);
        }
    }
    /* Hands: hour pointing up, minute pointing right */
    rd_line(cx, cy, cx, cy - r + 3, fg);
    rd_line(cx, cy, cx + r - 4, cy, fg);
    /* Center dot */
    rd_pixel(cx, cy, fg);
}

static void icon_sysmon(int x, int y, int s, uint32_t fg) {
    /* Bar chart style */
    rd_line(x + 2, y + s - 3, x + s - 2, y + s - 3, fg);  /* baseline */
    int bw = (s - 6) / 4;
    if (bw < 1) bw = 1;
    int heights[4] = { s / 3, s / 2, s / 4, s * 2 / 3 };
    for (int i = 0; i < 4; i++) {
        int hh = heights[i];
        rd_rect(x + 2 + i * (bw + 1), y + s - 3 - hh, bw, hh, fg);
    }
}

static void icon_notes(int x, int y, int s, uint32_t fg) {
    /* Page with lines */
    rd_rounded_outline(x + 3, y + 1, s - 6, s - 2, 2, fg);
    for (int i = 0; i < 3; i++) {
        rd_rect(x + 5, y + 5 + i * 4, s - 10, 1, fg);
    }
}

static void icon_about(int x, int y, int s, uint32_t fg) {
    /* "i" inside a circle */
    int cx = x + s / 2, cy = y + s / 2;
    int r = s / 2 - 2;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 >= (r - 1) * (r - 1)) rd_pixel(cx + dx, cy + dy, fg);
        }
    }
    rd_rect(cx - 1, cy - r / 2 - 1, 2, 2, fg);
    rd_rect(cx - 1, cy - r / 4 + 1, 2, r - 2, fg);
}

static void icon_settings(int x, int y, int s, uint32_t fg) {
    /* Gear shape simplified to ring + 8 teeth */
    int cx = x + s / 2, cy = y + s / 2;
    int r = s / 2 - 3;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 >= (r - 1) * (r - 1)) rd_pixel(cx + dx, cy + dy, fg);
        }
    }
    /* 4 cardinal teeth */
    rd_rect(cx - 1, cy - r - 2, 2, 2, fg);
    rd_rect(cx - 1, cy + r,     2, 2, fg);
    rd_rect(cx - r - 2, cy - 1, 2, 2, fg);
    rd_rect(cx + r,     cy - 1, 2, 2, fg);
    /* Center hole */
    int hr = r / 2;
    for (int dy = -hr; dy <= hr; dy++) {
        for (int dx = -hr; dx <= hr; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= hr * hr && d2 >= (hr - 1) * (hr - 1)) rd_pixel(cx + dx, cy + dy, fg);
        }
    }
}

static void icon_power(int x, int y, int s, uint32_t fg) {
    int cx = x + s / 2, cy = y + s / 2;
    int r = s / 2 - 3;
    /* Open ring (top notch) */
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 >= (r - 1) * (r - 1)) {
                /* Skip top notch */
                if (dy < -r / 2 && dx > -2 && dx < 2) continue;
                rd_pixel(cx + dx, cy + dy, fg);
            }
        }
    }
    /* Vertical line at top */
    rd_rect(cx - 1, cy - r - 1, 2, r / 2 + 2, fg);
}

static void icon_folder_simple(int x, int y, int s, uint32_t fg) {
    int pad = s / 8;
    rd_rounded(x + pad, y + pad + 1, s / 3, 2, 1, fg);
    rd_rounded(x + pad, y + pad + 3, s - 2 * pad, s - 2 * pad - 4, 2, fg);
}

static void icon_file(int x, int y, int s, uint32_t fg) {
    /* Page with folded corner */
    int pad = s / 6;
    rd_rect_outline(x + pad, y + pad, s - 2 * pad, s - 2 * pad, fg);
    /* fold */
    int fc = s / 4;
    rd_rect(x + pad + (s - 2 * pad) - fc, y + pad, fc, 1, fg);
    rd_rect(x + pad + (s - 2 * pad) - 1, y + pad, 1, fc, fg);
    rd_line(x + pad + (s - 2 * pad) - fc, y + pad,
            x + pad + (s - 2 * pad) - 1, y + pad + fc, fg);
}

static void icon_back(int x, int y, int s, uint32_t fg) {
    int cx = x + s / 2, cy = y + s / 2;
    int len = s / 3;
    rd_line(cx + len, cy - len, cx - len, cy, fg);
    rd_line(cx - len, cy, cx + len, cy + len, fg);
    rd_line(cx + len, cy - len + 1, cx - len, cy + 1, fg);
    rd_line(cx - len, cy + 1, cx + len, cy + len + 1, fg);
}

static void icon_x(int x, int y, int s, uint32_t fg) {
    int pad = s / 4;
    rd_line(x + pad, y + pad, x + s - pad, y + s - pad, fg);
    rd_line(x + s - pad, y + pad, x + pad, y + s - pad, fg);
    rd_line(x + pad + 1, y + pad, x + s - pad, y + s - pad - 1, fg);
    rd_line(x + s - pad, y + pad + 1, x + pad + 1, y + s - pad, fg);
}

static void icon_minus(int x, int y, int s, uint32_t fg) {
    int pad = s / 4;
    rd_rect(x + pad, y + s / 2, s - 2 * pad, 2, fg);
}

static void icon_square(int x, int y, int s, uint32_t fg) {
    int pad = s / 4;
    rd_rect_outline(x + pad, y + pad, s - 2 * pad, s - 2 * pad, fg);
}

static void icon_globe(int x, int y, int s, uint32_t fg) {
    /* Earth: outer circle + 1 vertical + 2 horizontal arcs. */
    int cx = x + s / 2, cy = y + s / 2;
    int r = s / 2 - 2;
    /* outer ring */
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        int d2 = dx*dx + dy*dy;
        if (d2 <= r*r && d2 >= (r-1)*(r-1)) rd_pixel(cx+dx, cy+dy, fg);
    }
    /* vertical meridian */
    rd_rect(cx, cy - r, 1, 2*r + 1, fg);
    /* horizontal equator */
    rd_rect(cx - r, cy, 2*r + 1, 1, fg);
    /* upper / lower latitude lines (squashed ellipse-ish) */
    int rh = r / 2;
    for (int dx = -r + 1; dx <= r - 1; dx++) {
        /* squashed ellipse y = sqrt(rh^2 (1 - dx^2/r^2)) */
        int yy2 = rh * rh - rh * rh * dx * dx / (r * r);
        if (yy2 < 0) yy2 = 0;
        /* approx sqrt */
        int yy = 0; while (yy * yy < yy2) yy++;
        rd_pixel(cx + dx, cy - yy, fg);
        rd_pixel(cx + dx, cy + yy, fg);
    }
}

static void icon_reload(int x, int y, int s, uint32_t fg) {
    /* Curved arrow circle */
    int cx = x + s / 2, cy = y + s / 2;
    int r = s / 2 - 3;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        int d2 = dx*dx + dy*dy;
        if (d2 <= r*r && d2 >= (r-1)*(r-1)) {
            /* skip a notch in upper-right */
            if (dy < 0 && dx > r/2 - 1) continue;
            rd_pixel(cx+dx, cy+dy, fg);
        }
    }
    /* arrow head pointing right at top-right */
    int ax = cx + r/2, ay = cy - r;
    rd_line(ax, ay, ax + 3, ay + 3, fg);
    rd_line(ax + 3, ay + 3, ax, ay + 6, fg);
    rd_line(ax, ay, ax, ay + 6, fg);
}

static void icon_home(int x, int y, int s, uint32_t fg) {
    int cx = x + s / 2;
    int top_y = y + s / 4;
    int bot_y = y + s - s / 4;
    int hw = s / 3;
    /* roof */
    rd_line(cx - hw - 1, top_y + hw / 2, cx, top_y, fg);
    rd_line(cx, top_y, cx + hw + 1, top_y + hw / 2, fg);
    /* walls */
    rd_rect(cx - hw, top_y + hw / 2, 1, bot_y - top_y - hw / 2, fg);
    rd_rect(cx + hw, top_y + hw / 2, 1, bot_y - top_y - hw / 2, fg);
    /* floor */
    rd_rect(cx - hw, bot_y, 2 * hw + 1, 1, fg);
    /* door */
    rd_rect(cx - 2, bot_y - hw / 2, 4, hw / 2, fg);
}

static void icon_forward(int x, int y, int s, uint32_t fg) {
    int cx = x + s / 2, cy = y + s / 2;
    int len = s / 3;
    rd_line(cx - len, cy - len, cx + len, cy, fg);
    rd_line(cx + len, cy, cx - len, cy + len, fg);
    rd_line(cx - len, cy - len + 1, cx + len, cy + 1, fg);
    rd_line(cx + len, cy + 1, cx - len, cy + len + 1, fg);
}

static void icon_lock(int x, int y, int s, uint32_t fg) {
    int bx = x + s / 4 + 1;
    int bw = s / 2 - 2;
    int by = y + s / 2;
    int bh = s / 2 - 3;
    rd_rounded(bx, by, bw, bh, 2, fg);
    /* shackle */
    int sx = bx + bw / 2;
    int sr = bw / 2 - 1;
    for (int dy = -sr - 2; dy <= 0; dy++) for (int dx = -sr; dx <= sr; dx++) {
        int d2 = dx*dx + dy*dy;
        if (d2 <= sr*sr && d2 >= (sr-1)*(sr-1)) rd_pixel(sx+dx, by+dy, fg);
    }
}

void icon_draw(icon_id_t id, int x, int y, int size, uint32_t fg, uint32_t bg) {
    (void)bg;
    switch (id) {
        case ICON_FILES:    icon_files(x, y, size, fg); break;
        case ICON_TERMINAL: icon_terminal(x, y, size, fg); break;
        case ICON_CALC:     icon_calc(x, y, size, fg); break;
        case ICON_CLOCK:    icon_clock(x, y, size, fg); break;
        case ICON_SYSMON:   icon_sysmon(x, y, size, fg); break;
        case ICON_NOTES:    icon_notes(x, y, size, fg); break;
        case ICON_ABOUT:    icon_about(x, y, size, fg); break;
        case ICON_SETTINGS: icon_settings(x, y, size, fg); break;
        case ICON_POWER:    icon_power(x, y, size, fg); break;
        case ICON_FOLDER:   icon_folder_simple(x, y, size, fg); break;
        case ICON_FILE:     icon_file(x, y, size, fg); break;
        case ICON_BACK:     icon_back(x, y, size, fg); break;
        case ICON_CLOSE:    icon_x(x, y, size, fg); break;
        case ICON_MINIMIZE: icon_minus(x, y, size, fg); break;
        case ICON_MAXIMIZE: icon_square(x, y, size, fg); break;
        case ICON_BROWSER:  icon_globe(x, y, size, fg); break;
        case ICON_GLOBE:    icon_globe(x, y, size, fg); break;
        case ICON_RELOAD:   icon_reload(x, y, size, fg); break;
        case ICON_HOME:     icon_home(x, y, size, fg); break;
        case ICON_FORWARD:  icon_forward(x, y, size, fg); break;
        case ICON_LOCK:     icon_lock(x, y, size, fg); break;
        default: break;
    }
}
