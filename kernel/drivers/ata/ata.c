/* Rex OS - Egyszerű ATA PIO (LBA28) blokk eszköz driver.
 *
 * Implementáció minimumig: csak Primary IDE Master, polling mód (nincs IRQ).
 * Cél: QEMU `-drive file=disk.img,format=raw,if=ide` lemezéről FAT32 image
 * sectoronkénti olvasása. Nem fontos a sebesség.
 */

#include <drivers/ata/ata.h>
#include <drivers/block/block.h>
#include <rexos/io.h>
#include <lib/printf.h>
#include <lib/string.h>

#define ATA_PRIMARY_DATA       0x1F0
#define ATA_PRIMARY_ERROR      0x1F1
#define ATA_PRIMARY_SECCOUNT   0x1F2
#define ATA_PRIMARY_LBA_LO     0x1F3
#define ATA_PRIMARY_LBA_MID    0x1F4
#define ATA_PRIMARY_LBA_HI     0x1F5
#define ATA_PRIMARY_DRIVE      0x1F6
#define ATA_PRIMARY_STATUS     0x1F7
#define ATA_PRIMARY_COMMAND    0x1F7
#define ATA_PRIMARY_CTRL       0x3F6

#define ATA_SR_BSY     0x80
#define ATA_SR_DRDY    0x40
#define ATA_SR_DF      0x20
#define ATA_SR_DRQ     0x08
#define ATA_SR_ERR     0x01

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY    0xEC

static bool s_present = false;
static uint64_t s_sectors = 0;

/* Forward */
static int ata_block_read (block_device_t *dev, uint64_t lba, uint32_t count, void *buf);
static int ata_block_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf);

static void ata_io_delay(void) {
    /* 400 ns "alternate status" olvasás 4x — IDE szabványos várakozás */
    for (int i = 0; i < 4; i++) (void)inb(ATA_PRIMARY_CTRL);
}

static int ata_poll(void) {
    ata_io_delay();
    for (int tries = 0; tries < 1000000; tries++) {
        uint8_t s = inb(ATA_PRIMARY_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DF)  return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return 0;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRDY) && !(s & ATA_SR_DRQ)) return 1;
    }
    return -1;
}

bool ata_init(void) {
    /* Float bus check */
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0xFF) {
        kprintf("[ata] No IDE controller detected (floating bus)\n");
        s_present = false;
        return false;
    }

    /* IDENTIFY DEVICE Primary Masteren */
    outb(ATA_PRIMARY_DRIVE,    0xA0);  /* master */
    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA_LO,   0);
    outb(ATA_PRIMARY_LBA_MID,  0);
    outb(ATA_PRIMARY_LBA_HI,   0);
    outb(ATA_PRIMARY_COMMAND,  ATA_CMD_IDENTIFY);
    ata_io_delay();

    status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        kprintf("[ata] No drive present on primary master.\n");
        return false;
    }

    /* BSY-re várunk, majd LBA mid/hi == 0 ellenőrzés */
    while (inb(ATA_PRIMARY_STATUS) & ATA_SR_BSY) { /* spin */ }
    uint8_t mid = inb(ATA_PRIMARY_LBA_MID);
    uint8_t hi  = inb(ATA_PRIMARY_LBA_HI);
    if (mid != 0 || hi != 0) {
        /* nem sima ATA (lehet ATAPI), nem támogatjuk */
        kprintf("[ata] Drive is not ATA (mid=0x%x hi=0x%x)\n", mid, hi);
        return false;
    }

    /* DRQ-ra várunk */
    int got_drq = 0;
    for (int tries = 0; tries < 1000000; tries++) {
        uint8_t s = inb(ATA_PRIMARY_STATUS);
        if (s & ATA_SR_ERR) { kprintf("[ata] IDENTIFY error\n"); return false; }
        if (s & ATA_SR_DRQ) { got_drq = 1; break; }
    }
    if (!got_drq) { kprintf("[ata] IDENTIFY: no DRQ\n"); return false; }

    /* 256 word olvasás. A 60..61 word az LBA28 szektorszám. */
    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_PRIMARY_DATA);

    uint32_t lba28 = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    s_sectors = lba28;
    s_present = true;

    /* Modell-szöveg kiírás (10..19 word, byte-swap) */
    char model[41];
    for (int i = 0; i < 20; i++) {
        model[i*2]     = (char)(id[27 + i] >> 8);
        model[i*2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    model[40] = 0;
    /* Trim trailing spaces */
    for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;

    kprintf("[ata] Drive: %s, sectors=%lu (%lu MB)\n",
            model, s_sectors, (s_sectors * 512) / (1024 * 1024));

    /* Block device regisztráció */
    block_device_t bd;
    for (int i = 0; i < 32; i++) bd.name[i] = 0;
    bd.name[0] = 'a'; bd.name[1] = 't'; bd.name[2] = 'a'; bd.name[3] = '0';
    bd.sector_size = 512;
    bd.sector_count = s_sectors;
    bd.read  = ata_block_read;
    bd.write = ata_block_write;
    bd.driver_data = NULL;
    block_register(&bd);

    return true;
}

/* Block device read callback: a meglévő ata_read_sectors-ra hív át,
 * 256-os szelekciónkbontva (LBA28 max). */
static int ata_block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf) {
    (void)dev;
    uint8_t *p = (uint8_t *)buf;
    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > 255) chunk = 255;  /* LBA28 + 8-bit seccount korlát */
        uint32_t r = ata_read_sectors((uint32_t)(lba + done),
                                      (uint8_t)chunk,
                                      p + done * 512);
        if (r == 0) return (int)done;
        done += r;
    }
    return (int)done;
}

