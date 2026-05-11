/* Rex OS - AHCI SATA driver
 *
 * Tudás:
 *  - PCI scan-nel megtalálja a SATA AHCI controllert.
 *  - HBA reset, AHCI mode enable.
 *  - Az első detektált SATA port-ot inicializálja.
 *  - IDENTIFY DEVICE -> szektorszám.
 *  - READ DMA EXT (LBA48) parancs polling módban.
 *  - Block device-ként regisztrál.
 *
 * Korlátozás:
 *  - Egy port támogatott (az első talált device-os).
 *  - Csak olvasás. Írás később.
 *  - Polling, nincs IRQ.
 *  - Egy parancs egyszerre, nincs NCQ.
 */

#include <drivers/ahci/ahci.h>
#include <drivers/pci/pci.h>
#include <drivers/block/block.h>
#include <mm/pmm.h>
#include <rexos/io.h>
#include <lib/printf.h>
#include <lib/string.h>

/* --- HBA register offsets (Generic Host Control) ------------------------- */

#define HBA_CAP     0x00
#define HBA_GHC     0x04
#define HBA_IS      0x08
#define HBA_PI      0x0C
#define HBA_VS      0x10

#define GHC_HR      (1u << 0)   /* HBA Reset */
#define GHC_IE      (1u << 1)   /* Interrupt Enable */
#define GHC_AE      (1u << 31)  /* AHCI Enable */

/* Port register offsets (port base = ABAR + 0x100 + port*0x80) */
#define PORT_CLB    0x00
#define PORT_CLBU   0x04
#define PORT_FB     0x08
#define PORT_FBU    0x0C
#define PORT_IS     0x10
#define PORT_IE     0x14
#define PORT_CMD    0x18
#define PORT_TFD    0x20
#define PORT_SIG    0x24
#define PORT_SSTS   0x28
#define PORT_SCTL   0x2C
#define PORT_SERR   0x30
#define PORT_SACT   0x34
#define PORT_CI     0x38

#define CMD_ST   (1u << 0)
#define CMD_SUD  (1u << 1)
#define CMD_POD  (1u << 2)
#define CMD_FRE  (1u << 4)
#define CMD_FR   (1u << 14)
#define CMD_CR   (1u << 15)

#define TFD_BSY  (1u << 7)
#define TFD_DRQ  (1u << 3)
#define TFD_ERR  (1u << 0)

#define SIG_SATA   0x00000101  /* SATA drive */

/* SATA SSTS DET (device detection) */
#define SSTS_DET_PRESENT 0x3

/* --- FIS típusok --------------------------------------------------------- */
#define FIS_TYPE_REG_H2D    0x27
#define FIS_TYPE_REG_D2H    0x34

/* --- ATA parancsok ------------------------------------------------------- */
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_READ_DMA_EX 0x25

/* --- AHCI strukturák ----------------------------------------------------- */
#pragma pack(push, 1)

typedef struct {
    /* DWORD 0 */
    uint8_t  cfl     : 5;   /* Command FIS Length in dwords (max 16) */
    uint8_t  a       : 1;   /* ATAPI */
    uint8_t  w       : 1;   /* Write (1 = H2D) */
    uint8_t  p       : 1;
    uint8_t  r       : 1;
    uint8_t  b       : 1;
    uint8_t  c       : 1;
    uint8_t  rsv0    : 1;
    uint8_t  pmp     : 4;   /* Port multiplier port */
    uint16_t prdtl;          /* Physical Region Descriptor Table Length */
    /* DWORD 1 */
    volatile uint32_t prdbc; /* PRD Byte Count transferred */
    /* DWORD 2,3 */
    uint32_t ctba;           /* Command Table Base Address */
    uint32_t ctbau;
    /* DWORD 4..7 */
    uint32_t rsv1[4];
} ahci_cmd_header_t;

typedef struct {
    uint32_t dba;            /* Data Base Address (phys) */
    uint32_t dbau;
    uint32_t rsv0;
    /* DWORD 3 */
    uint32_t dbc      : 22;  /* Byte count - 1 */
    uint32_t rsv1     : 9;
    uint32_t i        : 1;   /* Interrupt on completion */
} ahci_prdt_entry_t;

