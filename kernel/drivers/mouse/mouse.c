/* ============================================================================
 *  Rex OS - PS/2 Mouse Driver (IRQ12)
 * ========================================================================== */

#include <drivers/mouse/mouse.h>
#include <arch/x86_64/irq.h>
#include <rexos/io.h>
#include <lib/printf.h>

#define MOUSE_DATA   0x60
#define MOUSE_STATUS 0x64
#define MOUSE_CMD    0x64

static volatile int32_t  s_x = 640, s_y = 400;
static volatile uint8_t  s_buttons = 0;
static volatile uint32_t s_max_x = 1280, s_max_y = 800;
static volatile uint8_t  s_cycle = 0;
static volatile uint8_t  s_packet[3];

static inline void mouse_wait_write(void) {
    int timeout = 100000;
    while ((inb(MOUSE_STATUS) & 0x02) && --timeout);
}

static inline void mouse_wait_read(void) {
    int timeout = 100000;
    while (!(inb(MOUSE_STATUS) & 0x01) && --timeout);
}

static void mouse_cmd(uint8_t cmd) {
    mouse_wait_write();
    outb(MOUSE_CMD, 0xD4);   /* prefix: a következő bájt az egérnek szól */
    mouse_wait_write();
    outb(MOUSE_DATA, cmd);
    mouse_wait_read();
    (void)inb(MOUSE_DATA);   /* ACK eldobása */
}

static void mouse_irq_handler(uint8_t irq, struct interrupt_frame *f) {
    (void)irq; (void)f;
    uint8_t data = inb(MOUSE_DATA);

    /* Első bájt szinkronizálás: bit 3-nak mindig 1-nek kell lennie.
       Ha nem az, ez nem érvényes első bájt — dobjuk el és kezdjük újra. */
    if (s_cycle == 0 && !(data & 0x08)) {
        return; /* desync — skipeljük */
    }

    s_packet[s_cycle] = data;
    s_cycle++;

    if (s_cycle >= 3) {
        s_cycle = 0;
        uint8_t flags = s_packet[0];

        int32_t dx = (int32_t)s_packet[1];
        int32_t dy = (int32_t)s_packet[2];

        /* Overflow flag-ek: ha be vannak állítva, a delta hibás */
        if (flags & 0xC0) return; /* X/Y overflow — dobjuk */

        /* Előjel-kiterjesztés */
        if (flags & 0x10) dx |= (int32_t)0xFFFFFF00;
        if (flags & 0x20) dy |= (int32_t)0xFFFFFF00;

        s_x += dx;
        s_y -= dy; /* PS/2 az Y-t fordítva adja */

        /* Clamp a képernyőre */
        if (s_x < 0) s_x = 0;
        if (s_y < 0) s_y = 0;
        if (s_x >= (int32_t)s_max_x) s_x = (int32_t)s_max_x - 1;
        if (s_y >= (int32_t)s_max_y) s_y = (int32_t)s_max_y - 1;

        s_buttons = flags & 0x07;
    }
}

void mouse_init(void) {
    /* 1. Controller konfigurálás: a 2. PS/2 port (egér) IRQ engedélyezése */
    mouse_wait_write();
    outb(MOUSE_CMD, 0xA8);   /* Enable auxiliary device */

    mouse_wait_write();
    outb(MOUSE_CMD, 0x20);   /* Olvassuk a controller config bájtot */
    mouse_wait_read();
    uint8_t config = inb(MOUSE_DATA);
    config |= 0x02;           /* Bit 1: IRQ12 engedélyezése */
    config &= ~0x20;          /* Bit 5: egér órajel tiltás kikapcs */
    mouse_wait_write();
    outb(MOUSE_CMD, 0x60);   /* Írjuk vissza a config-ot */
    mouse_wait_write();
    outb(MOUSE_DATA, config);

    /* 2. Egér alapértelmezett beállítások és engedélyezés */
    mouse_cmd(0xF6);          /* Set Defaults */
    mouse_cmd(0xF4);          /* Enable Data Reporting */

    /* Drain pending data */
    while (inb(MOUSE_STATUS) & 0x01) (void)inb(MOUSE_DATA);

    irq_register(12, mouse_irq_handler);
    kprintf("[mouse] PS/2 mouse initialized (IRQ12)\n");
}

void mouse_get_state(mouse_state_t *out) {
    out->x = s_x;
    out->y = s_y;
    out->buttons = s_buttons;
}

void mouse_set_bounds(uint32_t max_x, uint32_t max_y) {
    s_max_x = max_x;
    s_max_y = max_y;
}
