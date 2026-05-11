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