uint32_t ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer) {
    if (!s_present) return 0;
    if (count == 0) return 0;

    /* BSY-re várunk */
    while (inb(ATA_PRIMARY_STATUS) & ATA_SR_BSY) { /* spin */ }

    outb(ATA_PRIMARY_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO,   (uint8_t)(lba));
    outb(ATA_PRIMARY_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI,   (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND,  ATA_CMD_WRITE_PIO);
    ata_io_delay();

    const uint16_t *buf = (const uint16_t *)buffer;
    uint32_t actual = 0;

    for (uint8_t s = 0; s < count; s++) {
        /* DRQ-ra várunk mielőtt az adatot kiírjuk */
        if (ata_poll() < 0) {
            kprintf("[ata] write error at LBA %u (sector %u)\n", lba, s);
            return actual;
        }
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_DATA, buf[(uint32_t)s * 256 + i]);
        }
        ata_io_delay();
        actual++;
    }

    /* Cache flush: biztosítéjuk, hogy a drive a pufferét kiírta */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    while (inb(ATA_PRIMARY_STATUS) & ATA_SR_BSY) { /* spin */ }

    return actual;
}

/* Block device write callback: a meglévő ata_write_sectors-ra hív át. */
static int ata_block_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    (void)dev;
    uint8_t *p = (uint8_t *)buf;
    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > 255) chunk = 255;
        uint32_t r = ata_write_sectors((uint32_t)(lba + done),
                                       (uint8_t)chunk,
                                       p + done * 512);
        if (r == 0) return (int)done;
        done += r;
    }
    return (int)done;
}

uint64_t ata_sector_count(void) { return s_sectors; }

uint32_t ata_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    if (!s_present) return 0;
    if (count == 0) return 0;

    /* BSY-re várunk */
    while (inb(ATA_PRIMARY_STATUS) & ATA_SR_BSY) { /* spin */ }

    outb(ATA_PRIMARY_DRIVE,    0xE0 | ((lba >> 24) & 0x0F)); /* master + LBA */
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO,   (uint8_t)(lba));
    outb(ATA_PRIMARY_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI,   (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND,  ATA_CMD_READ_PIO);

    uint16_t *buf = (uint16_t *)buffer;
    uint32_t actual = 0;

    for (uint16_t s = 0; s < (count == 0 ? 256 : count); s++) {
        if (ata_poll() < 0) {
            kprintf("[ata] read error at LBA %u (s=%u)\n", lba + s, s);
            return actual;
        }
        for (int i = 0; i < 256; i++) {
            buf[s * 256 + i] = inw(ATA_PRIMARY_DATA);
        }
        actual++;
    }
    return actual;
}
