/*
 * libnsfb surface a RexOS-hez.
 *
 * Közvetlenül a kernel framebufferébe rajzol (RSYS_GET_FB mapping), az
 * inputot a RexOS billentyű/egér syscallok pollozásából állítja elő.
 *
 * A NSFB_SURFACE_LINUX enum slotot használjuk: így ez lesz a legkisebb
 * sorszámú regisztrált surface, amit a NetSurf framebuffer frontend
 * alapértelmezettként választ.
 *
 * A build során a libnsfb/src/surface/rexos.c néven fordul
 * (tools/build-netsurf.sh másolja be).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_plot_util.h"
#include "libnsfb_event.h"

#include "nsfb.h"
#include "surface.h"
#include "plot.h"
#include "cursor.h"

/* --- RexOS syscallok (duplikálva a rexos_syscall.h-ból, hogy a libnsfb fa
 *     önállóan forduljon) --- */

#define RSYS_GET_FB    4
#define RSYS_KBD_POLL  11
#define RSYS_MOUSE     12
#define RSYS_SLEEP_MS  29

static inline uint64_t rexos_sc3(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3)
{
    uint64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

/* --- belső állapot ---------------------------------------------------------- */

#define EVQ_SIZE 32

struct rexos_priv {
    uint32_t last_mx, last_my, last_buttons;
    nsfb_event_t evq[EVQ_SIZE];
    int evq_head, evq_tail;
};

static void evq_push(struct rexos_priv *p, const nsfb_event_t *ev)
{
    int next = (p->evq_tail + 1) % EVQ_SIZE;
    if (next == p->evq_head) return; /* tele - eldobjuk */
    p->evq[p->evq_tail] = *ev;
    p->evq_tail = next;
}

static bool evq_pop(struct rexos_priv *p, nsfb_event_t *ev)
{
    if (p->evq_head == p->evq_tail) return false;
    *ev = p->evq[p->evq_head];
    p->evq_head = (p->evq_head + 1) % EVQ_SIZE;
    return true;
}

/* --- surface műveletek ------------------------------------------------------- */

static int rexos_defaults(nsfb_t *nsfb)
{
    uint64_t w, h, pitch;
    void *fb = (void *)rexos_sc3(RSYS_GET_FB, (uint64_t)&w, (uint64_t)&h,
                                 (uint64_t)&pitch);
    if (!fb) return -1;

    nsfb->width = (int)w;
    nsfb->height = (int)h;
    nsfb->format = NSFB_FMT_XRGB8888;

    select_plotters(nsfb);
    return 0;
}

static int rexos_initialise(nsfb_t *nsfb)
{
    uint64_t w, h, pitch;
    void *fb = (void *)rexos_sc3(RSYS_GET_FB, (uint64_t)&w, (uint64_t)&h,
                                 (uint64_t)&pitch);
    if (!fb) return -1;

    struct rexos_priv *priv = calloc(1, sizeof(*priv));
    if (!priv) return -1;

    /* mindig a valódi framebuffer méretét használjuk */
    nsfb->width = (int)w;
    nsfb->height = (int)h;
    nsfb->format = NSFB_FMT_XRGB8888;
    nsfb->bpp = 32;
    nsfb->linelen = (int)pitch;
    nsfb->ptr = fb;
    nsfb->surface_priv = priv;

    select_plotters(nsfb);
    return 0;
}

static int rexos_finalise(nsfb_t *nsfb)
{
    free(nsfb->surface_priv);
    nsfb->surface_priv = NULL;
    return 0;
}

static int
rexos_set_geometry(nsfb_t *nsfb, int width, int height,
                   enum nsfb_format_e format)
{
    /* A geometria fix: a hardver framebuffer méretét adjuk vissza. */
    (void)width; (void)height;

    uint64_t w, h, pitch;
    void *fb = (void *)rexos_sc3(RSYS_GET_FB, (uint64_t)&w, (uint64_t)&h,
                                 (uint64_t)&pitch);
    if (!fb) return -1;

    nsfb->width = (int)w;
    nsfb->height = (int)h;
    nsfb->bpp = 32;
    nsfb->linelen = (int)pitch;
    nsfb->ptr = fb;
    if (format != NSFB_FMT_ANY) nsfb->format = NSFB_FMT_XRGB8888;

    select_plotters(nsfb);
    return 0;
}

/* billentyű: a kernel ASCII karaktereket ad; DOWN+UP párrá alakítjuk */
static void poll_keyboard(struct rexos_priv *p)
{
    for (;;) {
        char c = (char)rexos_sc3(RSYS_KBD_POLL, 0, 0, 0);
        if (!c) break;

        int code;
        switch (c) {
        case '\n':
        case '\r': code = NSFB_KEY_RETURN;    break;
        case '\b':
        case 127:  code = NSFB_KEY_BACKSPACE; break;
        case '\t': code = NSFB_KEY_TAB;       break;
        case 27:   code = NSFB_KEY_ESCAPE;    break;
        default:
            if ((unsigned char)c < 32 || (unsigned char)c > 126) continue;
            code = (int)(unsigned char)c;
            break;
        }

        nsfb_event_t ev;
        ev.type = NSFB_EVENT_KEY_DOWN;
        ev.value.keycode = code;
        evq_push(p, &ev);
        ev.type = NSFB_EVENT_KEY_UP;
        evq_push(p, &ev);
    }
}

static void poll_mouse(struct rexos_priv *p)
{
    uint32_t st[3];
    rexos_sc3(RSYS_MOUSE, (uint64_t)st, 0, 0);
    uint32_t mx = st[0], my = st[1], btn = st[2];

    if (mx != p->last_mx || my != p->last_my) {
        nsfb_event_t ev;
        ev.type = NSFB_EVENT_MOVE_ABSOLUTE;
        ev.value.vector.x = (int)mx;
        ev.value.vector.y = (int)my;
        ev.value.vector.z = 0;
        evq_push(p, &ev);
        p->last_mx = mx;
        p->last_my = my;
    }

    uint32_t changed = btn ^ p->last_buttons;
    static const int map[3] = { NSFB_KEY_MOUSE_1, NSFB_KEY_MOUSE_3,
                                NSFB_KEY_MOUSE_2 }; /* bit1=jobb gomb */
    for (int b = 0; b < 3; b++) {
        if (changed & (1u << b)) {
            nsfb_event_t ev;
            ev.type = (btn & (1u << b)) ? NSFB_EVENT_KEY_DOWN
                                        : NSFB_EVENT_KEY_UP;
            ev.value.keycode = map[b];
            evq_push(p, &ev);
        }
    }
    p->last_buttons = btn;
}

static bool rexos_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
    struct rexos_priv *p = nsfb->surface_priv;
    if (!p) return false;

    if (evq_pop(p, event)) return true;

    int waited = 0;
    for (;;) {
        poll_keyboard(p);
        poll_mouse(p);
        if (evq_pop(p, event)) return true;

        if (timeout == 0) return false;
        if (timeout > 0 && waited >= timeout) return false;

        rexos_sc3(RSYS_SLEEP_MS, 5, 0, 0);
        waited += 5;
    }
}

