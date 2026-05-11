/* Rex OS - Generic block device interface.
 *
 * Egységes absztrakció az ATA PIO, AHCI SATA, USB MSC stb. driverekhez.
 * Ugyanazt az interfészt használja a felsőbb fájlrendszer kód (FAT32, ext2...).
 */
#pragma once
#include <rexos/types.h>

typedef struct block_device block_device_t;

typedef int (*block_read_fn) (block_device_t *dev, uint64_t lba, uint32_t count, void *buf);
typedef int (*block_write_fn)(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf);

struct block_device {
    char           name[32];     /* pl. "ata0", "ahci0:1", "usb0" */
    uint32_t       sector_size;  /* általában 512 */
    uint64_t       sector_count; /* lemez teljes szektorszáma */
    block_read_fn  read;
    block_write_fn write;        /* NULL ha read-only */
    void          *driver_data;
};

/* Eszköz regisztrálása. Másolatot készít a struktúráról.
 * Visszaad egy stabil pointert, amit a driver tovább használhat. */
block_device_t *block_register(const block_device_t *dev);

/* Lekérdezés. */
size_t          block_count(void);
block_device_t *block_at(size_t i);
block_device_t *block_get_first(void);
block_device_t *block_find(const char *name);
