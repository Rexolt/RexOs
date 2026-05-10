#include <rexos/fs.h>
#include <rexos/types.h>

/* A gyökér csomópont (root directory) */
vfs_node_t *fs_root = NULL;

void vfs_init(void) {
    /* Kezdetben nincs gyökér fájlrendszer.
     * Ezt később a TARFS (Initrd) fogja beállítani. */
    fs_root = NULL;
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

dirent_t *vfs_readdir(vfs_node_t *node, uint32_t index) {
    /* Csak mappák (directory) esetén működhet */
    if (node != NULL && (node->flags & 0x07) == FS_DIRECTORY && node->readdir != NULL) {
        return node->readdir(node, index);
    }
    return NULL;
}

vfs_node_t *vfs_finddir(vfs_node_t *node, const char *name) {
    /* Csak mappák (directory) esetén működhet */
    if (node != NULL && (node->flags & 0x07) == FS_DIRECTORY && node->finddir != NULL) {
        return node->finddir(node, name);
    }
    return NULL;
}