typedef struct {
    uint8_t  cfis[64];       /* Command FIS */
    uint8_t  acmd[16];       /* ATAPI command */
    uint8_t  rsv[48];
    ahci_prdt_entry_t prdt[1]; /* csak 1 PRDT entry kell nekünk */
} ahci_cmd_table_t;

typedef struct {
    uint8_t  fis_type;       /* 0x27 */
    uint8_t  pmport   : 4;
    uint8_t  rsv0     : 3;
    uint8_t  c        : 1;   /* 1 = command */
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0, lba1, lba2;
    uint8_t  device;
    uint8_t  lba3, lba4, lba5;
    uint8_t  featureh;
    uint16_t count;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv1[4];
} fis_reg_h2d_t;

#pragma pack(pop)

/* --- Globális állapot ---------------------------------------------------- */

static volatile uint8_t *s_abar = NULL;   /* HBA MMIO (HHDM virt) */
static int      s_port = -1;
static uint64_t s_sectors = 0;

/* DMA / parancs területek (1 lapon, HHDM-en elérve, fizikai cím lentebb) */
static uintptr_t       s_cmdlist_phys;
static ahci_cmd_header_t *s_cmdlist;     /* 32 header */
static uintptr_t       s_fis_phys;
static uint8_t        *s_fis;            /* 256 byte received FIS area */
static uintptr_t       s_cmdtable_phys;
static ahci_cmd_table_t *s_cmdtable;
static uintptr_t       s_scratch_phys;
static uint8_t        *s_scratch;        /* DMA buffer, 64 KB */

#define SCRATCH_PAGES 16  /* 64 KB */

/* --- MMIO accesorok ----------------------------------------------------- */

static inline uint32_t hba_read(uint32_t off) {
    return *(volatile uint32_t *)(s_abar + off);
}
static inline void hba_write(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(s_abar + off) = val;
}
static inline uint32_t port_read(int port, uint32_t off) {
    return *(volatile uint32_t *)(s_abar + 0x100 + port * 0x80 + off);
}
static inline void port_write(int port, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(s_abar + 0x100 + port * 0x80 + off) = val;
}

/* --- Port indítás/leállítás ---------------------------------------------- */

static void port_stop(int port) {
    uint32_t cmd = port_read(port, PORT_CMD);
    cmd &= ~CMD_ST;
    cmd &= ~CMD_FRE;
    port_write(port, PORT_CMD, cmd);

    /* Várunk amíg CR és FR clear */
    for (int i = 0; i < 1000000; i++) {
        uint32_t c = port_read(port, PORT_CMD);
        if (!(c & CMD_CR) && !(c & CMD_FR)) return;
    }
}

/* Egyszerű, ciklus-alapú késleltetés ~1 ms-ig (BSP, polling). */
static void short_delay(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++) {
        (void)inb(0x80);  /* port 0x80 ~1us delay */
    }
}

/* Port COMRESET (PHY szintű reset). A SIG mező csak ez után érvényes. */
static int port_reset(int port) {
    /* SCTL DET = 1 (request reset) */
    uint32_t sctl = port_read(port, PORT_SCTL);
    port_write(port, PORT_SCTL, (sctl & ~0x0Fu) | 0x1);
    short_delay(2000);  /* min 1 ms */
    port_write(port, PORT_SCTL, sctl & ~0x0Fu);

    /* Várunk amíg DET = 3 */
    for (int i = 0; i < 1000000; i++) {
        uint32_t s = port_read(port, PORT_SSTS);
        if ((s & 0x0F) == 0x3) return 0;
    }
    return -1;
}

static void port_start(int port) {
    /* Várunk amíg BSY+DRQ clear (timeout 1M iter ~10ms-1s). */
    int spin = 0;
    while ((port_read(port, PORT_TFD) & (TFD_BSY | TFD_DRQ)) && spin < 1000000) spin++;
    if (spin == 1000000) {
        kprintf("[ahci] port_start: BSY/DRQ stuck (TFD=0x%x); trying COMRESET\n",
                port_read(port, PORT_TFD));
        port_reset(port);
        /* Második próbálkozás reset után */
        spin = 0;
        while ((port_read(port, PORT_TFD) & (TFD_BSY | TFD_DRQ)) && spin < 1000000) spin++;
    }

    uint32_t cmd = port_read(port, PORT_CMD);
    cmd |= CMD_FRE;
    cmd |= CMD_ST;
    port_write(port, PORT_CMD, cmd);
}

