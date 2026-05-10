#pragma once

#include <rexos/types.h>

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x03
#define FS_BLOCKDEVICE 0x04
#define FS_PIPE        0x05
#define FS_SYMLINK     0x06
#define FS_MOUNTPOINT  0x08 // Egyedi flag, ha egy mappa alá van mountolva valami

struct vfs_node;

/* Egy mappa bejegyzése (Directory Entry) */
typedef struct dirent {
    char name[128]; // Fájl vagy mappa neve
    uint32_t ino;   // Inode szám
} dirent_t;

/* Függvény pointer típusok a VFS absztrakcióhoz */
typedef uint64_t (*read_type_t)(struct vfs_node *node, uint64_t offset, uint64_t size, uint8_t *buffer);
typedef uint64_t (*write_type_t)(struct vfs_node *node, uint64_t offset, uint64_t size, uint8_t *buffer);
typedef void (*open_type_t)(struct vfs_node *node);
typedef void (*close_type_t)(struct vfs_node *node);
typedef struct dirent *(*readdir_type_t)(struct vfs_node *node, uint32_t index);
typedef struct vfs_node *(*finddir_type_t)(struct vfs_node *node, const char *name);

/* A VFS Csomópont (Node) */
typedef struct vfs_node {
    char name[128];         // A csomópont neve
    uint32_t mask;          // Jogosultságok (rwxrwxrwx)
    uint32_t uid;           // User ID
    uint32_t gid;           // Group ID
    uint32_t flags;         // Típus és flag-ek (pl. FS_FILE, FS_DIRECTORY)
    uint32_t inode;         // Inode szám, ami azonosítja a fájlt
    uint64_t length;        // Fájl mérete byte-okban
    uint32_t impl;          // Implementáció-specifikus adat (pl. a fájlrendszer driver használhatja)

    /* Fájlrendszer műveletek */
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;

    struct vfs_node *ptr;   // Mutató egyéb adatokra (pl. szimbolikus linkeknél, mountoknál vagy TAR file címe)
} vfs_node_t;

extern vfs_node_t *fs_root;

void vfs_init(void);

uint64_t vfs_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
uint64_t vfs_write(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
void vfs_open(vfs_node_t *node);
void vfs_close(vfs_node_t *node);
dirent_t *vfs_readdir(vfs_node_t *node, uint32_t index);
vfs_node_t *vfs_finddir(vfs_node_t *node, const char *name);
