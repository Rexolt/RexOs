/* ============================================================================
 *  Rex OS - PIT driver
 *
 *  Base frequency: 1.193182 MHz (PC örökség, ~14.31818 MHz / 12)
 *  Mód 3 (square wave generator), channel 0 generálja az IRQ0-t.
 *
 *  Command byte (port 0x43):
 *    bit 7..6: channel select (00 = ch0)
 *    bit 5..4: access mode    (11 = lobyte/hibyte)
 *    bit 3..1: operating mode (011 = square wave)
 *    bit 0   : BCD/binary     (0 = binary)
 *    -> 0b00110110 = 0x36
 * ========================================================================== */

#include <drivers/timer/pit.h>
#include <arch/x86_64/irq.h>
#include <rexos/io.h>

#define PIT_BASE_FREQ   1193182u
#define PIT_CHANNEL0    0x40
#define PIT_CMD         0x43
#define PIT_CMD_BYTE    0x36

#include <sched/sched.h>

static volatile uint64_t s_ticks = 0;
static uint32_t          s_hz    = 0;

static void pit_irq_handler(uint8_t irq, struct interrupt_frame *f)
{
    (void)irq; (void)f;
    s_ticks++;

    /* Preempció: Minden 10. ticknél (kb. 100ms) kikényszerítjük a taszkváltást */
    if (s_ticks % 10 == 0) {
        g_need_resched = 1;
    }
}

void pit_init(uint32_t hz)
{
    if (hz == 0) hz = 1000;
    uint32_t divisor = PIT_BASE_FREQ / hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;  /* min ~18.2 Hz */
    if (divisor < 1)      divisor = 1;

    s_hz = PIT_BASE_FREQ / divisor;

    outb(PIT_CMD, PIT_CMD_BYTE);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register(0, pit_irq_handler);
}

uint64_t pit_ticks(void)     { return s_ticks; }
uint32_t pit_frequency(void) { return s_hz; }
