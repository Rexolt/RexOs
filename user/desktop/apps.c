/* =============================================================================
 *  RexOS Desktop - Built-in applications
 *
 *    - Files          File browser
 *    - Terminal       Command shell
 *    - Calculator     4-function calculator
 *    - Clock          Live clock + date
 *    - System Monitor Hardware introspection
 *    - Notes          In-memory text editor
 *    - About          System information
 * ========================================================================== */
#include "desktop.h"

/* ----- Tiny string helpers (exported via desktop.h) ------------------------ */
int d_strlen(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}
void d_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    if (max <= 0) return;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
void d_strcat(char *dst, const char *src, int max) {
    int dl = d_strlen(dst);
    int i = 0;
    if (max <= 0) return;
    while (src && src[i] && dl + i < max - 1) { dst[dl + i] = src[i]; i++; }
    dst[dl + i] = 0;
}
int d_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
int d_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
void d_int_to_str(int v, char *buf) {
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    char tmp[16];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    int i = 0;
    if (neg) buf[i++] = '-';
    while (n > 0) buf[i++] = tmp[--n];
    buf[i] = 0;
}
void d_uint_to_str(uint64_t v, char *buf) {
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    int i = 0;
    while (n > 0) buf[i++] = tmp[--n];
    buf[i] = 0;
}
void d_hex_to_str(uint64_t v, char *buf, int width) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[20];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = hex[v & 0xF]; v >>= 4; }
    int i = 0;
    while (n < width) { tmp[n++] = '0'; }
    while (n > 0) buf[i++] = tmp[--n];
    buf[i] = 0;
}

/* ============================================================================
 * Files app
 * ========================================================================== */
typedef struct {
    char  cwd[256];
    char  entries[64][96];
    int   is_dir[64];
    int   count;
    int   selected;
    int   scroll;
    int   need_refresh;
} files_state_t;

static void files_refresh(files_state_t *st) {
    st->count = 0;
    int fd = open(st->cwd);
    if (fd < 0) {
        d_strcpy(st->entries[0], "(cannot open)", 96);
        st->is_dir[0] = 0;
        st->count = 1;
        st->need_refresh = 0;
        return;
    }
    /* ".." entry */
    if (d_strcmp(st->cwd, "/") != 0) {
        d_strcpy(st->entries[st->count], "..", 96);
        st->is_dir[st->count] = 1;
        st->count++;
    }
    dirent_t e;
    while (st->count < 64 && getdents(fd, &e) == 0) {
        d_strcpy(st->entries[st->count], e.name, 96);
        /* heuristic: dirs typically have no '.' */
        int has_dot = 0;
        for (int i = 0; e.name[i]; i++) if (e.name[i] == '.') { has_dot = 1; break; }
        st->is_dir[st->count] = !has_dot;
        if (d_strcmp(e.name, "mnt") == 0 || d_strcmp(e.name, "initrd") == 0) st->is_dir[st->count] = 1;
        st->count++;
    }
    close(fd);
    st->need_refresh = 0;
}

static void files_open(window_t *w) {
    files_state_t *st = (files_state_t *)malloc(sizeof(files_state_t));
    memset(st, 0, sizeof(*st));
    d_strcpy(st->cwd, "/", sizeof(st->cwd));
    files_refresh(st);
    w->state = st;
}

static void files_render(window_t *w) {
    files_state_t *st = (files_state_t *)w->state;
    if (!st) return;
    if (st->need_refresh) files_refresh(st);

    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);

    /* Path bar */
    rd_rect(cx, cy, cw, 36, COL_SURFACE_2);
    rd_rect(cx, cy + 35, cw, 1, COL_WIN_BORDER);
    /* Back button */
    int bx = cx + 8, by = cy + 6, bs = 24;
    int back_hov = pt_in_rect(g_mouse.x, g_mouse.y, bx, by, bs, bs);
    if (back_hov) rd_rounded(bx, by, bs, bs, 4, COL_HOVER);
    icon_draw(ICON_BACK, bx, by, bs, COL_TEXT, 0);
    /* Path text */
    rd_text(bx + bs + 12, cy + 23, st->cwd, UI_PX, COL_TEXT);

    /* List */
    int ly = cy + 44;
    int row_h = 32;
    int max_rows = (ch - 44) / row_h;
    int start = st->scroll;
    int end = start + max_rows;
    if (end > st->count) end = st->count;

    for (int i = start; i < end; i++) {
        int yy = ly + (i - start) * row_h;
        int hov = pt_in_rect(g_mouse.x, g_mouse.y, cx + 4, yy, cw - 8, row_h);
        int sel = (i == st->selected);
        if (sel) rd_rounded(cx + 4, yy, cw - 8, row_h, 4, COL_ACCENT_DARK);
        else if (hov) rd_rounded(cx + 4, yy, cw - 8, row_h, 4, COL_HOVER);
        icon_draw(st->is_dir[i] ? ICON_FOLDER : ICON_FILE,
                  cx + 12, yy + 7, 18,
                  sel ? COL_TEXT : (st->is_dir[i] ? COL_ACCENT : COL_TEXT_MUTED), 0);
        rd_text(cx + 38, yy + 21, st->entries[i], UI_PX, sel ? COL_TEXT : COL_TEXT);
    }

    /* Status bar */
    char buf[64];
    d_strcpy(buf, "", sizeof(buf));
    char nb[16]; d_int_to_str(st->count, nb);
    d_strcat(buf, nb, sizeof(buf));
    d_strcat(buf, " items", sizeof(buf));
    rd_text(cx + 8, cy + ch - 8, buf, UI_PX_SMALL, COL_TEXT_MUTED);
}

