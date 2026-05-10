#include "libc.h"

void _start(void) {
    print("Hello from User Mode! Syscall mechanism is working!\n");
    exit(42);
}
