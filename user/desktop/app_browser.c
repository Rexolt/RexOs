/* =============================================================================
 *  RexOS Browser App
 *
 *  Modern web browser app integrated into the desktop:
 *    - URL bar with focus-state, padlock for HTTPS
 *    - Back / Forward / Reload / Home buttons
 *    - Minimal HTML renderer:
 *        * <title>, <h1>..<h3>, <p>, <br>, <hr>, <li>
 *        * Plain text with whitespace collapse
 *        * Basic entity decoding (&amp; &lt; &gt; &quot; &nbsp; &#39;)
 *    - Loading spinner during fetch (synchronous; freezes UI like 1995-vintage)
 *    - Vertical scrolling
 *
 *  Networking: uses user/http.c (TCP+TLS via BearSSL), works for http:// and https://.
 * ========================================================================== */
#include "desktop.h"
#include "../http.h"

#define BR_URL_MAX        256
#define BR_HISTORY_MAX    16
#define BR_BODY_MAX       (256 * 1024)
#define BR_RENDER_MAX     (512 * 1024)  /* room for layout text + tags */

typedef struct {
    char     url[BR_URL_MAX];
    char     url_input[BR_URL_MAX];
    int      url_len;
    int      url_focused;

    char     history[BR_HISTORY_MAX][BR_URL_MAX];
    int      hist_count;
    int      hist_pos;       /* index of current page in history */

    char    *body;            /* malloc'd, current page body (raw HTML) */
    int      body_len;
    int      status_code;
    int      is_https;
    int      loading;
    int      err;             /* HTTP_ERR_* or 0 */
    char     status_msg[64];
    char     page_title[128];

    int      scroll;          /* vertical scroll offset in px */
    int      content_h;       /* total laid-out height */
    uint64_t spin_start_tick;
} br_state_t;

/* ---- URL editing helpers ------------------------------------------------- */
static void br_url_apply(br_state_t *st) {
    /* Copy url_input -> url for fetching */
    int i;
    for (i = 0; i < st->url_len && i < BR_URL_MAX - 1; i++) st->url[i] = st->url_input[i];
    st->url[i] = 0;
}

static void br_url_set(br_state_t *st, const char *u) {
    int n = 0;
    for (; u[n] && n < BR_URL_MAX - 1; n++) {
        st->url_input[n] = u[n];
        st->url[n] = u[n];
    }
    st->url_input[n] = 0;
    st->url[n] = 0;
    st->url_len = n;
}

/* ---- Tiny case-insensitive matcher --------------------------------------- */
static int ci_eq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
        if (cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
        if (ca != cb) return 0;
    }
    return 1;
}

/* ---- Entity decoder ------------------------------------------------------- */
static int decode_entity(const char *s, int max, char *out) {
    /* Returns number of input chars consumed, or 0 if not an entity. */
    if (s[0] != '&') return 0;
    if (max >= 5 && ci_eq(s, "&amp;", 5)) { *out = '&'; return 5; }
    if (max >= 4 && ci_eq(s, "&lt;", 4))  { *out = '<'; return 4; }
    if (max >= 4 && ci_eq(s, "&gt;", 4))  { *out = '>'; return 4; }
    if (max >= 6 && ci_eq(s, "&quot;", 6)){ *out = '"'; return 6; }
    if (max >= 6 && ci_eq(s, "&nbsp;", 6)){ *out = ' '; return 6; }
    if (max >= 6 && ci_eq(s, "&apos;", 6)){ *out = '\''; return 6; }
    if (max >= 5 && ci_eq(s, "&#39;", 5)) { *out = '\''; return 5; }
    if (max >= 6 && ci_eq(s, "&copy;", 6)){ *out = 'C'; return 6; }
    /* Numeric &#NN; */
    if (s[1] == '#') {
        int code = 0; int i = 2;
        while (i < max && s[i] >= '0' && s[i] <= '9') { code = code*10 + (s[i]-'0'); i++; }
        if (i < max && s[i] == ';') {
            *out = (code >= 32 && code < 127) ? (char)code : '?';
            return i + 1;
        }
    }
    return 0;
}

