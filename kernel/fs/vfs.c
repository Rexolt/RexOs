#include <rexos/fs.h>
#include <rexos/types.h>
#include <lib/string.h>
#include <lib/printf.h>

/* A gyökér csomópont (root directory) */
vfs_node_t *fs_root = NULL;

/* --- Egyszerű mount-tábla ---------------------------------------------- */

#define VFS_MAX_MOUNTS 4

typedef struct {
    char name[64];          /* mountpoint név fs_root alatt (pl. "mnt") */
    vfs_node_t *node;       /* a felmountolt fájlrendszer gyökere */
    bool used;
} vfs_mount_t;

static vfs_mount_t s_mounts[VFS_MAX_MOUNTS];

/* Lazy cache: hány natív bejegyzés van fs_root->readdir-ban?
 * (uint32_t)-1 = még nem számoltuk meg. Egyszer számítjuk ki,
 * mert a tarfs bejegyzések statikusak (read-only initrd). */
static uint32_t s_root_base_count = (uint32_t)-1;

void vfs_init(void) {
    fs_root = NULL;
    s_root_base_count = (uint32_t)-1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) s_mounts[i].used = false;
}

uint64_t vfs_read(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (node != NULL && node->read != NULL) {
        return node->read(node, offset, size, buffer);
    }
    return 0;
}

uint64_t vfs_write(vfs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (node != NULL && node->write != NULL) {
        return node->write(node, offset, size, buffer);
    }
    return 0;
}

void vfs_open(vfs_node_t *node) {
    if (node != NULL && node->open != NULL) {
        node->open(node);
    }
}

void vfs_close(vfs_node_t *node) {
    if (node != NULL && node->close != NULL) {
        node->close(node);
    }
}

static dirent_t s_mount_dirent;

dirent_t *vfs_readdir(vfs_node_t *node, uint32_t index) {
    if (!node || (node->flags & 0x07) != FS_DIRECTORY) return NULL;

    /* fs_root esetén előbb az alaplistát adjuk vissza, utána a mountokat.
     * s_root_base_count cache-eli az alap-bejegyzések számát, így
     * elkerüljük az O(n²) ismételt megszámlálást (lazy, egyszer számítjuk). */
    if (node == fs_root) {
        if (s_root_base_count == (uint32_t)-1) {
            uint32_t bc = 0;
            if (node->readdir) {
                while (node->readdir(node, bc) != NULL) bc++;
            }
            s_root_base_count = bc;
        }

        if (index < s_root_base_count && node->readdir) {
            return node->readdir(node, index);
        }

        uint32_t mi = index - s_root_base_count;
        uint32_t seen = 0;
        for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
            if (s_mounts[i].used) {
                if (seen == mi) {
                    kstrncpy(s_mount_dirent.name, s_mounts[i].name, 127);
                    s_mount_dirent.ino = 1000 + i;
                    return &s_mount_dirent;
                }
                seen++;
            }
        }
        return NULL;
    }

    if (node->readdir != NULL) {
        return node->readdir(node, index);
    }
    return NULL;
}

vfs_node_t *vfs_finddir(vfs_node_t *node, const char *name) {
    if (!node || (node->flags & 0x07) != FS_DIRECTORY) return NULL;

    /* Először a mount-pontokat ellenőrizzük (csak ha fs_root-ban keresünk) */
    if (node == fs_root) {
        for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
            if (s_mounts[i].used && kstrcmp(s_mounts[i].name, name) == 0) {
                return s_mounts[i].node;
            }
        }
    }

    if (node->finddir != NULL) {
        return node->finddir(node, name);
    }
    return NULL;
}

bool vfs_mount(const char *mount_path, vfs_node_t *node) {
    if (!mount_path || !node) return false;
    /* A path elejéről '/'-t levesszük */
    while (*mount_path == '/') mount_path++;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!s_mounts[i].used) {
            size_t j = 0;
            while (mount_path[j] && j < 63) {
                s_mounts[i].name[j] = mount_path[j];
                j++;
            }
            s_mounts[i].name[j] = 0;
            s_mounts[i].node = node;
            s_mounts[i].used = true;
            kprintf("[vfs] mounted '%s' at /%s\n", node->name, s_mounts[i].name);
            return true;
        }
    }
    return false;
}

/* Egyszerű, beágyazott útvonal feloldó.
 * Komponensekre bontja a path-t és `vfs_finddir` segítségével navigál.
 */
vfs_node_t *vfs_lookup(const char *path) {
    if (!path || !fs_root) return NULL;

    /* Üres / "/" -> fs_root */
    if (path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
        return fs_root;
    }

    /* A vezető '/' levágása */
    if (*path == '/') path++;

    vfs_node_t *current = fs_root;
    char component[128];

    while (*path) {
        size_t i = 0;
        while (*path && *path != '/' && i < 127) {
            component[i++] = *path++;
        }
        component[i] = 0;

        /* Üres komponens (pl. dupla `//`) átugorható */
        if (i == 0) {
            while (*path == '/') path++;
            continue;
        }

        vfs_node_t *next = vfs_finddir(current, component);
        if (!next) return NULL;
        current = next;

        /* Skip subsequent '/' karaktereket */
        while (*path == '/') path++;
    }

    return current;
}

vfs_node_t *vfs_create(vfs_node_t *dir, const char *name, uint32_t flags)
{
    if (!dir || (dir->flags & 0x07) != FS_DIRECTORY) return NULL;
    if (!dir->create) return NULL;
    return dir->create(dir, name, flags);
}

int vfs_mkdir_node(vfs_node_t *dir, const char *name)
{
    if (!dir || (dir->flags & 0x07) != FS_DIRECTORY) return -1;
    if (!dir->mkdir) return -1;
    return dir->mkdir(dir, name);
}

int vfs_unlink(vfs_node_t *dir, const char *name)
{
    if (!dir || (dir->flags & 0x07) != FS_DIRECTORY) return -1;
    if (!dir->unlink) return -1;
    return dir->unlink(dir, name);
}