static int files_event(window_t *w) {
    files_state_t *st = (files_state_t *)w->state;
    if (!st) return 0;
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);

    if (g_mouse.clicked) {
        /* Back button */
        int bx = cx + 8, by = cy + 6, bs = 24;
        if (pt_in_rect(g_mouse.x, g_mouse.y, bx, by, bs, bs)) {
            /* go to parent */
            int n = d_strlen(st->cwd);
            if (n > 1) {
                while (n > 1 && st->cwd[n - 1] != '/') n--;
                if (n > 1) st->cwd[n - 1] = 0;
                else st->cwd[1] = 0;
                st->scroll = 0; st->selected = -1;
                files_refresh(st);
            }
            return 1;
        }
        /* Row hit */
        int ly = cy + 44, row_h = 32;
        int max_rows = (ch - 44) / row_h;
        int end = st->scroll + max_rows;
        if (end > st->count) end = st->count;
        for (int i = st->scroll; i < end; i++) {
            int yy = ly + (i - st->scroll) * row_h;
            if (pt_in_rect(g_mouse.x, g_mouse.y, cx + 4, yy, cw - 8, row_h)) {
                if (st->selected == i) {
                    /* "open": navigate if directory */
                    if (st->is_dir[i]) {
                        if (d_strcmp(st->entries[i], "..") == 0) {
                            int n = d_strlen(st->cwd);
                            if (n > 1) {
                                while (n > 1 && st->cwd[n - 1] != '/') n--;
                                if (n > 1) st->cwd[n - 1] = 0;
                                else st->cwd[1] = 0;
                            }
                        } else {
                            int n = d_strlen(st->cwd);
                            if (n > 0 && st->cwd[n - 1] != '/') {
                                d_strcat(st->cwd, "/", sizeof(st->cwd));
                            }
                            d_strcat(st->cwd, st->entries[i], sizeof(st->cwd));
                        }
                        st->scroll = 0; st->selected = -1;
                        files_refresh(st);
                    }
                } else {
                    st->selected = i;
                }
                return 1;
            }
        }
    }
    return 0;
}

const app_def_t app_files = {
    .name = "Files",
    .icon = ICON_FILES,
    .default_w = 560, .default_h = 420,
    .on_open = files_open,
    .on_render = files_render,
    .on_event = files_event,
};

/* ============================================================================
 * Terminal app
 * ========================================================================== */
#define TERM_COLS 80
#define TERM_ROWS 22
#define TERM_INPUT_MAX 128

typedef struct {
    char rows[TERM_ROWS][TERM_COLS + 1];
    int  row_count;
    char input[TERM_INPUT_MAX];
    int  input_len;
} term_state_t;

static void term_print_line(term_state_t *st, const char *s) {
    if (st->row_count >= TERM_ROWS) {
        for (int i = 0; i < TERM_ROWS - 1; i++) {
            d_strcpy(st->rows[i], st->rows[i + 1], TERM_COLS + 1);
        }
        st->row_count = TERM_ROWS - 1;
    }
    d_strcpy(st->rows[st->row_count++], s, TERM_COLS + 1);
}

static void term_print_kv(term_state_t *st, const char *k, const char *v) {
    char line[TERM_COLS + 1];
    d_strcpy(line, k, sizeof(line));
    d_strcat(line, v, sizeof(line));
    term_print_line(st, line);
}