/* ---- HTML to layout text -------------------------------------------------
 * We produce a flat sequence of "blocks". Each block is:
 *   {kind, text}
 *   kind: 0 = paragraph, 1 = h1, 2 = h2, 3 = h3, 4 = li, 5 = hr, 6 = pre
 * The renderer then draws them with appropriate font size / spacing.
 */

#define BR_BLOCK_MAX 256
#define BR_BLOCK_TEXT_MAX 1024

typedef struct {
    int  kind;
    char text[BR_BLOCK_TEXT_MAX];
} br_block_t;

static br_block_t s_blocks[BR_BLOCK_MAX];
static int        s_block_count = 0;

static void blocks_reset(void) { s_block_count = 0; }

static void blocks_push(int kind, const char *text) {
    if (s_block_count >= BR_BLOCK_MAX) return;
    br_block_t *b = &s_blocks[s_block_count++];
    b->kind = kind;
    int i = 0;
    while (text[i] && i < BR_BLOCK_TEXT_MAX - 1) { b->text[i] = text[i]; i++; }
    b->text[i] = 0;
}

/* Trim trailing whitespace and collapse runs to single space. */
static void clean_text(char *t) {
    int i = 0, j = 0;
    int prev_sp = 1; /* trim leading */
    while (t[i]) {
        char c = t[i];
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c == ' ') {
            if (!prev_sp) t[j++] = ' ';
            prev_sp = 1;
        } else {
            t[j++] = c;
            prev_sp = 0;
        }
        i++;
    }
    while (j > 0 && t[j-1] == ' ') j--;
    t[j] = 0;
}

