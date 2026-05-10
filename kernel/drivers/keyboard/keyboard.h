/* Rex OS - PS/2 keyboard driver (IRQ1) */
#pragma once
#include <rexos/types.h>

void     keyboard_init(void);
bool     keyboard_has_data(void);
char     keyboard_getc(void);     /* ring-bufferből; 0 ha üres */
