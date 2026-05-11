/* Rex OS - Minimal FAT32 read-only driver
 *
 * Az ATA blokk eszközről olvas, és kibontja a struktúrát egy VFS node-fába.
 * Mount: vfs_mount("/mnt", fat32_init()).
 */
#pragma once

#include <rexos/fs.h>

/* Inicializálja a FAT32-t. Visszaad egy VFS node-ot a gyökérre,
 * vagy NULL-t ha nincs lemez / hibás a partíció. */
vfs_node_t *fat32_init(void);

/* Fájl/könyvtár létrehozása a FAT32 partíción.
 * A dir paraméter egy FAT32 könyvtár VFS node-ja.
 * Visszatér: az új VFS node-dal, vagy NULL-lal hiba esetén. */
vfs_node_t *fat32_create_file(vfs_node_t *dir, const char *name);
int         fat32_mkdir(vfs_node_t *dir, const char *name);
int         fat32_unlink(vfs_node_t *dir, const char *name);