static void term_run_cmd(term_state_t *st, const char *cmd) {
    char promptline[TERM_COLS + 1];
    d_strcpy(promptline, "> ", sizeof(promptline));
    d_strcat(promptline, cmd, sizeof(promptline));
    term_print_line(st, promptline);

    if (cmd[0] == 0) return;
    if (d_strcmp(cmd, "help") == 0) {
        term_print_line(st, "Commands: help, clear, ls <path>, cat <path>, ");
        term_print_line(st, "          uptime, mem, ver, echo <text>, time, date");
    } else if (d_strcmp(cmd, "clear") == 0) {
        st->row_count = 0;
    } else if (d_strncmp(cmd, "echo ", 5) == 0) {
        term_print_line(st, cmd + 5);
    } else if (d_strcmp(cmd, "ver") == 0) {
        term_print_line(st, "RexOS v0.1.0-alpha");
    } else if (d_strcmp(cmd, "uptime") == 0) {
        char buf[64]; char nb[24];
        uint64_t t = get_ticks();
        d_strcpy(buf, "uptime: ", sizeof(buf));
        d_uint_to_str(t, nb);
        d_strcat(buf, nb, sizeof(buf));
        d_strcat(buf, " ticks", sizeof(buf));
        term_print_line(st, buf);
    } else if (d_strcmp(cmd, "mem") == 0) {
        term_print_line(st, "(memory stats not available via syscall)");
    } else if (d_strcmp(cmd, "time") == 0) {
        rtc_time_t tt; get_time(&tt);
        char buf[32];
        buf[0] = '0' + (tt.hour / 10) % 10;
        buf[1] = '0' + tt.hour % 10;
        buf[2] = ':';
        buf[3] = '0' + (tt.minute / 10) % 10;
        buf[4] = '0' + tt.minute % 10;
        buf[5] = ':';
        buf[6] = '0' + (tt.second / 10) % 10;
        buf[7] = '0' + tt.second % 10;
        buf[8] = 0;
        term_print_kv(st, "time: ", buf);
    } else if (d_strcmp(cmd, "date") == 0) {
        rtc_time_t tt; get_time(&tt);
        char buf[16];
        char yb[8]; d_int_to_str((int)tt.year, yb);
        d_strcpy(buf, yb, sizeof(buf));
        d_strcat(buf, "-", sizeof(buf));
        char mb[4];
        mb[0] = '0' + (tt.month / 10) % 10;
        mb[1] = '0' + tt.month % 10; mb[2] = 0;
        d_strcat(buf, mb, sizeof(buf));
        d_strcat(buf, "-", sizeof(buf));
        char db[4];
        db[0] = '0' + (tt.day / 10) % 10;
        db[1] = '0' + tt.day % 10; db[2] = 0;
        d_strcat(buf, db, sizeof(buf));
        term_print_kv(st, "date: ", buf);
    } else if (d_strncmp(cmd, "ls", 2) == 0) {
        const char *p = cmd + 2;
        while (*p == ' ') p++;
        const char *path = (*p) ? p : "/";
        int fd = open(path);
        if (fd < 0) {
            term_print_line(st, "ls: cannot open path");
        } else {
            dirent_t e;
            while (getdents(fd, &e) == 0) {
                term_print_line(st, e.name);
            }
            close(fd);
        }
    } else if (d_strncmp(cmd, "cat ", 4) == 0) {
        const char *path = cmd + 4;
        int fd = open(path);
        if (fd < 0) { term_print_line(st, "cat: cannot open"); return; }
        char buf[TERM_COLS];
        int n;
        while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = 0;
            /* Print as one or more lines */
            char line[TERM_COLS + 1];
            int li = 0;
            for (int i = 0; i < n; i++) {
                if (buf[i] == '\n' || li >= TERM_COLS) {
                    line[li] = 0;
                    term_print_line(st, line);
                    li = 0;
                } else if (buf[i] >= 32 || buf[i] == '\t') {
                    line[li++] = buf[i] == '\t' ? ' ' : buf[i];
                }
            }
            if (li > 0) { line[li] = 0; term_print_line(st, line); }
        }
        close(fd);
    } else {
        char buf[TERM_COLS + 1];
        d_strcpy(buf, "unknown command: ", sizeof(buf));
        d_strcat(buf, cmd, sizeof(buf));
        term_print_line(st, buf);
        term_print_line(st, "type 'help' for commands");
    }
}

static void term_open(window_t *w) {
    term_state_t *st = (term_state_t *)malloc(sizeof(term_state_t));
    memset(st, 0, sizeof(*st));
    term_print_line(st, "RexOS Terminal v0.1");
    term_print_line(st, "type 'help' for commands");
    term_print_line(st, "");
    w->state = st;
}

static void term_render(window_t *w) {
    term_state_t *st = (term_state_t *)w->state;
    if (!st) return;
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    rd_rect(cx, cy, cw, ch, 0x0A0E14);

    int line_h = 16;
    int margin = 8;
    int max_rows = (ch - 2 * margin - line_h) / line_h;
    int start = st->row_count - max_rows;
    if (start < 0) start = 0;
    int yy = cy + margin + 12;
    for (int i = start; i < st->row_count; i++) {
        rd_text(cx + margin, yy, st->rows[i], UI_PX_SMALL,
                st->rows[i][0] == '>' ? COL_GREEN : COL_TEXT);
        yy += line_h;
    }
    /* Input line at bottom */
    int iy = cy + ch - margin - 4;
    rd_text(cx + margin, iy, "> ", UI_PX_SMALL, COL_ACCENT);
    rd_text(cx + margin + rd_text_w("> ", UI_PX_SMALL), iy, st->input,
            UI_PX_SMALL, COL_TEXT);
    /* Cursor blink */
    if ((g_tick / 30) % 2 == 0) {
        int cur_x = cx + margin + rd_text_w("> ", UI_PX_SMALL)
                    + rd_text_w(st->input, UI_PX_SMALL);
        rd_rect(cur_x, iy - 10, 7, 12, COL_TEXT);
    }
}

