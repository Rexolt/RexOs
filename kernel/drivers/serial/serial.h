/* Rex OS - 16550 UART serial driver (COM1) */
#pragma once
#include <rexos/types.h>

#define COM1_PORT 0x3F8

/* @return true, ha a UART loopback-teszt sikeres volt. */
bool serial_init(void);

void serial_putc(char c);
void serial_puts(const char *s);
void serial_write(const char *buf, size_t len);
