/* ============================================================================
 *  Rex OS - 16550 UART serial driver (COM1 @ 0x3F8)
 *
 *  Regiszter-térkép (offset COM1_PORT-tól):
 *    0  RBR (read)   / THR (write)  / DLL (when DLAB=1)
 *    1  IER          / DLH (when DLAB=1)
 *    2  IIR (read)   / FCR (write)
 *    3  LCR
 *    4  MCR
 *    5  LSR
 *    6  MSR
 *    7  Scratch
 * ========================================================================== */

#include <drivers/serial/serial.h>
#include <rexos/io.h>

#define UART_RBR 0
#define UART_THR 0
#define UART_DLL 0
#define UART_IER 1
#define UART_DLH 1
#define UART_IIR 2
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5

#define UART_LSR_THRE 0x20  /* Transmitter Holding Register Empty */

static bool s_initialized = false;

bool serial_init(void)
{
    outb(COM1_PORT + UART_IER, 0x00);  /* megszakítások tiltása */
    outb(COM1_PORT + UART_LCR, 0x80);  /* DLAB = 1: baud rate beállítás */
    outb(COM1_PORT + UART_DLL, 0x01);  /* divisor low  = 1  -> 115200 baud */
    outb(COM1_PORT + UART_DLH, 0x00);  /* divisor high = 0                  */
    outb(COM1_PORT + UART_LCR, 0x03);  /* 8N1 + DLAB=0                      */
    outb(COM1_PORT + UART_FCR, 0xC7);  /* FIFO enable + clear + 14B trigger */
    outb(COM1_PORT + UART_MCR, 0x0B);  /* DTR + RTS + OUT2                  */

    /* --- Loopback önteszt --- */
    outb(COM1_PORT + UART_MCR, 0x1E);  /* loopback mode */
    outb(COM1_PORT + UART_THR, 0xAE);  /* küldött test bájt */
    if (inb(COM1_PORT + UART_RBR) != 0xAE) {
        return false;
    }

    outb(COM1_PORT + UART_MCR, 0x0F);  /* normál üzem: DTR+RTS+OUT1+OUT2 */
    s_initialized = true;
    return true;
}

static inline bool tx_empty(void)
{
    return (inb(COM1_PORT + UART_LSR) & UART_LSR_THRE) != 0;
}

void serial_putc(char c)
{
    if (!s_initialized) return;
    while (!tx_empty()) {
        __asm__ volatile ("pause");
    }
    outb(COM1_PORT + UART_THR, (uint8_t)c);
}

void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_write(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n') serial_putc('\r');
        serial_putc(buf[i]);
    }
}