/* --- Parancs kiadás ----------------------------------------------------- */

static int issue_cmd(int port, uint8_t ata_cmd, uint64_t lba, uint16_t count,
                     uintptr_t dma_phys, uint32_t byte_count, int is_identify) {
    /* PRDT entry */
    ahci_prdt_entry_t *prdt = &s_cmdtable->prdt[0];
    prdt->dba  = (uint32_t)(dma_phys & 0xFFFFFFFF);
    prdt->dbau = (uint32_t)((uint64_t)dma_phys >> 32);
    prdt->rsv0 = 0;
    prdt->dbc  = byte_count - 1;
    prdt->rsv1 = 0;
    prdt->i    = 0;

    /* Command header */
    ahci_cmd_header_t *hdr = &s_cmdlist[0];
    hdr->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    hdr->a = 0;
    hdr->w = 0;   /* device-to-host (read) */
    hdr->p = 0; hdr->r = 0; hdr->b = 0; hdr->c = 0;
    hdr->rsv0 = 0; hdr->pmp = 0;
    hdr->prdtl = 1;
    hdr->prdbc = 0;
    hdr->ctba  = (uint32_t)(s_cmdtable_phys & 0xFFFFFFFF);
    hdr->ctbau = (uint32_t)((uint64_t)s_cmdtable_phys >> 32);
    for (int i = 0; i < 4; i++) hdr->rsv1[i] = 0;

    /* Command FIS */
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)s_cmdtable->cfis;
    for (int i = 0; i < 64; i++) s_cmdtable->cfis[i] = 0;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport = 0;
    fis->c = 1;
    fis->command = ata_cmd;
    fis->featurel = 0;
    fis->featureh = 0;

    if (is_identify) {
        fis->device = 0;
        fis->lba0 = fis->lba1 = fis->lba2 = 0;
        fis->lba3 = fis->lba4 = fis->lba5 = 0;
        fis->count = 0;
    } else {
        fis->device = (1 << 6); /* LBA mode */
        fis->lba0 = (uint8_t)(lba);
        fis->lba1 = (uint8_t)(lba >> 8);
        fis->lba2 = (uint8_t)(lba >> 16);
        fis->lba3 = (uint8_t)(lba >> 24);
        fis->lba4 = (uint8_t)(lba >> 32);
        fis->lba5 = (uint8_t)(lba >> 40);
        fis->count = count;
    }
    fis->icc = 0;
    fis->control = 0;
    for (int i = 0; i < 4; i++) fis->rsv1[i] = 0;

    /* Várunk amíg a port felszabadul */
    int spin = 0;
    while ((port_read(port, PORT_TFD) & (TFD_BSY | TFD_DRQ)) && spin < 1000000) spin++;
    if (spin == 1000000) {
        kprintf("[ahci] port hung before command\n");
        return -1;
    }

    /* Töröljük az error-okat */
    port_write(port, PORT_SERR, 0xFFFFFFFF);
    port_write(port, PORT_IS,   0xFFFFFFFF);

    /* Issue: slot 0 */
    port_write(port, PORT_CI, 1u << 0);

    /* Polling */
    for (int i = 0; i < 10000000; i++) {
        if (!(port_read(port, PORT_CI) & (1u << 0))) break;
        if (port_read(port, PORT_IS) & (1u << 30)) {
            kprintf("[ahci] task file error\n");
            return -1;
        }
    }
    if (port_read(port, PORT_CI) & (1u << 0)) {
        kprintf("[ahci] command timeout\n");
        return -1;
    }
    if (port_read(port, PORT_TFD) & TFD_ERR) {
        kprintf("[ahci] command failed (TFD=0x%x)\n", port_read(port, PORT_TFD));
        return -1;
    }
    return 0;
}

/* --- Public: block device read callback ----------------------------------- */

