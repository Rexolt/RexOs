/* ============================================================================
 *  Rex OS - PS/2 keyboard driver
 *
 *  Port 0x60: adat (scancode)
 *  Port 0x64: status / command
 *  IRQ1: minden billentyű le- és felengedéskor érkezik.
 *
 *  Set 1 scancode-okat használjuk (default QEMU emuláció).
 *  A scancode bit 7 = 1 jelzi a felengedést (Break code).
 *  Most csak az US-QWERTY printable + néhány vezérlőt támogatjuk;
 *  shift/ctrl/alt módosítók a következő iterációban jönnek.
 * ========================================================================== */

#include <drivers/keyboard/keyboard.h>
#include <arch/x86_64/irq.h>
#include <rexos/io.h>
#include <lib/printf.h>

#define KBD_DATA    0x60
#define KBD_STATUS  0x64

/* Set-1 scancode -> ASCII map (Make code-okra, US-QWERTY layout) */
static const char sc1_ascii[128] = {
    /* 0x00 */  0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    /* 0x10 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',  0,  'a', 's',
    /* 0x20 */ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`',  0, '\\','z', 'x', 'c', 'v',
    /* 0x30 */ 'b', 'n', 'm', ',', '.', '/',  0,  '*',  0,  ' ',  0,   0,   0,   0,   0,   0,
    /* 0x40 */  0,   0,   0,   0,   0,   0,   0,  '7','8','9', '-', '4','5','6', '+','1',
    /* 0x50 */ '2', '3', '0', '.',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x60..0x7F: 0 */
};

#define BUFFER_SIZE 64
static volatile char     s_buf[BUFFER_SIZE];
static volatile uint32_t s_head = 0;   /* írás index */
static volatile uint32_t s_tail = 0;   /* olvasás index */

static inline void buf_push(char c)
{
    uint32_t next = (s_head + 1) % BUFFER_SIZE;
    if (next != s_tail) {           /* buffer full -> drop */
        s_buf[s_head] = c;
        s_head = next;
    }
}

static void kbd_irq_handler(uint8_t irq, struct interrupt_frame *f)
{
    (void)irq; (void)f;
    uint8_t sc = inb(KBD_DATA);

    /* Break code (felengedés): figyelmen kívül hagyjuk most. */
    if (sc & 0x80) return;

    char c = sc1_ascii[sc & 0x7F];
    if (c) buf_push(c);
}

void keyboard_init(void)
{
    /* Drain bármilyen pending adatot a controller bufferéből. */
    while (inb(KBD_STATUS) & 0x01) {
        (void)inb(KBD_DATA);
    }
    irq_register(1, kbd_irq_handler);
}

bool keyboard_has_data(void)
{
    return s_head != s_tail;
}

char keyboard_getc(void)
{
    if (s_head == s_tail) return 0;
    char c = s_buf[s_tail];
    s_tail = (s_tail + 1) % BUFFER_SIZE;
    return c;
}