static int term_event(window_t *w) {
    term_state_t *st = (term_state_t *)w->state;
    if (!st) return 0;
    while (kbd_has()) {
        char c = kbd_pop();
        if (c == '\n' || c == '\r') {
            char cmd[TERM_INPUT_MAX];
            d_strcpy(cmd, st->input, sizeof(cmd));
            st->input[0] = 0;
            st->input_len = 0;
            term_run_cmd(st, cmd);
        } else if (c == 8 || c == 127) {
            if (st->input_len > 0) {
                st->input[--st->input_len] = 0;
            }
        } else if (c >= 32 && c < 127 && st->input_len < TERM_INPUT_MAX - 1) {
            st->input[st->input_len++] = c;
            st->input[st->input_len] = 0;
        }
    }
    return 1;
}

const app_def_t app_terminal = {
    .name = "Terminal",
    .icon = ICON_TERMINAL,
    .default_w = 620, .default_h = 380,
    .on_open = term_open,
    .on_render = term_render,
    .on_event = term_event,
};

/* ============================================================================
 * Calculator
 * ========================================================================== */
typedef struct {
    char  display[24];
    int64_t accum;
    int64_t pending;
    int   pending_op; /* 0=none, 1=+, 2=-, 3=*, 4=/ */
    int   reset_on_digit;
} calc_state_t;

static void calc_open(window_t *w) {
    calc_state_t *st = (calc_state_t *)malloc(sizeof(calc_state_t));
    memset(st, 0, sizeof(*st));
    d_strcpy(st->display, "0", sizeof(st->display));
    w->state = st;
}

static void calc_set_display(calc_state_t *st, int64_t v) {
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    char tmp[24]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    int i = 0;
    if (neg) st->display[i++] = '-';
    while (n > 0) st->display[i++] = tmp[--n];
    st->display[i] = 0;
}

static int64_t calc_apply(int op, int64_t a, int64_t b) {
    switch (op) {
        case 1: return a + b;
        case 2: return a - b;
        case 3: return a * b;
        case 4: return b == 0 ? 0 : a / b;
        default: return b;
    }
}

static void calc_input(calc_state_t *st, char c) {
    if (c >= '0' && c <= '9') {
        if (st->reset_on_digit || d_strcmp(st->display, "0") == 0) {
            st->display[0] = c;
            st->display[1] = 0;
            st->reset_on_digit = 0;
        } else {
            int n = d_strlen(st->display);
            if (n < 12) { st->display[n] = c; st->display[n + 1] = 0; }
        }
        return;
    }
    /* Convert display to int */
    int64_t val = 0; int neg = 0; int j = 0;
    if (st->display[0] == '-') { neg = 1; j = 1; }
    for (; st->display[j]; j++) val = val * 10 + (st->display[j] - '0');
    if (neg) val = -val;

    if (c == '=') {
        if (st->pending_op) {
            int64_t r = calc_apply(st->pending_op, st->pending, val);
            calc_set_display(st, r);
            st->pending_op = 0;
            st->reset_on_digit = 1;
        }
    } else if (c == 'C') {
        d_strcpy(st->display, "0", sizeof(st->display));
        st->pending = 0; st->pending_op = 0; st->reset_on_digit = 0;
    } else if (c == '+' || c == '-' || c == '*' || c == '/') {
        if (st->pending_op) {
            int64_t r = calc_apply(st->pending_op, st->pending, val);
            calc_set_display(st, r);
            st->pending = r;
        } else {
            st->pending = val;
        }
        st->pending_op = (c == '+' ? 1 : c == '-' ? 2 : c == '*' ? 3 : 4);
        st->reset_on_digit = 1;
    } else if (c == 'N') {
        calc_set_display(st, -val);
    }
}

static const char *calc_btn_label(int r, int c) {
    static const char *labels[5][4] = {
        {"C", "+/-", "%", "/"},
        {"7", "8",   "9", "*"},
        {"4", "5",   "6", "-"},
        {"1", "2",   "3", "+"},
        {"0", "00",  ".", "="},
    };
    return labels[r][c];
}