static void parse_html(const char *html, int len, char *title_out, int title_max) {
    blocks_reset();
    title_out[0] = 0;

    char buf[BR_BLOCK_TEXT_MAX];
    int  bi = 0;
    int  current_kind = 0;  /* paragraph by default */
    int  in_script = 0, in_style = 0, in_title = 0;
    int  pre_depth = 0;

    int i = 0;
    while (i < len) {
        char c = html[i];
        if (c == '<') {
            /* Flush current buffer as block. */
            if (bi > 0 && !in_script && !in_style && !in_title) {
                buf[bi] = 0;
                clean_text(buf);
                if (buf[0]) blocks_push(current_kind, buf);
            }
            bi = 0;

            /* Parse tag */
            int j = i + 1;
            int closing = 0;
            if (j < len && html[j] == '/') { closing = 1; j++; }
            int tag_start = j;
            while (j < len && html[j] != '>' && html[j] != ' ' && html[j] != '\t' && html[j] != '\n') j++;
            int tag_len = j - tag_start;
            const char *tag = html + tag_start;
            /* Find tag end */
            while (j < len && html[j] != '>') j++;
            int tag_end = j;
            if (j < len) j++;

            /* Tag dispatch */
            if (tag_len > 0) {
                if (!closing && tag_len == 6 && ci_eq(tag, "script", 6)) in_script = 1;
                else if (closing && tag_len == 6 && ci_eq(tag, "script", 6)) in_script = 0;
                else if (!closing && tag_len == 5 && ci_eq(tag, "style", 5)) in_style = 1;
                else if (closing && tag_len == 5 && ci_eq(tag, "style", 5)) in_style = 0;
                else if (!closing && tag_len == 5 && ci_eq(tag, "title", 5)) in_title = 1;
                else if (closing && tag_len == 5 && ci_eq(tag, "title", 5)) {
                    in_title = 0;
                    /* Title is between the tags - extract from html[tag_start_of_open_close..] */
                    /* Actually we accumulated text since open tag. Copy from accumulated buf? */
                    /* For simplicity: title was being captured into buf. Move it out. */
                }
                else if (!closing && tag_len == 2 && (ci_eq(tag, "h1", 2) || ci_eq(tag, "h2", 2) || ci_eq(tag, "h3", 2))) {
                    current_kind = (tag[1] - '0');
                }
                else if (closing && tag_len == 2 && (ci_eq(tag, "h1", 2) || ci_eq(tag, "h2", 2) || ci_eq(tag, "h3", 2))) {
                    /* End heading - flush */
                    current_kind = 0;
                }
                else if (!closing && tag_len == 2 && ci_eq(tag, "li", 2)) current_kind = 4;
                else if (closing && tag_len == 2 && ci_eq(tag, "li", 2)) current_kind = 0;
                else if (tag_len == 2 && ci_eq(tag, "br", 2)) {
                    /* Force line break: push current buf */
                    if (bi > 0) { buf[bi] = 0; clean_text(buf); if (buf[0]) blocks_push(current_kind, buf); bi = 0; }
                    blocks_push(0, "");
                }
                else if (tag_len == 2 && ci_eq(tag, "hr", 2)) {
                    blocks_push(5, "");
                }
                else if (!closing && tag_len == 3 && ci_eq(tag, "pre", 3)) { pre_depth++; current_kind = 6; }
                else if (closing && tag_len == 3 && ci_eq(tag, "pre", 3)) { if (pre_depth > 0) pre_depth--; current_kind = 0; }
                else if (closing && tag_len == 1 && (tag[0] == 'p' || tag[0] == 'P')) { current_kind = 0; }
            }
            i = tag_end < len ? tag_end + 1 : len;
            (void)tag_end;
            i = j;
            continue;
        }
        if (c == '&') {
            char ec = 0;
            int n = decode_entity(html + i, len - i, &ec);
            if (n > 0) {
                if (in_title) {
                    int tl = 0; while (title_out[tl]) tl++;
                    if (tl < title_max - 1) { title_out[tl] = ec; title_out[tl + 1] = 0; }
                } else if (!in_script && !in_style) {
                    if (bi < BR_BLOCK_TEXT_MAX - 1) buf[bi++] = ec;
                }
                i += n;
                continue;
            }
        }
        if (in_title) {
            int tl = 0; while (title_out[tl]) tl++;
            if (tl < title_max - 1) { title_out[tl] = c; title_out[tl + 1] = 0; }
        } else if (!in_script && !in_style) {
            if (bi < BR_BLOCK_TEXT_MAX - 1) buf[bi++] = c;
        }
        i++;
    }
    if (bi > 0) {
        buf[bi] = 0;
        clean_text(buf);
        if (buf[0]) blocks_push(current_kind, buf);
    }
    /* Clean title */
    clean_text(title_out);
}

/* Forward declaration */
static void br_fetch_ex(br_state_t *st, int push_history);
static void br_fetch(br_state_t *st);

/* ---- Lifecycle ----------------------------------------------------------- */
static void br_open(window_t *w) {
    br_state_t *st = (br_state_t *)malloc(sizeof(br_state_t));
    memset(st, 0, sizeof(*st));
    br_url_set(st, "http://info.cern.ch/");
    d_strcpy(st->page_title, "New Tab", sizeof(st->page_title));
    d_strcpy(st->status_msg, "Ready - type a URL and press Enter", sizeof(st->status_msg));
    w->state = st;
}

static void br_close(window_t *w) {
    br_state_t *st = (br_state_t *)w->state;
    if (st && st->body) free(st->body);
}

