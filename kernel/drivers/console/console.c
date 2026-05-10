/* ============================================================================
 *  Rex OS - Framebuffer text console
 *
 *  A framebufferre felépített karakter-grid. Funkciók:
 *    - putc (támogatott speciális: \n, \r, \b, \t)
 *    - automatikus sortörés
 *    - scrolling (egész képernyő felfelé tolása FONT_H pixellel)
 *    - cursor pozíció követés
 *
 *  A printf.c hívja, ha console_is_ready() igaz - így boot előtt csak a serial,
 *  fb init után a serial + framebuffer is kapja az outputot.
 * ========================================================================== */

#include <drivers/console/console.h>
#include <drivers/framebuffer/fb.h>

#define DEFAULT_FG 0x00E0E0E0u   /* világos szürke */
#define DEFAULT_BG 0x00101820u   /* sötét kék-fekete */
#define TAB_WIDTH  4

static bool     s_ready = false;
static uint32_t s_cols  = 0;
static uint32_t s_rows  = 0;
static uint32_t s_cur_x = 0;
static uint32_t s_cur_y = 0;
static uint32_t s_fg    = DEFAULT_FG;
static uint32_t s_bg    = DEFAULT_BG;

static void scroll_up(void)
{
    /* Az alsó (s_rows-1) sornyi pixelt felfelé toljuk FONT_H pixellel. */
    fb_blit_rows(0, FONT_H, (s_rows - 1) * FONT_H);
    /* A legalsó sort kitisztítjuk. */
    fb_fill_rect(0, (s_rows - 1) * FONT_H,
                 s_cols * FONT_W, FONT_H, s_bg);
}

static void newline(void)
{
    s_cur_x = 0;
    if (s_cur_y + 1 >= s_rows) {
        scroll_up();
    } else {
        s_cur_y++;
    }
}

void console_init(void)
{
    if (fb_width() == 0 || fb_height() == 0) return;

    s_cols = fb_width()  / FONT_W;
    s_rows = fb_height() / FONT_H;
    s_cur_x = 0;
    s_cur_y = 0;

    fb_clear(s_bg);
    s_ready = true;
}

bool console_is_ready(void) { return s_ready; }

void console_set_colors(uint32_t fg, uint32_t bg)
{
    s_fg = fg;
    s_bg = bg;
}

void console_clear(void)
{
    if (!s_ready) return;
    fb_clear(s_bg);
    s_cur_x = 0;
    s_cur_y = 0;
}

void console_putc(char c)
{
    if (!s_ready) return;

    switch (c) {
    case '\n':
        newline();
        return;
    case '\r':
        s_cur_x = 0;
        return;
    case '\b':
        if (s_cur_x > 0) {
            s_cur_x--;
        } else if (s_cur_y > 0) {
            s_cur_y--;
            s_cur_x = s_cols - 1;
        }
        fb_put_char(s_cur_x * FONT_W, s_cur_y * FONT_H, ' ', s_fg, s_bg);
        return;
    case '\t': {
        uint32_t spaces = TAB_WIDTH - (s_cur_x % TAB_WIDTH);
        for (uint32_t i = 0; i < spaces; i++) console_putc(' ');
        return;
    }
    default:
        break;
    }

    /* Nem-printable karaktert egyszerűen ignorálunk (kivéve a fentieket). */
    if ((uint8_t)c < 32 && c != ' ') return;

    fb_put_char(s_cur_x * FONT_W, s_cur_y * FONT_H, (uint8_t)c, s_fg, s_bg);
    s_cur_x++;
    if (s_cur_x >= s_cols) {
        newline();
    }
}

void console_puts(const char *s)
{
    while (*s) console_putc(*s++);
}
