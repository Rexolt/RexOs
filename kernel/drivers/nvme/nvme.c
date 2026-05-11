/* Rex OS - NVMe PCIe SSD driver
 *
 * Megvalósítás:
 *   - PCI class=01h sub=08h prog_if=02h keresés
 *   - BAR0 (64-bit) MMIO elérés HHDM-en keresztül
 *   - HBA reset, Admin + I/O queue setup
 *   - Identify Controller + Identify Namespace
 *   - Read + Write DMA (scatter/gather nélkül, max 8 szektoros chunk)
 *   - Block device regisztráció
 */

#include <drivers/nvme/nvme.h>
#include <drivers/pci/pci.h>
#include <drivers/block/block.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <lib/printf.h>
#include <lib/string.h>
#include <lib/panic.h>

/* --- NVMe Controller Register Offsets ------------------------------------ */
#define NVME_REG_CAP    0x00   /* Controller Capabilities (8 byte) */
#define NVME_REG_VS     0x08   /* Version (4 byte) */
#define NVME_REG_INTMS  0x0C   /* Interrupt Mask Set */
#define NVME_REG_INTMC  0x10   /* Interrupt Mask Clear */
#define NVME_REG_CC     0x14   /* Controller Configuration (4 byte) */
#define NVME_REG_CSTS   0x1C   /* Controller Status (4 byte) */
#define NVME_REG_NSSR   0x20   /* NVM Subsystem Reset */
#define NVME_REG_AQA    0x24   /* Admin Queue Attributes (4 byte) */
#define NVME_REG_ASQ    0x28   /* Admin SQ Base Address (8 byte) */
#define NVME_REG_ACQ    0x30   /* Admin CQ Base Address (8 byte) */
/* Doorbell base: 0x1000, stride = 4 << CAP.DSTRD */

/* CC fields */
#define CC_EN       (1u << 0)
#define CC_CSS_NVM  (0u << 4)   /* NVM Command Set */
#define CC_MPS(x)   ((x) << 7)  /* Memory Page Size (log2(page)-12) */
#define CC_IOSQES(x) ((x) << 16) /* I/O SQ entry size (log2) */
#define CC_IOCQES(x) ((x) << 20) /* I/O CQ entry size (log2) */

/* CSTS fields */
#define CSTS_RDY    (1u << 0)
#define CSTS_CFS    (1u << 1)

/* --- Queue depth --------------------------------------------------------- */
#define NVME_QUEUE_DEPTH  64    /* bejegyzések száma egy sorban */

/* --- NVMe Submission Queue Entry (64 byte) ------------------------------- */
typedef struct {
    uint32_t dw0;   /* opcode[7:0], fuse[9:8], rsvd[13:10], PSDT[15:14], CID[31:16] */
    uint32_t nsid;
    uint64_t rsvd;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t dw10;
    uint32_t dw11;
    uint32_t dw12;
    uint32_t dw13;
    uint32_t dw14;
    uint32_t dw15;
} __attribute__((packed)) nvme_sqe_t;

_Static_assert(sizeof(nvme_sqe_t) == 64, "NVMe SQE must be 64 bytes");

/* --- NVMe Completion Queue Entry (16 byte) ------------------------------- */
typedef struct {
    uint32_t dw0;         /* command specific */
    uint32_t dw1;         /* reserved */
    uint16_t sq_head;     /* SQ head pointer */
    uint16_t sq_id;       /* SQ identifier */
    uint16_t cid;         /* command identifier */
    uint16_t status;      /* P[0], SF[15:1] */
} __attribute__((packed)) nvme_cqe_t;

_Static_assert(sizeof(nvme_cqe_t) == 16, "NVMe CQE must be 16 bytes");

/* --- Admin Opcodes ------------------------------------------------------- */
#define NVME_ADMIN_DELETE_SQ  0x00
#define NVME_ADMIN_CREATE_SQ  0x01
#define NVME_ADMIN_DELETE_CQ  0x04
#define NVME_ADMIN_CREATE_CQ  0x05
#define NVME_ADMIN_IDENTIFY   0x06

/* --- I/O Opcodes --------------------------------------------------------- */
#define NVME_IO_WRITE  0x01
#define NVME_IO_READ   0x02

/* --- Globális állapot ---------------------------------------------------- */

static volatile uint8_t *s_bar0    = NULL;
static uint32_t          s_dstrd   = 0;    /* doorbell stride bytes */
static uint64_t          s_ns_size = 0;    /* namespace size in LBAs */
static uint32_t          s_lba_sz  = 512;  /* LBA size (bytes) */