static int rexos_claim(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    struct nsfb_cursor_s *cursor = nsfb->cursor;

    if ((cursor != NULL) &&
        (cursor->plotted == true) &&
        (nsfb_plot_bbox_intersect(box, &cursor->loc))) {
        nsfb_cursor_clear(nsfb, cursor);
    }
    return 0;
}

static int rexos_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    (void)box; /* közvetlenül a hardver fb-be rajzolunk, nincs blit */
    struct nsfb_cursor_s *cursor = nsfb->cursor;

    if ((cursor != NULL) && (cursor->plotted == false)) {
        nsfb_cursor_plot(nsfb, cursor);
    }
    return 0;
}

static int rexos_cursor(nsfb_t *nsfb, struct nsfb_cursor_s *cursor)
{
    if ((cursor != NULL) && (cursor->plotted == true)) {
        nsfb_cursor_clear(nsfb, cursor);
        nsfb_cursor_plot(nsfb, cursor);
    }
    return true;
}

const nsfb_surface_rtns_t rexos_rtns = {
    .defaults = rexos_defaults,
    .initialise = rexos_initialise,
    .finalise = rexos_finalise,
    .geometry = rexos_set_geometry,
    .input = rexos_input,
    .claim = rexos_claim,
    .update = rexos_update,
    .cursor = rexos_cursor,
};

NSFB_SURFACE_DEF(rexos, NSFB_SURFACE_LINUX, &rexos_rtns)
