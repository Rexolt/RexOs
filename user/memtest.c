#include "libc.h"

/* Mini itoa a számok kiíratásához */
static void print_num(uint64_t n) {
    char buf[20];
    int i = 0;
    if (n == 0) { print("0"); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (--i >= 0) print_char(buf[i]);
}

void _start() {
    print("=== malloc/free test ===\n");
    
    /* 1. Egyszerű allokáció */
    char *s = (char *)malloc(64);
    if (!s) { print("FAIL: malloc returned NULL\n"); exit(1); }
    
    /* Írjunk bele */
    const char *msg = "Hello from malloc!";
    int i = 0;
    while (msg[i]) { s[i] = msg[i]; i++; }
    s[i] = 0;
    
    print("  Allocated string: ");
    print(s);
    print("\n");
    
    /* 2. Több allokáció */
    uint64_t *nums = (uint64_t *)malloc(10 * sizeof(uint64_t));
    if (!nums) { print("FAIL: second malloc failed\n"); exit(1); }
    
    for (i = 0; i < 10; i++) nums[i] = i * i;
    
    print("  Squares: ");
    for (i = 0; i < 10; i++) {
        print_num(nums[i]);
        print(" ");
    }
    print("\n");
    
    /* 3. Free és re-allokáció */
    free(s);
    
    char *s2 = (char *)malloc(32);
    if (!s2) { print("FAIL: re-alloc failed\n"); exit(1); }
    s2[0] = 'O'; s2[1] = 'K'; s2[2] = 0;
    print("  Re-allocated after free: ");
    print(s2);
    print("\n");
    
    free(s2);
    free(nums);
    
    print("=== ALL TESTS PASSED ===\n");
    exit(0);
}