/* ---- Fetch (synchronous) ------------------------------------------------- */
static void br_fetch_ex(br_state_t *st, int push_history) {
    /* Free any previous body */
    if (st->body) { free(st->body); st->body = 0; st->body_len = 0; }
    st->loading = 1;
    st->spin_start_tick = g_tick;
    st->err = 0;
    st->scroll = 0;

    /* Detect HTTPS */
    st->is_https = 0;
    if (d_strncmp(st->url, "https://", 8) == 0) st->is_https = 1;
    else if (d_strncmp(st->url, "http://", 7) != 0) {
        /* Auto-prefix https:// if user didn't type a scheme */
        char tmp[BR_URL_MAX];
        d_strcpy(tmp, "https://", sizeof(tmp));
        d_strcat(tmp, st->url, sizeof(tmp));
        br_url_set(st, tmp);
        st->is_https = 1;
    }

    /* Add to history (drop forward history if navigating from middle).
     * Back/Forward navigáció push_history=0-val hív, így nem duplikál. */
    if (push_history) {
        if (st->hist_pos < st->hist_count - 1) st->hist_count = st->hist_pos + 1;
        if (st->hist_count >= BR_HISTORY_MAX) {
            for (int i = 0; i < BR_HISTORY_MAX - 1; i++) {
                d_strcpy(st->history[i], st->history[i+1], BR_URL_MAX);
            }
            st->hist_count = BR_HISTORY_MAX - 1;
        }
        d_strcpy(st->history[st->hist_count], st->url, BR_URL_MAX);
        st->hist_pos = st->hist_count;
        st->hist_count++;
    }

    /* Allocate body buffer */
    char *body = (char *)malloc(BR_BODY_MAX);
    if (!body) {
        d_strcpy(st->status_msg, "Out of memory", sizeof(st->status_msg));
        st->err = HTTP_ERR_NO_MEMORY;
        st->loading = 0;
        return;
    }

    http_response_t resp;
    int rc = http_get(st->url, body, BR_BODY_MAX, &resp);
    if (rc < 0) {
        st->err = rc;
        switch (rc) {
            case HTTP_ERR_BAD_URL: d_strcpy(st->status_msg, "Bad URL", sizeof(st->status_msg)); break;
            case HTTP_ERR_CONNECT: d_strcpy(st->status_msg, "Connection failed", sizeof(st->status_msg)); break;
            case HTTP_ERR_HTTPS:   d_strcpy(st->status_msg, "TLS error", sizeof(st->status_msg)); break;
            case HTTP_ERR_RECV:    d_strcpy(st->status_msg, "Receive failed", sizeof(st->status_msg)); break;
            case HTTP_ERR_RESPONSE:d_strcpy(st->status_msg, "Bad response", sizeof(st->status_msg)); break;
            case HTTP_ERR_TOO_MANY_REDIRECTS: d_strcpy(st->status_msg, "Too many redirects", sizeof(st->status_msg)); break;
            default: d_strcpy(st->status_msg, "Network error", sizeof(st->status_msg)); break;
        }
        free(body);
        st->loading = 0;
        return;
    }
    st->body = body;
    st->body_len = resp.body_len;
    st->status_code = resp.status_code;
    /* Update URL with final (after redirects) */
    if (resp.final_url[0]) br_url_set(st, resp.final_url);
    /* Set status msg */
    char buf[64]; char nb[8];
    d_int_to_str(resp.status_code, nb);
    d_strcpy(buf, "HTTP ", sizeof(buf));
    d_strcat(buf, nb, sizeof(buf));
    d_strcat(buf, " - ", sizeof(buf));
    d_strcat(buf, resp.status, sizeof(buf));
    d_strcpy(st->status_msg, buf, sizeof(st->status_msg));
    /* Parse HTML into blocks */
    parse_html(st->body, st->body_len, st->page_title, sizeof(st->page_title));
    if (!st->page_title[0]) d_strcpy(st->page_title, st->url, sizeof(st->page_title));

    st->loading = 0;
}

static void br_fetch(br_state_t *st) {
    br_fetch_ex(st, 1);
}

/* ---- Layout helpers ------------------------------------------------------- */
static int block_font_px(int kind) {
    switch (kind) {
        case 1: return 28;
        case 2: return 22;
        case 3: return 18;
        case 6: return 13;
        default: return 15;
    }
}