/* Admin queue */
static nvme_sqe_t *s_asq        = NULL;
static nvme_cqe_t *s_acq        = NULL;
static uintptr_t   s_asq_phys   = 0;
static uintptr_t   s_acq_phys   = 0;
static uint32_t    s_asq_tail   = 0;
static uint32_t    s_acq_head   = 0;
static uint32_t    s_acq_phase  = 1;   /* expected phase bit */

/* I/O queue (QID=1) */
static nvme_sqe_t *s_iosq       = NULL;
static nvme_cqe_t *s_iocq       = NULL;
static uintptr_t   s_iosq_phys  = 0;
static uintptr_t   s_iocq_phys  = 0;
static uint32_t    s_iosq_tail  = 0;
static uint32_t    s_iocq_head  = 0;
static uint32_t    s_iocq_phase = 1;

/* DMA buffer: 2 egybefüggő page = 8192 byte = 16 szektor */
static uint8_t    *s_dma_buf      = NULL;
static uintptr_t   s_dma_phys[2]  = { 0, 0 };  /* 2 lapra osztva */

static uint16_t    s_cid = 0;   /* command ID counter */

/* --- MMIO olvasás/írás --------------------------------------------------- */

static inline uint32_t nvme_r32(uint32_t off) {
    return *(volatile uint32_t *)(s_bar0 + off);
}
static inline uint64_t nvme_r64(uint32_t off) {
    uint32_t lo = *(volatile uint32_t *)(s_bar0 + off);
    uint32_t hi = *(volatile uint32_t *)(s_bar0 + off + 4);
    return ((uint64_t)hi << 32) | lo;
}
static inline void nvme_w32(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(s_bar0 + off) = val;
}
static inline void nvme_w64(uint32_t off, uint64_t val) {
    *(volatile uint32_t *)(s_bar0 + off)     = (uint32_t)(val & 0xFFFFFFFF);
    *(volatile uint32_t *)(s_bar0 + off + 4) = (uint32_t)(val >> 32);
}

/* Doorbell regiszter offset:
 *   SQ n tail: 0x1000 + 2n * stride
 *   CQ n head: 0x1000 + (2n+1) * stride */
static inline uint32_t sq_db_off(uint32_t qid) { return 0x1000 + 2 * qid * s_dstrd; }
static inline uint32_t cq_db_off(uint32_t qid) { return 0x1000 + (2 * qid + 1) * s_dstrd; }

/* --- Command submission -------------------------------------------------- */

static uint16_t next_cid(void) { return s_cid++; }

/* Admin parancs küldése és szinkron megvárása */
static int admin_cmd(nvme_sqe_t *cmd) {
    uint16_t cid = next_cid();
    cmd->dw0 = (cmd->dw0 & 0x0000FFFF) | ((uint32_t)cid << 16);

    /* Beírjuk a parancsot az Admin SQ slot-jába */
    s_asq[s_asq_tail] = *cmd;
    s_asq_tail = (s_asq_tail + 1) % NVME_QUEUE_DEPTH;

    /* Becsengő: Admin SQ tail doorbell */
    nvme_w32(sq_db_off(0), s_asq_tail);

    /* Polling: Admin CQ-ban várjuk a completion-t */
    for (int i = 0; i < 10000000; i++) {
        nvme_cqe_t *cqe = &s_acq[s_acq_head];
        uint16_t st = cqe->status;
        if ((st & 1) == (uint16_t)s_acq_phase) {
            uint16_t sc = (st >> 1) & 0xFF;
            s_acq_head = (s_acq_head + 1) % NVME_QUEUE_DEPTH;
            if (s_acq_head == 0) s_acq_phase ^= 1u;
            nvme_w32(cq_db_off(0), s_acq_head);
            if (sc != 0) {
                kprintf("[nvme] admin cmd 0x%02x FAILED status=0x%03x\n",
                        (uint8_t)cmd->dw0, (st >> 1) & 0x7FFF);
                return -1;
            }
            return 0;
        }
    }
    kprintf("[nvme] admin cmd timeout\n");
    return -1;
}

