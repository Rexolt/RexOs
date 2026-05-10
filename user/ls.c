#include "libc.h"

void _start() {
    int fd = open("/");
    if (fd < 0) {
        print("ls: failed to open directory\n");
        exit(1);
    }
    
    dirent_t dir;
    while (getdents(fd, &dir) > 0) {
        print("  ");
        print(dir.name);
        print("\n");
    }
    
    exit(0);
}
