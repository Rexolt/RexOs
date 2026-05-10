/* Rex OS - kernel panic */
#pragma once
#include <rexos/types.h>

__noreturn void kpanic(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