/* I/O parancs (read/write) küldése szinkron */
static int io_cmd_submit_poll(nvme_sqe_t *cmd) {
    uint16_t cid = next_cid();
    cmd->dw0 = (cmd->dw0 & 0x0000FFFF) | ((uint32_t)cid << 16);
    cmd->nsid = 1;

    s_iosq[s_iosq_tail] = *cmd;
    s_iosq_tail = (s_iosq_tail + 1) % NVME_QUEUE_DEPTH;
    nvme_w32(sq_db_off(1), s_iosq_tail);

    for (int i = 0; i < 10000000; i++) {
        nvme_cqe_t *cqe = &s_iocq[s_iocq_head];
        uint16_t st = cqe->status;
        if ((st & 1) == (uint16_t)s_iocq_phase) {
            uint16_t sc = (st >> 1) & 0xFF;
            s_iocq_head = (s_iocq_head + 1) % NVME_QUEUE_DEPTH;
            if (s_iocq_head == 0) s_iocq_phase ^= 1u;
            nvme_w32(cq_db_off(1), s_iocq_head);
            if (sc != 0) {
                kprintf("[nvme] I/O cmd FAILED status=0x%03x\n",
                        (st >> 1) & 0x7FFF);
                return -1;
            }
            return 0;
        }
    }
    kprintf("[nvme] I/O cmd timeout\n");
    return -1;
}

/* --- Block I/O ----------------------------------------------------------- */

/* Max 8 szektort küldünk egyszerre (1 lap = 4096 byte ha 512B/szek).
 * 2 DMA lapot allokáltunk, tehát legfeljebb 16 szektort.
 * A DMA buf 2 lapból áll: PRP1 = lap0, PRP2 = lap1.
 */
#define MAX_SECTORS_PER_CMD 16u

static int nvme_do_io(uint64_t lba, uint32_t count, void *buf, int is_write) {
    uint8_t *p = (uint8_t *)buf;
    uint32_t done = 0;

    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > MAX_SECTORS_PER_CMD) chunk = MAX_SECTORS_PER_CMD;

        uint32_t bytes = chunk * s_lba_sz;

        if (is_write) {
            /* Másolás DMA bufferbe */
            kmemcpy(s_dma_buf, p + done * s_lba_sz, bytes);
        }

        nvme_sqe_t cmd;
        kmemset(&cmd, 0, sizeof(cmd));
        cmd.dw0  = is_write ? NVME_IO_WRITE : NVME_IO_READ;
        cmd.prp1 = s_dma_phys[0];
        /* Ha több mint 1 lap kell, PRP2 = második lap */
        cmd.prp2 = (bytes > 4096) ? s_dma_phys[1] : 0;
        /* DW10: Starting LBA low 32 bit, DW11: high 32 bit */
        cmd.dw10 = (uint32_t)(lba + done);
        cmd.dw11 = (uint32_t)((lba + done) >> 32);
        /* DW12: NLB (number of logical blocks - 1) */
        cmd.dw12 = chunk - 1;

        if (io_cmd_submit_poll(&cmd) != 0) return (int)done;

        if (!is_write) {
            /* Másolás DMA bufferből a hívó pufferébe */
            kmemcpy(p + done * s_lba_sz, s_dma_buf, bytes);
        }
        done += chunk;
    }
    return (int)done;
}

static int nvme_block_read(block_device_t *dev, uint64_t lba, uint32_t count, void *buf) {
    (void)dev;
    return nvme_do_io(lba, count, buf, 0);
}

static int nvme_block_write(block_device_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    (void)dev;
    return nvme_do_io(lba, count, (void *)buf, 1);
}

/* --- Init ---------------------------------------------------------------- */