static int block_top_pad(int kind) {
    switch (kind) {
        case 1: return 18;
        case 2: return 14;
        case 3: return 10;
        case 4: return 4;
        case 5: return 12;
        default: return 6;
    }
}

/* Word-wrap text and draw at (x,y) within content_w; return final y after draw.
 * Words are split on space. */
static int draw_wrapped(int x, int y, int content_w, const char *text, int px,
                        uint32_t color, int line_h, int do_draw) {
    int tx = x;
    int ty = y;
    int i = 0;
    char word[256];
    int wi = 0;
    while (1) {
        char c = text[i];
        if (c == 0 || c == ' ') {
            if (wi > 0) {
                word[wi] = 0;
                int ww = rd_text_w(word, px);
                if (tx + ww > x + content_w && tx > x) {
                    tx = x;
                    ty += line_h;
                }
                if (do_draw) rd_text(tx, ty + line_h - 4, word, px, color);
                tx += ww;
                wi = 0;
            }
            if (c == 0) break;
            int sw = rd_text_w(" ", px);
            tx += sw;
            i++;
            continue;
        }
        if (wi < (int)sizeof(word) - 1) word[wi++] = c;
        i++;
    }
    return ty + line_h;
}

/* ---- Render -------------------------------------------------------------- */
#define BR_TOOLBAR_H  44
#define BR_URLBAR_H   28
#define BR_STATUS_H   22

static void br_render_toolbar(window_t *w, br_state_t *st) {
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    (void)ch;

    /* Toolbar background */
    rd_rect(cx, cy, cw, BR_TOOLBAR_H, COL_SURFACE_2);
    rd_rect(cx, cy + BR_TOOLBAR_H - 1, cw, 1, COL_WIN_BORDER);

    /* Buttons */
    int btn_size = 28;
    int by = cy + (BR_TOOLBAR_H - btn_size) / 2;
    int bx = cx + 8;
    int gap = 4;

    /* Back */
    int can_back = st->hist_pos > 0;
    uint32_t bcol = can_back ? COL_TEXT : COL_TEXT_DIM;
    int hov = pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_size, btn_size);
    if (hov && can_back) rd_rounded(bx, by, btn_size, btn_size, 4, COL_HOVER);
    icon_draw(ICON_BACK, bx + 6, by + 6, 16, bcol, 0);
    bx += btn_size + gap;

    /* Forward */
    int can_fwd = st->hist_pos < st->hist_count - 1;
    uint32_t fcol = can_fwd ? COL_TEXT : COL_TEXT_DIM;
    hov = pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_size, btn_size);
    if (hov && can_fwd) rd_rounded(bx, by, btn_size, btn_size, 4, COL_HOVER);
    icon_draw(ICON_FORWARD, bx + 6, by + 6, 16, fcol, 0);
    bx += btn_size + gap;

    /* Reload */
    hov = pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_size, btn_size);
    if (hov) rd_rounded(bx, by, btn_size, btn_size, 4, COL_HOVER);
    icon_draw(ICON_RELOAD, bx + 6, by + 6, 16, COL_TEXT, 0);
    bx += btn_size + gap;

    /* Home */
    hov = pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_size, btn_size);
    if (hov) rd_rounded(bx, by, btn_size, btn_size, 4, COL_HOVER);
    icon_draw(ICON_HOME, bx + 6, by + 6, 16, COL_TEXT, 0);
    bx += btn_size + gap + 4;

    /* URL bar */
    int urlbar_x = bx;
    int urlbar_w = cx + cw - urlbar_x - 8;
    int urlbar_y = cy + (BR_TOOLBAR_H - BR_URLBAR_H) / 2;
    uint32_t border = st->url_focused ? COL_ACCENT : COL_WIN_BORDER;
    rd_rounded(urlbar_x, urlbar_y, urlbar_w, BR_URLBAR_H, 4, COL_SURFACE_1);
    rd_rounded_outline(urlbar_x, urlbar_y, urlbar_w, BR_URLBAR_H, 4, border);
    /* lock if https */
    int text_x = urlbar_x + 10;
    if (st->is_https && !st->url_focused) {
        icon_draw(ICON_LOCK, urlbar_x + 6, urlbar_y + 6, 16, COL_GREEN, 0);
        text_x = urlbar_x + 24;
    }
    /* URL text */
    const char *display = st->url_focused ? st->url_input : st->url;
    rd_text(text_x, urlbar_y + 18, display, UI_PX, COL_TEXT);
    /* cursor blink */
    if (st->url_focused && (g_tick / 30) % 2 == 0) {
        char tmp[BR_URL_MAX];
        d_strcpy(tmp, st->url_input, sizeof(tmp));
        tmp[st->url_len] = 0;
        int cw2 = rd_text_w(tmp, UI_PX);
        rd_rect(text_x + cw2, urlbar_y + 6, 1, BR_URLBAR_H - 12, COL_ACCENT);
    }
}