static void calc_render(window_t *w) {
    calc_state_t *st = (calc_state_t *)w->state;
    if (!st) return;
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    /* Display */
    int dh = 56;
    rd_rect(cx, cy, cw, dh, 0x0A0E14);
    int tx = cx + cw - rd_text_w(st->display, UI_PX_HUGE) - 16;
    rd_text(tx, cy + 38, st->display, UI_PX_HUGE, COL_TEXT);
    /* Buttons grid 5x4 */
    int gy = cy + dh + 8;
    int gh = ch - dh - 16;
    int btn_w = (cw - 5 * 8) / 4;
    int btn_h = (gh - 4 * 6) / 5;
    if (btn_w < 16) btn_w = 16;
    if (btn_h < 16) btn_h = 16;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            int bx = cx + 8 + c * (btn_w + 8);
            int by = gy + r * (btn_h + 6);
            const char *lab = calc_btn_label(r, c);
            int hov = pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_w, btn_h);
            uint32_t col = COL_SURFACE_2;
            uint32_t txt = COL_TEXT;
            if (c == 3) { col = COL_ACCENT_DARK; txt = COL_TEXT; }
            if (r == 0 && c == 0) { col = COL_RED; }
            if (hov) col = rd_lerp_color(col, COL_TEXT, 40);
            rd_rounded(bx, by, btn_w, btn_h, 6, col);
            int lw = rd_text_w(lab, UI_PX_LARGE);
            rd_text(bx + (btn_w - lw) / 2, by + btn_h / 2 + 7, lab, UI_PX_LARGE, txt);
        }
    }
}

static int calc_event(window_t *w) {
    calc_state_t *st = (calc_state_t *)w->state;
    if (!st) return 0;
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    int dh = 56;
    int gy = cy + dh + 8;
    int gh = ch - dh - 16;
    int btn_w = (cw - 5 * 8) / 4;
    int btn_h = (gh - 4 * 6) / 5;
    if (g_mouse.clicked) {
        for (int r = 0; r < 5; r++) for (int c = 0; c < 4; c++) {
            int bx = cx + 8 + c * (btn_w + 8);
            int by = gy + r * (btn_h + 6);
            if (pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_w, btn_h)) {
                const char *lab = calc_btn_label(r, c);
                if (lab[0] == '+' && !lab[1]) calc_input(st, '+');
                else if (lab[0] == '-' && !lab[1]) calc_input(st, '-');
                else if (lab[0] == '*' && !lab[1]) calc_input(st, '*');
                else if (lab[0] == '/' && !lab[1]) calc_input(st, '/');
                else if (lab[0] == '=' && !lab[1]) calc_input(st, '=');
                else if (lab[0] == 'C' && !lab[1]) calc_input(st, 'C');
                else if (lab[0] == '+' && lab[1] == '/') calc_input(st, 'N');
                else if (lab[0] >= '0' && lab[0] <= '9' && !lab[1]) calc_input(st, lab[0]);
                else if (lab[0] == '0' && lab[1] == '0') { calc_input(st, '0'); calc_input(st, '0'); }
                return 1;
            }
        }
    }
    while (kbd_has()) {
        char c = kbd_pop();
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == '\n') {
            calc_input(st, c == '\n' ? '=' : c);
        } else if (c == 'c' || c == 'C') calc_input(st, 'C');
    }
    return 1;
}

const app_def_t app_calc = {
    .name = "Calculator",
    .icon = ICON_CALC,
    .default_w = 280, .default_h = 380,
    .on_open = calc_open,
    .on_render = calc_render,
    .on_event = calc_event,
};

/* ============================================================================
 * Clock app
 * ========================================================================== */