static int ahci_block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf) {
    (void)dev;
    if (!s_abar || s_port < 0) return 0;

    uint8_t *out = (uint8_t *)buf;
    uint32_t done = 0;
    uint32_t max_per_cmd = (SCRATCH_PAGES * 4096) / 512;  /* 128 sect */

    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > max_per_cmd) chunk = max_per_cmd;

        if (issue_cmd(s_port, ATA_CMD_READ_DMA_EX, lba + done,
                      (uint16_t)chunk, s_scratch_phys, chunk * 512, 0) != 0) {
            return (int)done;
        }
        /* Másoljuk a scratch-ből a hívó pufferébe */
        kmemcpy(out + done * 512, s_scratch, chunk * 512);
        done += chunk;
    }
    return (int)done;
}

/* --- Init ---------------------------------------------------------------- */

static int find_first_active_port(uint32_t pi) {
    /* Több körös keresés: a SIG mező csak port reset után érvényes,
     * de a DET mező már HBA reset után is jelez. Az IDENTIFY-ig nem
     * tudjuk, hogy ATAPI vagy ATA, de a DET=3 elég ahhoz, hogy
     * próbálkozzunk. */
    for (int p = 0; p < 32; p++) {
        if (!(pi & (1u << p))) continue;
        uint32_t ssts = port_read(p, PORT_SSTS);
        uint32_t det = ssts & 0x0F;
        uint32_t ipm = (ssts >> 8) & 0x0F;
        if (det != SSTS_DET_PRESENT) continue;
        if (ipm != 0x1) continue;  /* Active power state */
        uint32_t sig = port_read(p, PORT_SIG);
        kprintf("[ahci] port %d: DET=3 IPM=1 SIG=0x%x (candidate)\n", p, sig);
        /* SATAPI signature mellett kihagyjuk */
        if (sig == 0xEB140101 || sig == 0xC33C0101 || sig == 0x96690101) {
            kprintf("[ahci]   skipping non-disk signature\n");
            continue;
        }
        return p;
    }
    return -1;
}

