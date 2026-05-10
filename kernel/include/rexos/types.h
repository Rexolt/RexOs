/* Rex OS - Common kernel types and macros */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#define KERNEL_NAME    "Rex OS"
#define KERNEL_VERSION "0.1.0-alpha"

#define UNUSED(x)        ((void)(x))
#define ARRAY_SIZE(a)    (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

#define __packed         __attribute__((packed))
#define __aligned(n)     __attribute__((aligned(n)))
#define __noreturn       __attribute__((noreturn))
#define __unused         __attribute__((unused))
