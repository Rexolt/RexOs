/* ============================================================================
 *  Rex OS - kprintf implementation
 *
 *  Támogatott formátum: %[0][width][l|ll]<conv>
 *    conv ∈ { d, i, u, x, X, p, s, c, % }
 *  Példák:  %lu  %016lx  %5d  %p
 * ========================================================================== */

#include <lib/printf.h>
#include <drivers/serial/serial.h>
#include <drivers/console/console.h>

static void out_char(char c)
{
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    if (console_is_ready()) console_putc(c);
}

static void out_str(const char *s)
{
    while (*s) out_char(*s++);
}

static void emit_padded(const char *digits, int len, int width, bool zero_pad,
                        const char *prefix)
{
    int plen = 0;
    while (prefix && prefix[plen]) plen++;

    int total = len + plen;
    int pad   = width - total;

    if (zero_pad && prefix) {
        for (int i = 0; i < plen; i++) out_char(prefix[i]);
        while (pad-- > 0) out_char('0');
    } else {
        while (pad-- > 0) out_char(zero_pad ? '0' : ' ');
        if (prefix) for (int i = 0; i < plen; i++) out_char(prefix[i]);
    }

    for (int i = len - 1; i >= 0; i--) out_char(digits[i]);
}

static void print_uint(uint64_t val, unsigned base, bool uppercase,
                       int width, bool zero_pad, const char *prefix)
{
    static const char hex_lo[] = "0123456789abcdef";
    static const char hex_hi[] = "0123456789ABCDEF";
    const char *digits = uppercase ? hex_hi : hex_lo;

    char buf[32];
    int  len = 0;

    if (val == 0) {
        buf[len++] = '0';
    } else {
        while (val > 0) {
            buf[len++] = digits[val % base];
            val /= base;
        }
    }
    emit_padded(buf, len, width, zero_pad, prefix);
}

static void print_int(int64_t val, int width, bool zero_pad)
{
    if (val < 0) {
        print_uint((uint64_t)(-val), 10, false, width ? width - 1 : 0, zero_pad, "-");
    } else {
        print_uint((uint64_t)val, 10, false, width, zero_pad, NULL);
    }
}

void kvprintf(const char *fmt, va_list ap)
{
    while (*fmt) {
        if (*fmt != '%') {
            out_char(*fmt++);
            continue;
        }
        fmt++;

        bool zero_pad = false;
        int  width    = 0;

        if (*fmt == '0') { zero_pad = true; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        bool is_long = false, is_longlong = false;
        if (*fmt == 'l') {
            is_long = true; fmt++;
            if (*fmt == 'l') { is_longlong = true; fmt++; }
        }
        /* 'z' (size_t) - kezelése: ugyanúgy mint long x86_64-en */
        if (*fmt == 'z') { is_long = true; fmt++; }

        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t v;
            if (is_longlong)  v = va_arg(ap, long long);
            else if (is_long) v = va_arg(ap, long);
            else              v = va_arg(ap, int);
            print_int(v, width, zero_pad);
            break;
        }
        case 'u': {
            uint64_t v;
            if (is_longlong)  v = va_arg(ap, unsigned long long);
            else if (is_long) v = va_arg(ap, unsigned long);
            else              v = va_arg(ap, unsigned);
            print_uint(v, 10, false, width, zero_pad, NULL);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v;
            if (is_longlong)  v = va_arg(ap, unsigned long long);
            else if (is_long) v = va_arg(ap, unsigned long);
            else              v = va_arg(ap, unsigned);
            print_uint(v, 16, *fmt == 'X', width, zero_pad, NULL);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            print_uint((uint64_t)v, 16, false, width ? width : 16, true, "0x");
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            out_str(s ? s : "(null)");
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            out_char(c);
            break;
        }
        case '%':
            out_char('%');
            break;
        default:
            out_char('%');
            out_char(*fmt);
            break;
        }
        fmt++;
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
