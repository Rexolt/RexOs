/* Rex OS - 8253/8254 PIT (Programmable Interval Timer) */
#pragma once
#include <rexos/types.h>

void     pit_init(uint32_t hz);   /* IRQ0 frekvencia beállítása */
uint64_t pit_ticks(void);          /* tickok száma az init óta */
uint32_t pit_frequency(void);      /* a beállított Hz */
