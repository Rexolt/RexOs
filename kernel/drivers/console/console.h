/* Rex OS - Framebuffer text console (cursor + scrolling on top of fb) */
#pragma once
#include <rexos/types.h>

void console_init(void);                 /* fb_init() utáni állapotot vár */
void console_putc(char c);
void console_puts(const char *s);
void console_clear(void);

void console_set_colors(uint32_t fg, uint32_t bg);
bool console_is_ready(void);