static void br_render_loading(int cx, int cy, int cw, int ch, br_state_t *st) {
    /* Centered spinner: 8 dots rotating */
    int ccx = cx + cw / 2;
    int ccy = cy + ch / 2;
    int r = 18;
    int phase = (int)((g_tick - st->spin_start_tick) / 4) % 8;
    for (int i = 0; i < 8; i++) {
        int rel = (i - phase + 8) % 8;
        int alpha = 60 + rel * 24;
        if (alpha > 255) alpha = 255;
        /* approximate trig with table */
        static const int sx[8] = {  0,  4,  6,  4,  0, -4, -6, -4 };
        static const int sy[8] = { -6, -4,  0,  4,  6,  4,  0, -4 };
        int dx = sx[i] * r / 6;
        int dy = sy[i] * r / 6;
        rd_blend_rect(ccx + dx - 2, ccy + dy - 2, 4, 4, COL_ACCENT, (uint8_t)alpha);
    }
    rd_text(ccx - rd_text_w("Loading...", UI_PX) / 2, ccy + 40,
            "Loading...", UI_PX, COL_TEXT_MUTED);
}

static void br_render(window_t *w) {
    br_state_t *st = (br_state_t *)w->state;
    if (!st) return;
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);

    /* Clear content area */
    rd_rect(cx, cy, cw, ch, COL_WIN_BG);

    br_render_toolbar(w, st);

    int content_y = cy + BR_TOOLBAR_H;
    int content_h = ch - BR_TOOLBAR_H - BR_STATUS_H;

    rd_set_clip(cx, content_y, cw, content_h);

    if (st->loading) {
        br_render_loading(cx, content_y, cw, content_h, st);
    } else if (st->err) {
        /* Error page */
        int ey = content_y + 60;
        rd_text_bold(cx + 32, ey, "Could not load page", 26, COL_TEXT);
        rd_text(cx + 32, ey + 36, st->status_msg, UI_PX, COL_TEXT_MUTED);
        rd_text(cx + 32, ey + 60, st->url, UI_PX_SMALL, COL_TEXT_DIM);
        rd_text(cx + 32, ey + 96, "Try checking the URL or your network connection.",
                UI_PX, COL_TEXT_MUTED);
    } else if (st->body) {
        /* Render blocks */
        int margin = 32;
        int x = cx + margin;
        int content_w = cw - 2 * margin;
        int y = content_y + 16 - st->scroll;

        /* Page title (big, bold) */
        if (st->page_title[0]) {
            rd_text_bold(x, y + 28, st->page_title, 24, COL_TEXT);
            y += 44;
            rd_rect(x, y, content_w, 1, COL_WIN_BORDER);
            y += 16;
        }

        for (int i = 0; i < s_block_count; i++) {
            br_block_t *b = &s_blocks[i];
            y += block_top_pad(b->kind);
            if (b->kind == 5) {
                rd_rect(x, y, content_w, 1, COL_WIN_BORDER);
                y += 8;
                continue;
            }
            int px = block_font_px(b->kind);
            int line_h = px + 6;
            uint32_t color = COL_TEXT;
            int prefix_w = 0;
            if (b->kind == 4) {
                rd_text(x + 4, y + line_h - 4, "•", px, COL_ACCENT);
                prefix_w = 16;
            }
            if (b->kind >= 1 && b->kind <= 3) color = COL_TEXT;
            int do_draw = (y > content_y - line_h && y < content_y + content_h);
            int new_y = draw_wrapped(x + prefix_w, y, content_w - prefix_w,
                                     b->text, px, color, line_h, do_draw);
            if (b->kind >= 1 && b->kind <= 3) {
                /* Bold via second pass */
                if (do_draw) draw_wrapped(x + prefix_w + 1, y, content_w - prefix_w,
                                          b->text, px, color, line_h, 1);
            }
            y = new_y;
        }
        st->content_h = y + st->scroll - content_y;

        /* Scroll indicator */
        if (st->content_h > content_h) {
            int track_h = content_h - 8;
            int thumb_h = track_h * content_h / st->content_h;
            if (thumb_h < 24) thumb_h = 24;
            int max_scroll = st->content_h - content_h;
            int thumb_y = content_y + 4 + (max_scroll > 0 ? (track_h - thumb_h) * st->scroll / max_scroll : 0);
            rd_rounded(cx + cw - 8, thumb_y, 4, thumb_h, 2, COL_TEXT_DIM);
        }
    } else {
        /* Welcome screen */
        int wy = content_y + 80;
        rd_text_bold(cx + 32, wy, "RexBrowser", 32, COL_ACCENT);
        rd_text(cx + 32, wy + 36, "A minimal modern web browser", UI_PX_LARGE, COL_TEXT_MUTED);
        rd_text(cx + 32, wy + 80, "Type a URL and press Enter to navigate.",
                UI_PX, COL_TEXT);
        rd_text(cx + 32, wy + 104, "HTTPS supported via BearSSL TLS.",
                UI_PX, COL_TEXT_MUTED);
        rd_text(cx + 32, wy + 140, "Try:", UI_PX, COL_TEXT_MUTED);
        rd_text(cx + 48, wy + 164, "https://example.com/", UI_PX, COL_ACCENT);
        rd_text(cx + 48, wy + 188, "http://info.cern.ch/", UI_PX, COL_ACCENT);
    }

    rd_clear_clip();

    /* Status bar */
    int sy = cy + ch - BR_STATUS_H;
    rd_rect(cx, sy, cw, BR_STATUS_H, COL_SURFACE_2);
    rd_rect(cx, sy, cw, 1, COL_WIN_BORDER);
    uint32_t scol = st->err ? COL_RED : (st->loading ? COL_YELLOW : COL_GREEN);
    rd_circle_fill(cx + 12, sy + BR_STATUS_H / 2, 4, scol);
    rd_text(cx + 24, sy + 16, st->status_msg, UI_PX_SMALL, COL_TEXT_MUTED);
    /* body length on right */
    if (st->body_len > 0) {
        char b[32]; char nb[16];
        d_int_to_str(st->body_len, nb);
        d_strcpy(b, nb, sizeof(b));
        d_strcat(b, " bytes", sizeof(b));
        rd_text(cx + cw - rd_text_w(b, UI_PX_SMALL) - 12, sy + 16, b,
                UI_PX_SMALL, COL_TEXT_DIM);
    }
}

