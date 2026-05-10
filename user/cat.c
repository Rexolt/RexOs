#include "libc.h"

void _start() {
    print("File to read: ");
    char path[128];
    int len = 0;
    while (1) {
        char c;
        read(0, &c, 1);
        if (c == '\n') {
            print("\n");
            path[len] = 0;
            break;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                print_char('\b');
            }
        } else if (c >= 32 && c < 127 && len < 127) {
            path[len++] = c;
            print_char(c);
        }
    }
    
    if (len == 0) exit(0);
    
    int fd = open(path);
    if (fd < 0) {
        print("cat: failed to open file\n");
        exit(1);
    }
    
    char buf[256];
    int r;
    while ((r = read(fd, buf, 256)) > 0) {
        write(1, buf, r);
    }
    print("\n");
    
    exit(0);
}
