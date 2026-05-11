#include "libc.h"

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void _start(void) {
    print("\n----------------------------------\n");
    print("  Welcome to the User Mode Shell! \n");
    print("----------------------------------\n");
    
    char line[128];
    int len = 0;
    
    print("user> ");
    while (1) {
        char c;
        read(0, &c, 1);
        
        if (c == '\n') {
            print("\n");
            line[len] = 0;
            
            if (len > 0) {
                if (strcmp(line, "exit") == 0) {
                    print("Goodbye!\n");
                    exit(0);
                } else if (strcmp(line, "help") == 0) {
                    print("Built-in commands:\n");
                    print("  help        - Show this help\n");
                    print("  exit        - Exit the shell\n");
                    print("  mkdir <dir> - Create a directory\n");
                    print("  rm <file>   - Delete a file\n");
                    print("  ls [path]   - List files (default: /)\n");
                    print("  cat <file>  - Show file contents (supports /mnt/...)\n");
                    print("  clear       - Clear the screen (ANSI)\n");
                    print("  desktop     - Launch the graphical desktop\n");
                    print("You can also run any .elf file directly.\n");
                } else if (strcmp(line, "clear") == 0) {
                    /* ANSI clear screen */
                    print("\033[2J\033[H");
                } else if (line[0] == 'l' && line[1] == 's' && (line[2] == 0 || line[2] == ' ')) {
                    const char *path = (line[2] == ' ') ? &line[3] : "/";
                    while (*path == ' ') path++;
                    if (*path == 0) path = "/";
                    int fd = open(path);
                    if (fd >= 0) {
                        dirent_t dir;
                        while (getdents(fd, &dir) > 0) {
                            print("  ");
                            print(dir.name);
                            print("\n");
                        }
                    } else {
                        print("ls: cannot open '");
                        print(path);
                        print("'\n");
                    }
                } else if (line[0] == 'c' && line[1] == 'a' && line[2] == 't' && line[3] == ' ') {
                    /* Beépített cat: cat <fájlnév> */
                    const char *fname = &line[4];
                    int fd = open(fname);
                    if (fd >= 0) {
                        char buf[256];
                        int r;
                        while ((r = read(fd, buf, 256)) > 0) {
                            write(1, buf, r);
                        }
                        print("\n");
                    } else {
                        print("cat: file not found: ");
                        print(fname);
                        print("\n");
                    }
                } else if (line[0]=='m'&&line[1]=='k'&&line[2]=='d'&&line[3]=='i'&&line[4]=='r'&&line[5]==' ') {
                    const char *dname = &line[6];
                    while (*dname == ' ') dname++;
                    if (*dname) {
                        if (mkdir(dname) == 0) { print("Created: "); print(dname); print("\n"); }
                        else { print("mkdir: failed\n"); }
                    } else { print("Usage: mkdir <name>\n"); }
                } else if (line[0]=='r'&&line[1]=='m'&&line[2]==' ') {
                    const char *fname = &line[3];
                    while (*fname == ' ') fname++;
                    if (*fname) {
                        if (unlink(fname) == 0) { print("Removed: "); print(fname); print("\n"); }
                        else { print("rm: failed\n"); }
                    } else { print("Usage: rm <file>\n"); }
                } else {
                    /* Próbáljuk spawnolni programként, ha nem megy, .elf-fel is */
                    int pid = spawn(line);
                    if (pid < 0) {
                        /* Automatikus .elf hozzáfűzés */
                        char elf_name[140];
                        int i = 0;
                        while (line[i] && i < 128) { elf_name[i] = line[i]; i++; }
                        elf_name[i++] = '.'; elf_name[i++] = 'e';
                        elf_name[i++] = 'l'; elf_name[i++] = 'f';
                        elf_name[i] = 0;
                        pid = spawn(elf_name);
                    }
                    if (pid >= 0) {
                        waitpid(pid);
                    } else {
                        print("Command not found: ");
                        print(line);
                        print("\n");
                    }
                }
            }
            
            len = 0;
            print("user> ");
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                print_char('\b');
            }
        } else if (c >= 32 && c < 127 && len < 127) {
            line[len++] = c;
            print_char(c);
        }
    }
}