bool ahci_init(void) {
    const pci_device_t *pd = pci_find_class(0x01, 0x06, 0x01);
    if (!pd) {
        kprintf("[ahci] no SATA AHCI controller found on PCI\n");
        return false;
    }
    kprintf("[ahci] found controller %04x:%04x at %02x:%02x.%x\n",
            pd->vendor, pd->device, pd->bus, pd->dev, pd->func);

    pci_enable_busmaster(pd);

    uint64_t abar_phys = pci_get_bar(pd, 5);
    if (!abar_phys) {
        kprintf("[ahci] BAR5 invalid\n");
        return false;
    }
    s_abar = (volatile uint8_t *)phys_to_virt((uintptr_t)abar_phys);

    /* AHCI enable */
    uint32_t ghc = hba_read(HBA_GHC);
    hba_write(HBA_GHC, ghc | GHC_AE);

    /* HBA Reset */
    hba_write(HBA_GHC, hba_read(HBA_GHC) | GHC_HR);
    for (int i = 0; i < 1000000; i++) {
        if (!(hba_read(HBA_GHC) & GHC_HR)) break;
    }
    if (hba_read(HBA_GHC) & GHC_HR) {
        kprintf("[ahci] HBA reset timeout\n");
        return false;
    }
    /* Reset után AHCI mode-ot újra be kell kapcsolni */
    hba_write(HBA_GHC, hba_read(HBA_GHC) | GHC_AE);

    uint32_t cap = hba_read(HBA_CAP);
    uint32_t pi  = hba_read(HBA_PI);
    int max_ports = (cap & 0x1F) + 1;
    kprintf("[ahci] CAP=0x%x PI=0x%x (max %d ports)\n", cap, pi, max_ports);

    s_port = find_first_active_port(pi);
    if (s_port < 0) {
        kprintf("[ahci] no SATA device attached\n");
        return false;
    }
    kprintf("[ahci] using port %d\n", s_port);

    kprintf("[ahci] stopping port...\n");
    port_stop(s_port);
    kprintf("[ahci] port stopped\n");

    /* Foglalunk: 1 lap parancs-strukturákhoz + 16 lap DMA scratch.
     * Mind 4 KB-aligned (PMM ezt garantálja), és a parancs-strukturák
     * eleve 1 KB-aligned igénye is teljesül. */
    uintptr_t ctrl_phys = pmm_alloc_frame();
    if (!ctrl_phys) return false;
    /* Töröljük */
    uint8_t *ctrl_v = (uint8_t *)phys_to_virt(ctrl_phys);
    for (int i = 0; i < 4096; i++) ctrl_v[i] = 0;

    s_cmdlist_phys  = ctrl_phys;
    s_cmdlist       = (ahci_cmd_header_t *)ctrl_v;  /* 32*32 = 1024 B */
    s_fis_phys      = ctrl_phys + 1024;
    s_fis           = ctrl_v + 1024;                /* 256 B */
    s_cmdtable_phys = ctrl_phys + 1024 + 256;
    s_cmdtable      = (ahci_cmd_table_t *)(ctrl_v + 1024 + 256);

    /* DMA scratch */
    s_scratch_phys = pmm_alloc_frames(SCRATCH_PAGES);
    if (!s_scratch_phys) {
        pmm_free_frame(ctrl_phys);
        return false;
    }
    s_scratch = (uint8_t *)phys_to_virt(s_scratch_phys);

    /* Port konfiguráció */
    port_write(s_port, PORT_CLB,  (uint32_t)(s_cmdlist_phys & 0xFFFFFFFF));
    port_write(s_port, PORT_CLBU, (uint32_t)((uint64_t)s_cmdlist_phys >> 32));
    port_write(s_port, PORT_FB,   (uint32_t)(s_fis_phys & 0xFFFFFFFF));
    port_write(s_port, PORT_FBU,  (uint32_t)((uint64_t)s_fis_phys >> 32));

    /* Disable interrupts (polling) */
    port_write(s_port, PORT_IE, 0);
    port_write(s_port, PORT_IS, 0xFFFFFFFF);
    port_write(s_port, PORT_SERR, 0xFFFFFFFF);

    /* Power on, spin up, ICC = active */
    uint32_t pcmd = port_read(s_port, PORT_CMD);
    pcmd |= CMD_POD | CMD_SUD;
    pcmd = (pcmd & ~(0xFu << 28)) | (0x1u << 28);   /* ICC = active */
    port_write(s_port, PORT_CMD, pcmd);

    kprintf("[ahci] starting port...\n");
    port_start(s_port);
    kprintf("[ahci] port started, issuing IDENTIFY...\n");

    /* IDENTIFY DEVICE */
    if (issue_cmd(s_port, ATA_CMD_IDENTIFY, 0, 0, s_scratch_phys, 512, 1) != 0) {
        kprintf("[ahci] IDENTIFY failed\n");
        return false;
    }

    uint16_t *id = (uint16_t *)s_scratch;
    /* LBA48 sectors at word 100..103 */
    uint64_t lba48 =
        (uint64_t)id[100]
      | ((uint64_t)id[101] << 16)
      | ((uint64_t)id[102] << 32)
      | ((uint64_t)id[103] << 48);
    if (lba48 == 0) {
        /* fallback LBA28 (word 60..61) */
        lba48 = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    }
    s_sectors = lba48;

    char model[41];
    for (int i = 0; i < 20; i++) {
        model[i*2]     = (char)(id[27 + i] >> 8);
        model[i*2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    model[40] = 0;
    for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;

    kprintf("[ahci] device: %s, sectors=%lu (%lu MB)\n",
            model, s_sectors, (s_sectors * 512) / (1024 * 1024));

    /* Block device regisztráció */
    block_device_t bd;
    for (int i = 0; i < 32; i++) bd.name[i] = 0;
    bd.name[0] = 'a'; bd.name[1] = 'h'; bd.name[2] = 'c'; bd.name[3] = 'i';
    bd.name[4] = '0' + s_port;
    bd.sector_size  = 512;
    bd.sector_count = s_sectors;
    bd.read  = ahci_block_read;
    bd.write = NULL;
    bd.driver_data = NULL;
    block_register(&bd);

    return true;
}