static void clock_render(window_t *w) {
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    rd_rect(cx, cy, cw, ch, COL_SURFACE_1);

    rtc_time_t t; get_time(&t);
    char tbuf[16];
    tbuf[0] = '0' + (t.hour / 10) % 10;
    tbuf[1] = '0' + t.hour % 10;
    tbuf[2] = ':';
    tbuf[3] = '0' + (t.minute / 10) % 10;
    tbuf[4] = '0' + t.minute % 10;
    tbuf[5] = ':';
    tbuf[6] = '0' + (t.second / 10) % 10;
    tbuf[7] = '0' + t.second % 10;
    tbuf[8] = 0;

    /* Big time */
    int big = 64;
    int tw = rd_text_w(tbuf, big);
    rd_text_bold(cx + (cw - tw) / 2, cy + ch / 2 + 12, tbuf, big, COL_ACCENT);

    /* Date below */
    char dbuf[32];
    char yb[8]; d_int_to_str((int)t.year, yb);
    d_strcpy(dbuf, yb, sizeof(dbuf));
    d_strcat(dbuf, " - ", sizeof(dbuf));
    static const char *months[] = {
        "?", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    int mi = (t.month >= 1 && t.month <= 12) ? t.month : 0;
    d_strcat(dbuf, months[mi], sizeof(dbuf));
    d_strcat(dbuf, " ", sizeof(dbuf));
    char db[4];
    d_int_to_str((int)t.day, db);
    d_strcat(dbuf, db, sizeof(dbuf));
    int dw = rd_text_w(dbuf, UI_PX_LARGE);
    rd_text(cx + (cw - dw) / 2, cy + ch / 2 + 56, dbuf, UI_PX_LARGE, COL_TEXT_MUTED);
}

const app_def_t app_clock = {
    .name = "Clock",
    .icon = ICON_CLOCK,
    .default_w = 460, .default_h = 280,
    .on_render = clock_render,
};

/* ============================================================================
 * About app
 * ========================================================================== */
static void about_render(window_t *w) {
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    rd_rect(cx, cy, cw, ch, COL_SURFACE_1);
    /* Big logo / accent */
    int hero_h = 120;
    rd_gradient_v(cx, cy, cw, hero_h, 0x1E3A5F, COL_SURFACE_1);
    rd_text_bold(cx + 24, cy + 60, "RexOS", 48, COL_ACCENT);
    rd_text(cx + 24, cy + 92, "Modern hobby kernel + desktop", UI_PX, COL_TEXT_MUTED);

    int y = cy + hero_h + 24;
    rd_text_bold(cx + 24, y, "System Information", UI_PX_LARGE, COL_TEXT);
    y += 32;

    char buf[96];
    char nb[24];

    rd_text(cx + 24, y, "Version", UI_PX, COL_TEXT_MUTED);
    rd_text(cx + 180, y, "v0.1.0-alpha", UI_PX, COL_TEXT);
    y += 24;

    rd_text(cx + 24, y, "Architecture", UI_PX, COL_TEXT_MUTED);
    rd_text(cx + 180, y, "x86_64", UI_PX, COL_TEXT);
    y += 24;

    rd_text(cx + 24, y, "Display", UI_PX, COL_TEXT_MUTED);
    d_uint_to_str(g_scr_w, nb);
    d_strcpy(buf, nb, sizeof(buf));
    d_strcat(buf, " x ", sizeof(buf));
    d_uint_to_str(g_scr_h, nb);
    d_strcat(buf, nb, sizeof(buf));
    d_strcat(buf, " px", sizeof(buf));
    rd_text(cx + 180, y, buf, UI_PX, COL_TEXT);
    y += 24;

    rd_text(cx + 24, y, "Uptime", UI_PX, COL_TEXT_MUTED);
    d_uint_to_str(get_ticks(), nb);
    d_strcpy(buf, nb, sizeof(buf));
    d_strcat(buf, " ticks", sizeof(buf));
    rd_text(cx + 180, y, buf, UI_PX, COL_TEXT);
    y += 32;

    rd_text(cx + 24, y, "(c) RexOS contributors. Built with care.", UI_PX_SMALL, COL_TEXT_DIM);
}

const app_def_t app_about = {
    .name = "About RexOS",
    .icon = ICON_ABOUT,
    .default_w = 520, .default_h = 420,
    .on_render = about_render,
};

/* ============================================================================
 * NetSurf launcher
 *
 * A NetSurf teljes képernyős, közvetlenül a framebufferre rajzoló app.
 * Indításkor spawn + waitpid: amíg fut, a desktop main loopja itt blokkol,
 * így nem rajzol bele a NetSurf képébe. Kilépés után a desktop következő
 * frame-je visszarajzolja az asztalt.
 * ========================================================================== */
static int g_netsurf_ran = 0;

static void netsurf_open(window_t *w) {
    (void)w;
    int pid = spawn("netsurf.elf");
    if (pid > 0) {
        waitpid(pid);
        g_netsurf_ran = 1;
    } else {
        g_netsurf_ran = -1;
    }
}

static void netsurf_render(window_t *w) {
    int cx = w->x + 16, cy = w->y + WIN_TITLEBAR_H + 24;
    if (g_netsurf_ran > 0) {
        rd_text(cx, cy, "A NetSurf munkamenet veget ert.", UI_PX, COL_TEXT);
        rd_text(cx, cy + 24, "Ujrainditas: nyisd meg ujra a NetSurf ikont.",
                UI_PX, COL_TEXT_MUTED);
    } else {
        rd_text(cx, cy, "netsurf.elf nem talalhato az initrd-ben!", UI_PX, COL_TEXT);
    }
}

const app_def_t app_netsurf = {
    .name = "NetSurf",
    .icon = ICON_GLOBE,
    .default_w = 480, .default_h = 160,
    .on_open = netsurf_open,
    .on_render = netsurf_render,
};

/* ============================================================================
 * System Monitor
 * ========================================================================== */
typedef struct {
    int tab;  /* 0=Overview, 1=PCI, 2=Block */
} sysmon_state_t;

static void sysmon_open(window_t *w) {
    sysmon_state_t *st = (sysmon_state_t *)malloc(sizeof(sysmon_state_t));
    memset(st, 0, sizeof(*st));
    w->state = st;
}

static const char *pci_class_name(uint8_t cl) {
    switch (cl) {
        case 0x00: return "Pre-2.0";
        case 0x01: return "Mass Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Comm";
        case 0x08: return "System Periph";
        case 0x09: return "Input";
        case 0x0C: return "Serial bus";
        default: return "Other";
    }
}

static void sysmon_render(window_t *w) {
    sysmon_state_t *st = (sysmon_state_t *)w->state;
    if (!st) return;
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    rd_rect(cx, cy, cw, ch, COL_SURFACE_1);

    /* Tabs */
    static const char *tabs[] = { "Overview", "PCI Devices", "Block Devices" };
    int tx = cx + 8, ty = cy + 8;
    for (int i = 0; i < 3; i++) {
        int tw = rd_text_w(tabs[i], UI_PX) + 24;
        int active = st->tab == i;
        int hov = pt_in_rect(g_mouse.x, g_mouse.y, tx, ty, tw, 28);
        uint32_t col = active ? COL_ACCENT_DARK
                              : (hov ? COL_HOVER : 0);
        if (col) rd_rounded(tx, ty, tw, 28, 4, col);
        rd_text(tx + 12, ty + 19, tabs[i], UI_PX,
                active ? COL_TEXT : COL_TEXT_MUTED);
        tx += tw + 4;
    }
    rd_rect(cx, cy + 40, cw, 1, COL_WIN_BORDER);

    int y = cy + 56;
    char buf[96]; char nb[24];

    if (st->tab == 0) {
        /* Overview: basic counts */
        rd_text(cx + 16, y, "Uptime (ticks):", UI_PX, COL_TEXT_MUTED);
        d_uint_to_str(get_ticks(), nb);
        rd_text(cx + 200, y, nb, UI_PX, COL_TEXT);
        y += 24;

        rd_text(cx + 16, y, "Display:", UI_PX, COL_TEXT_MUTED);
        d_uint_to_str(g_scr_w, nb);
        d_strcpy(buf, nb, sizeof(buf));
        d_strcat(buf, " x ", sizeof(buf));
        d_uint_to_str(g_scr_h, nb);
        d_strcat(buf, nb, sizeof(buf));
        rd_text(cx + 200, y, buf, UI_PX, COL_TEXT);
        y += 24;

        rd_text(cx + 16, y, "PCI devices:", UI_PX, COL_TEXT_MUTED);
        d_int_to_str(pci_dev_count(), nb);
        rd_text(cx + 200, y, nb, UI_PX, COL_TEXT);
        y += 24;

        rd_text(cx + 16, y, "Block devices:", UI_PX, COL_TEXT_MUTED);
        d_int_to_str(block_dev_count(), nb);
        rd_text(cx + 200, y, nb, UI_PX, COL_TEXT);
        y += 24;

        rd_text(cx + 16, y, "Open windows:", UI_PX, COL_TEXT_MUTED);
        d_int_to_str(wm_count_visible(), nb);
        rd_text(cx + 200, y, nb, UI_PX, COL_TEXT);
        y += 32;

        rd_text(cx + 16, y, "Tip: switch to other tabs for hardware list.",
                UI_PX_SMALL, COL_TEXT_DIM);
    } else if (st->tab == 1) {
        int n = pci_dev_count();
        rd_text(cx + 16, y, "B:D.F", UI_PX_SMALL, COL_TEXT_MUTED);
        rd_text(cx + 80, y, "Vend/Dev", UI_PX_SMALL, COL_TEXT_MUTED);
        rd_text(cx + 200, y, "Class", UI_PX_SMALL, COL_TEXT_MUTED);
        y += 18;
        for (int i = 0; i < n && i < 16; i++) {
            pci_info_t pi;
            if (pci_dev_info(i, &pi) != 0) continue;
            char a[8];
            a[0] = '0' + (pi.bus / 10) % 10;
            a[1] = '0' + pi.bus % 10;
            a[2] = ':';
            a[3] = '0' + (pi.dev / 10) % 10;
            a[4] = '0' + pi.dev % 10;
            a[5] = '.';
            a[6] = '0' + pi.func % 10;
            a[7] = 0;
            rd_text(cx + 16, y, a, UI_PX_SMALL, COL_TEXT);
            char vd[16];
            d_hex_to_str(pi.vendor, vd, 4);
            d_strcat(vd, ":", sizeof(vd));
            char dd[8]; d_hex_to_str(pi.device, dd, 4);
            d_strcat(vd, dd, sizeof(vd));
            rd_text(cx + 80, y, vd, UI_PX_SMALL, COL_TEXT);
            rd_text(cx + 200, y, pci_class_name(pi.class_code), UI_PX_SMALL, COL_TEXT);
            y += 18;
        }
    } else if (st->tab == 2) {
        int n = block_dev_count();
        rd_text(cx + 16, y, "Name", UI_PX_SMALL, COL_TEXT_MUTED);
        rd_text(cx + 140, y, "Sectors", UI_PX_SMALL, COL_TEXT_MUTED);
        rd_text(cx + 240, y, "Size (MB)", UI_PX_SMALL, COL_TEXT_MUTED);
        rd_text(cx + 360, y, "RW", UI_PX_SMALL, COL_TEXT_MUTED);
        y += 18;
        for (int i = 0; i < n && i < 8; i++) {
            block_info_t bi;
            if (block_dev_info(i, &bi) != 0) continue;
            rd_text(cx + 16, y, bi.name, UI_PX_SMALL, COL_TEXT);
            d_uint_to_str(bi.sector_count, nb);
            rd_text(cx + 140, y, nb, UI_PX_SMALL, COL_TEXT);
            uint64_t mb = (bi.sector_count * (uint64_t)bi.sector_size) / (1024 * 1024);
            d_uint_to_str(mb, nb);
            rd_text(cx + 240, y, nb, UI_PX_SMALL, COL_TEXT);
            rd_text(cx + 360, y, bi.writable ? "yes" : "no", UI_PX_SMALL,
                    bi.writable ? COL_GREEN : COL_TEXT_DIM);
            y += 18;
        }
    }
}

static int sysmon_event(window_t *w) {
    sysmon_state_t *st = (sysmon_state_t *)w->state;
    if (!st || !g_mouse.clicked) return 0;
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    (void)cw; (void)ch;
    static const char *tabs[] = { "Overview", "PCI Devices", "Block Devices" };
    int tx = cx + 8, ty = cy + 8;
    for (int i = 0; i < 3; i++) {
        int tw = rd_text_w(tabs[i], UI_PX) + 24;
        if (pt_in_rect(g_mouse.x, g_mouse.y, tx, ty, tw, 28)) {
            st->tab = i;
            return 1;
        }
        tx += tw + 4;
    }
    return 0;
}

const app_def_t app_sysmon = {
    .name = "System Monitor",
    .icon = ICON_SYSMON,
    .default_w = 540, .default_h = 380,
    .on_open = sysmon_open,
    .on_render = sysmon_render,
    .on_event = sysmon_event,
};

/* ============================================================================
 * Notes (in-memory text editor)
 * ========================================================================== */
#define NOTES_MAX 2048

typedef struct {
    char buf[NOTES_MAX];
    int  len;
} notes_state_t;

static void notes_open(window_t *w) {
    notes_state_t *st = (notes_state_t *)malloc(sizeof(notes_state_t));
    memset(st, 0, sizeof(*st));
    const char *welcome = "Welcome to RexOS Notes.\n\nClick this window and start typing.\nUse Backspace to delete.\n\n";
    int n = d_strlen(welcome);
    for (int i = 0; i < n && i < NOTES_MAX - 1; i++) st->buf[i] = welcome[i];
    st->len = n < NOTES_MAX - 1 ? n : NOTES_MAX - 1;
    st->buf[st->len] = 0;
    w->state = st;
}

static void notes_render(window_t *w) {
    notes_state_t *st = (notes_state_t *)w->state;
    if (!st) return;
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    rd_rect(cx, cy, cw, ch, COL_SURFACE_1);

    /* Wrap and render line by line. */
    int line_h = 18;
    int y = cy + 18;
    int x = cx + 16;
    char line[128];
    int li = 0;
    int max_w = cw - 32;
    for (int i = 0; i <= st->len; i++) {
        char c = (i < st->len) ? st->buf[i] : '\n';
        if (c == '\n' || li >= 120 || rd_text_w(line, UI_PX) > max_w) {
            line[li] = 0;
            rd_text(x, y, line, UI_PX, COL_TEXT);
            y += line_h;
            li = 0;
            if (y > cy + ch - 10) break;
            if (c == '\n') continue;
        }
        if (c >= 32 || c == '\t') line[li++] = c == '\t' ? ' ' : c;
    }
    /* Cursor at end */
    if ((g_tick / 30) % 2 == 0) {
        line[li] = 0;
        int cur_w = rd_text_w(line, UI_PX);
        rd_rect(x + cur_w, y - 14, 7, 14, COL_ACCENT);
    }

    /* Status */
    char buf[32]; char nb[16];
    d_int_to_str(st->len, nb);
    d_strcpy(buf, nb, sizeof(buf));
    d_strcat(buf, " chars", sizeof(buf));
    rd_text(cx + cw - rd_text_w(buf, UI_PX_SMALL) - 16, cy + ch - 8,
            buf, UI_PX_SMALL, COL_TEXT_DIM);
}

static int notes_event(window_t *w) {
    notes_state_t *st = (notes_state_t *)w->state;
    if (!st) return 0;
    while (kbd_has()) {
        char c = kbd_pop();
        if (c == 8 || c == 127) {
            if (st->len > 0) { st->len--; st->buf[st->len] = 0; }
        } else if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c < 127)) {
            if (st->len < NOTES_MAX - 1) {
                st->buf[st->len++] = (c == '\r' ? '\n' : c);
                st->buf[st->len] = 0;
            }
        }
    }
    return 1;
}

const app_def_t app_notes = {
    .name = "Notes",
    .icon = ICON_NOTES,
    .default_w = 520, .default_h = 380,
    .on_open = notes_open,
    .on_render = notes_render,
    .on_event = notes_event,
};

/* ============================================================================
 * Registry
 * ========================================================================== */
const app_def_t *g_apps[] = {
    &app_browser,
    &app_netsurf,
    &app_files,
    &app_terminal,
    &app_calc,
    &app_clock,
    &app_sysmon,
    &app_notes,
    &app_about,
};
int g_app_count = sizeof(g_apps) / sizeof(g_apps[0]);
