/* Rex OS - Simple ATA PIO (LBA28) block driver
 *
 * Csak az IDE primary master csatornát kezeljük. QEMU `-drive ... ,if=ide`
 * mellé tökéletes egy egyszerű FAT32 image olvasásához.
 */
#pragma once

#include <rexos/types.h>

#define ATA_SECTOR_SIZE 512

/* Inicializálás: detektálja, hogy van-e elérhető IDE master.
 * Visszatérési érték: true, ha sikerült azonosítani egy lemezt. */
bool ata_init(void);

/* Méret szektorokban. 0, ha nincs lemez. */
uint64_t ata_sector_count(void);

/* LBA28 olvasás: max 256 szektor egy híváson belül.
 * Visszaadott érték: ténylegesen beolvasott szektorok száma. */
uint32_t ata_read_sectors(uint32_t lba, uint8_t count, void *buffer);

/* LBA28 PIO írás. Visszaadott érték: ténylegesen kiírt szektorok száma. */
uint32_t ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer);