/* ---- Event handling ------------------------------------------------------- */
static int br_event(window_t *w) {
    br_state_t *st = (br_state_t *)w->state;
    if (!st) return 0;
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);

    /* Toolbar buttons */
    if (g_mouse.clicked) {
        int btn_size = 28;
        int by = cy + (BR_TOOLBAR_H - btn_size) / 2;
        int bx = cx + 8;
        int gap = 4;

        /* Back */
        if (pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_size, btn_size)) {
            if (st->hist_pos > 0) {
                st->hist_pos--;
                br_url_set(st, st->history[st->hist_pos]);
                br_fetch_ex(st, 0); /* history-pozíció megmarad, nincs dup */
            }
            return 1;
        }
        bx += btn_size + gap;

        /* Forward */
        if (pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_size, btn_size)) {
            if (st->hist_pos < st->hist_count - 1) {
                st->hist_pos++;
                br_url_set(st, st->history[st->hist_pos]);
                br_fetch_ex(st, 0);
            }
            return 1;
        }
        bx += btn_size + gap;

        /* Reload */
        if (pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_size, btn_size)) {
            br_fetch(st);
            return 1;
        }
        bx += btn_size + gap;

        /* Home */
        if (pt_in_rect(g_mouse.x, g_mouse.y, bx, by, btn_size, btn_size)) {
            br_url_set(st, "https://example.com/");
            br_fetch(st);
            return 1;
        }
        bx += btn_size + gap + 4;

        /* URL bar click - focus */
        int urlbar_x = bx;
        int urlbar_w = cx + cw - urlbar_x - 8;
        int urlbar_y = cy + (BR_TOOLBAR_H - BR_URLBAR_H) / 2;
        if (pt_in_rect(g_mouse.x, g_mouse.y, urlbar_x, urlbar_y, urlbar_w, BR_URLBAR_H)) {
            st->url_focused = 1;
            d_strcpy(st->url_input, st->url, sizeof(st->url_input));
            st->url_len = d_strlen(st->url_input);
            return 1;
        }

        /* Click in content area: defocus URL bar */
        if (pt_in_rect(g_mouse.x, g_mouse.y, cx, cy + BR_TOOLBAR_H, cw, ch - BR_TOOLBAR_H - BR_STATUS_H)) {
            st->url_focused = 0;
        }
    }

    /* Keyboard input */
    while (kbd_has()) {
        char c = kbd_pop();
        if (st->url_focused) {
            if (c == '\n' || c == '\r') {
                br_url_apply(st);
                st->url_focused = 0;
                br_fetch(st);
            } else if (c == 8 || c == 127) {
                if (st->url_len > 0) {
                    st->url_len--;
                    st->url_input[st->url_len] = 0;
                }
            } else if (c == 27) {
                /* Escape: cancel edit */
                st->url_focused = 0;
                d_strcpy(st->url_input, st->url, sizeof(st->url_input));
                st->url_len = d_strlen(st->url_input);
            } else if (c >= 32 && c < 127 && st->url_len < BR_URL_MAX - 1) {
                st->url_input[st->url_len++] = c;
                st->url_input[st->url_len] = 0;
            }
        } else {
            /* Page-level shortcuts */
            if (c == 'r' || c == 'R') br_fetch(st);
            else if (c == ' ' && st->body) st->scroll += 200;
            else if (c == 'j' && st->body) st->scroll += 40;
            else if (c == 'k' && st->body) st->scroll -= 40;
            else if (c == 'g' && st->body) st->scroll = 0;
        }
    }

    /* Clamp scroll */
    int content_h = ch - BR_TOOLBAR_H - BR_STATUS_H;
    int max_scroll = st->content_h - content_h;
    if (max_scroll < 0) max_scroll = 0;
    if (st->scroll < 0) st->scroll = 0;
    if (st->scroll > max_scroll) st->scroll = max_scroll;

    return 1;
}

const app_def_t app_browser = {
    .name = "Browser",
    .icon = ICON_BROWSER,
    .default_w = 820, .default_h = 560,
    .on_open = br_open,
    .on_close = br_close,
    .on_render = br_render,
    .on_event = br_event,
};