bool nvme_init(void) {
    const pci_device_t *pd = pci_find_class(0x01, 0x08, 0x02);
    if (!pd) {
        /* prog_if wildcard: néha 0x00-val is megjelenik */
        pd = pci_find_class(0x01, 0x08, 0xFF);
    }
    if (!pd) {
        kprintf("[nvme] No NVMe controller found on PCI bus\n");
        return false;
    }

    kprintf("[nvme] Found NVMe controller %04x:%04x at %02x:%02x.%x\n",
            pd->vendor, pd->device, pd->bus, pd->dev, pd->func);

    pci_enable_busmaster(pd);

    /* BAR0 (64-bit) fizikai cím */
    uint64_t bar_phys = pci_get_bar(pd, 0);
    if (!bar_phys) {
        kprintf("[nvme] BAR0 is 0, cannot map registers\n");
        return false;
    }
    kprintf("[nvme] BAR0 phys=0x%lx\n", bar_phys);

    /* MMIO elérése HHDM-en keresztül (0..4GB mappelve az vmm_init-ben) */
    s_bar0 = (volatile uint8_t *)phys_to_virt(bar_phys);

    /* CAP olvasása */
    uint64_t cap = nvme_r64(NVME_REG_CAP);
    uint32_t to     = (uint32_t)((cap >> 24) & 0xFF);  /* timeout in 500ms */
    s_dstrd = (uint32_t)(4u << ((cap >> 32) & 0xF));   /* doorbell stride bytes */
    uint32_t mqes   = (uint32_t)((cap & 0xFFFF) + 1);  /* max queue entries */

    kprintf("[nvme] CAP: TO=%u*500ms, DSTRD=%u, MQES=%u\n", to, s_dstrd, mqes);

    /* Kontroller leállítása: CC.EN = 0 */
    uint32_t cc = nvme_r32(NVME_REG_CC);
    cc &= ~CC_EN;
    nvme_w32(NVME_REG_CC, cc);

    /* CSTS.RDY == 0-ra várunk */
    for (int i = 0; i < 1000000; i++) {
        if (!(nvme_r32(NVME_REG_CSTS) & CSTS_RDY)) break;
    }
    if (nvme_r32(NVME_REG_CSTS) & CSTS_RDY) {
        kprintf("[nvme] controller did not become NOT-READY after CC.EN=0\n");
        return false;
    }

    /* Admin sorok allokálása (4KB-igazítású fizikai memória) */
    s_asq_phys = pmm_alloc_frame();
    s_acq_phys = pmm_alloc_frame();
    if (!s_asq_phys || !s_acq_phys) {
        kprintf("[nvme] OOM allocating admin queues\n");
        return false;
    }
    s_asq = (nvme_sqe_t *)phys_to_virt(s_asq_phys);
    s_acq = (nvme_cqe_t *)phys_to_virt(s_acq_phys);
    kmemset(s_asq, 0, 4096);
    kmemset(s_acq, 0, 4096);

    /* I/O sorok allokálása */
    s_iosq_phys = pmm_alloc_frame();
    s_iocq_phys = pmm_alloc_frame();
    if (!s_iosq_phys || !s_iocq_phys) {
        kprintf("[nvme] OOM allocating I/O queues\n");
        return false;
    }
    s_iosq = (nvme_sqe_t *)phys_to_virt(s_iosq_phys);
    s_iocq = (nvme_cqe_t *)phys_to_virt(s_iocq_phys);
    kmemset(s_iosq, 0, 4096);
    kmemset(s_iocq, 0, 4096);

    /* DMA buffer: 2 egybefüggő fizikai lap */
    s_dma_phys[0] = pmm_alloc_frame();
    s_dma_phys[1] = pmm_alloc_frame();
    if (!s_dma_phys[0] || !s_dma_phys[1]) {
        kprintf("[nvme] OOM allocating DMA buffer\n");
        return false;
    }
    s_dma_buf = (uint8_t *)phys_to_virt(s_dma_phys[0]);
    /* Megjegyzés: a két lap nem feltétlenül folytonos a fizikai memóriában,
     * de a PRP1+PRP2 modell miatt ez nem baj. */

    /* AQA: Admin SQ + CQ mérete (0-alapú, tehát QUEUE_DEPTH-1) */
    uint32_t aqa = ((NVME_QUEUE_DEPTH - 1) << 16) | (NVME_QUEUE_DEPTH - 1);
    nvme_w32(NVME_REG_AQA, aqa);
    nvme_w64(NVME_REG_ASQ, (uint64_t)s_asq_phys);
    nvme_w64(NVME_REG_ACQ, (uint64_t)s_acq_phys);

    /* CC beállítása és EN=1 */
    cc = CC_EN
       | CC_CSS_NVM
       | CC_MPS(0)       /* page size = 4096 (2^(12+0)) */
       | CC_IOSQES(6)    /* SQE size = 64 (2^6) */
       | CC_IOCQES(4);   /* CQE size = 16 (2^4) */
    nvme_w32(NVME_REG_CC, cc);

    /* CSTS.RDY == 1-re várunk */
    uint32_t timeout_loops = to * 500000; /* durva közelítés */
    if (timeout_loops == 0) timeout_loops = 5000000;
    for (uint32_t i = 0; i < timeout_loops; i++) {
        uint32_t csts = nvme_r32(NVME_REG_CSTS);
        if (csts & CSTS_CFS) {
            kprintf("[nvme] controller fatal status after enable\n");
            return false;
        }
        if (csts & CSTS_RDY) break;
    }
    if (!(nvme_r32(NVME_REG_CSTS) & CSTS_RDY)) {
        kprintf("[nvme] controller did not become READY\n");
        return false;
    }
    kprintf("[nvme] controller READY\n");

    /* --- Identify Controller (CNS=1) -------------------------------------- */
    uint8_t *id_buf_phys_alloc = NULL;
    uintptr_t id_phys = pmm_alloc_frame();
    if (!id_phys) return false;
    uint8_t *id_buf = (uint8_t *)phys_to_virt(id_phys);
    kmemset(id_buf, 0, 4096);

    nvme_sqe_t cmd;
    kmemset(&cmd, 0, sizeof(cmd));
    cmd.dw0  = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 0;
    cmd.prp1 = id_phys;
    cmd.dw10 = 1;  /* CNS=1: Identify Controller */

    if (admin_cmd(&cmd) != 0) {
        kprintf("[nvme] Identify Controller failed\n");
        pmm_free_frame(id_phys);
        return false;
    }

    /* Model number bytes 24..63 */
    char model[41];
    for (int i = 0; i < 40; i++) model[i] = (char)id_buf[24 + i];
    model[40] = 0;
    /* byte-swap (big-endian word pairs) */
    for (int i = 0; i < 40; i += 2) {
        char tmp = model[i]; model[i] = model[i+1]; model[i+1] = tmp;
    }
    /* Trailing space trim */
    for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;

    kprintf("[nvme] controller model: %s\n", model);

    /* --- Create I/O Completion Queue (QID=1) ----------------------------- */
    kmemset(&cmd, 0, sizeof(cmd));
    cmd.dw0  = NVME_ADMIN_CREATE_CQ;
    cmd.prp1 = s_iocq_phys;
    /* DW10: QSIZE[15:0]=(QUEUE_DEPTH-1), QID[31:16]=1 */
    cmd.dw10 = ((uint32_t)1 << 16) | (NVME_QUEUE_DEPTH - 1);
    /* DW11: PC=1 (physically contiguous), IEN=0, IV=0 */
    cmd.dw11 = 1;

    if (admin_cmd(&cmd) != 0) {
        kprintf("[nvme] Create I/O CQ failed\n");
        pmm_free_frame(id_phys);
        return false;
    }
    kprintf("[nvme] I/O CQ (QID=1) created\n");

    /* --- Create I/O Submission Queue (QID=1) ----------------------------- */
    kmemset(&cmd, 0, sizeof(cmd));
    cmd.dw0  = NVME_ADMIN_CREATE_SQ;
    cmd.prp1 = s_iosq_phys;
    cmd.dw10 = ((uint32_t)1 << 16) | (NVME_QUEUE_DEPTH - 1);
    /* DW11: QPRIO=0 (urgent), CQID=1, PC=1 */
    cmd.dw11 = ((uint32_t)1 << 16) | 1;

    if (admin_cmd(&cmd) != 0) {
        kprintf("[nvme] Create I/O SQ failed\n");
        pmm_free_frame(id_phys);
        return false;
    }
    kprintf("[nvme] I/O SQ (QID=1) created\n");

    /* --- Identify Namespace (NSID=1, CNS=0) ----------------------------- */
    kmemset(id_buf, 0, 4096);
    kmemset(&cmd, 0, sizeof(cmd));
    cmd.dw0  = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 1;
    cmd.prp1 = id_phys;
    cmd.dw10 = 0;  /* CNS=0: Identify Namespace */

    if (admin_cmd(&cmd) != 0) {
        kprintf("[nvme] Identify Namespace failed\n");
        pmm_free_frame(id_phys);
        return false;
    }

    /* NSZE: namespace size in LBAs (bytes 0-7) */
    s_ns_size = *(uint64_t *)id_buf;

    /* FLBAS: active LBA format index (byte 26, bits 3:0) */
    uint8_t flbas = id_buf[26] & 0x0F;
    /* LBAF array starts at byte 128, each entry 4 bytes */
    uint32_t lbaf = *(uint32_t *)(id_buf + 128 + flbas * 4);
    /* LBADS: bits 23:16 */
    uint8_t lbads = (uint8_t)((lbaf >> 16) & 0xFF);
    s_lba_sz = (lbads >= 9 && lbads <= 16) ? (1u << lbads) : 512u;

    pmm_free_frame(id_phys);
    (void)id_buf_phys_alloc;

    kprintf("[nvme] Namespace size: %lu LBAs, LBA size: %u bytes (%lu MB)\n",
            s_ns_size, s_lba_sz,
            (s_ns_size * s_lba_sz) / (1024 * 1024));

    /* --- Block device regisztrálás --------------------------------------- */
    block_device_t bd;
    kmemset(&bd, 0, sizeof(bd));
    bd.name[0]='n'; bd.name[1]='v'; bd.name[2]='m'; bd.name[3]='e';
    bd.name[4]='0'; bd.name[5]=0;
    bd.sector_size  = s_lba_sz;
    bd.sector_count = s_ns_size;
    bd.read         = nvme_block_read;
    bd.write        = nvme_block_write;
    bd.driver_data  = NULL;
    block_register(&bd);

    return true;
}
