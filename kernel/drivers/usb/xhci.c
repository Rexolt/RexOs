/* Rex OS - xHCI (USB 3.x) Host Controller driver
 *
 * Architektúra:
 *   - Minden struktúra fizikai memóriában él (PMM-ből allokálva),
 *     a CPU a HHDM-en keresztül éri el (phys_to_virt).
 *   - Rings: 256 TRB (Transfer Request Block) entry, 16 B / TRB = 4 KB (1 lap).
 *     Ring legvégén egy Link TRB vissza a ring elejére.
 *   - Event Ring: saját ring, ESRT (Event Ring Segment Table) + 1 segment.
 *   - A controller csak a fizikai címeket érti; mi a HHDM-et csak CPU hozzáférésre.
 *
 * Polling alapú: nincs IRQ még. A fő loop (xhci_poll) ellenőrzi az Event Ringet
 * és a HID endpointokat.
 */

#include <drivers/usb/xhci.h>
#include <drivers/pci/pci.h>
#include <mm/pmm.h>
#include <rexos/io.h>
#include <lib/printf.h>
#include <lib/string.h>

/* =============================================================================
 *  xHCI regiszter layout
 * ========================================================================== */

/* Capability Registers (offset 0 from BAR0) */
#define CAP_CAPLENGTH    0x00   /* u8:  length of capability regs */
#define CAP_HCIVERSION   0x02   /* u16: interface version BCD */
#define CAP_HCSPARAMS1   0x04   /* u32 */
#define CAP_HCSPARAMS2   0x08
#define CAP_HCSPARAMS3   0x0C
#define CAP_HCCPARAMS1   0x10
#define CAP_DBOFF        0x14   /* u32: doorbell array offset */
#define CAP_RTSOFF       0x18   /* u32: runtime register offset */
#define CAP_HCCPARAMS2   0x1C

/* Operational Registers (offset CAPLENGTH from BAR0) */
#define OP_USBCMD        0x00
#define OP_USBSTS        0x04
#define OP_PAGESIZE      0x08
#define OP_DNCTRL        0x14
#define OP_CRCR          0x18   /* Command Ring Control Register (u64) */
#define OP_DCBAAP        0x30   /* Device Context Base Address Array Ptr (u64) */
#define OP_CONFIG        0x38

/* Port Registers: base = OP + 0x400 + port_idx * 0x10 */
#define PORT_SC          0x00   /* Port Status/Control */
#define PORT_PMSC        0x04
#define PORT_LI          0x08
#define PORT_HLPMC       0x0C

/* USBCMD bits */
#define USBCMD_RUN       (1u << 0)
#define USBCMD_HCRST     (1u << 1)
#define USBCMD_INTE      (1u << 2)
#define USBCMD_HSEE      (1u << 3)

/* USBSTS bits */
#define USBSTS_HCH       (1u << 0)   /* HC Halted */
#define USBSTS_HSE       (1u << 2)
#define USBSTS_EINT      (1u << 3)   /* Event Interrupt */
#define USBSTS_PCD       (1u << 4)   /* Port Change Detect */
#define USBSTS_CNR       (1u << 11)  /* Controller Not Ready */
#define USBSTS_HCE       (1u << 12)

/* PORTSC bits */
#define PORTSC_CCS       (1u << 0)   /* Current Connect Status */
#define PORTSC_PED       (1u << 1)   /* Port Enabled/Disabled */
#define PORTSC_PR        (1u << 4)   /* Port Reset */
#define PORTSC_PP        (1u << 9)
#define PORTSC_CSC       (1u << 17)  /* Connect Status Change */
#define PORTSC_PEC       (1u << 18)
#define PORTSC_PRC       (1u << 21)  /* Port Reset Change */

/* Runtime Registers (offset RTSOFF from BAR0) */
#define RT_IR0           0x20   /* Interrupter 0 Register Set base */
#define IR_IMAN          0x00
#define IR_IMOD          0x04
#define IR_ERSTSZ        0x08
#define IR_ERSTBA        0x10   /* u64 */
#define IR_ERDP          0x18   /* u64, Event Ring Dequeue Pointer */

/* =============================================================================
 *  TRB-k és konstansok
 * ========================================================================== */

#define TRB_SIZE         16
#define RING_TRB_COUNT   256        /* 256 * 16 = 4096 B = 1 page */
#define RING_BYTES       (RING_TRB_COUNT * TRB_SIZE)

/* TRB típusok (shift 10-től a control dword-ben) */
#define TRB_NORMAL           1
#define TRB_SETUP_STAGE      2
#define TRB_DATA_STAGE       3
#define TRB_STATUS_STAGE     4
#define TRB_LINK             6
#define TRB_ENABLE_SLOT      9
#define TRB_ADDRESS_DEVICE   11
#define TRB_CONFIGURE_ENDPOINT 12
#define TRB_EVALUATE_CONTEXT 13
#define TRB_NOOP_CMD         23
#define TRB_EVENT_TRANSFER   32
#define TRB_EVENT_CMD_COMPLETION 33
#define TRB_EVENT_PORT_STATUS 34

/* TRB: 16 bájt = 4 DWORD */
#pragma pack(push, 1)
typedef struct {
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t control;   /* bit 0: cycle, bit 10..15: TRB type */
} xhci_trb_t;

/* Event Ring Segment Table entry */
typedef struct {
    uint32_t base_lo;
    uint32_t base_hi;
    uint32_t size;   /* number of TRBs in segment */
    uint32_t rsv;
} erst_entry_t;
#pragma pack(pop)

/* =============================================================================
 *  Globális állapot
 * ========================================================================== */

typedef struct {
    volatile uint8_t *mmio;       /* BAR0 (HHDM) */
    volatile uint8_t *op;         /* OP regs */
    volatile uint32_t *dbells;    /* Doorbell Array */
    volatile uint8_t *rt;         /* Runtime regs */

    uint32_t hcsparams1;
    uint32_t hcsparams2;
    uint32_t hccparams1;
    uint8_t  max_slots;
    uint8_t  max_intrs;
    uint8_t  max_ports;
    uint8_t  context_size;        /* 32 vagy 64 bájt */

    /* Device Context Base Address Array (max_slots+1 * 8 bájt) */
    uintptr_t      dcbaa_phys;
    volatile uint64_t *dcbaa;

    /* Command Ring */
    uintptr_t      cmd_ring_phys;
    xhci_trb_t    *cmd_ring;
    uint32_t       cmd_enqueue;
    uint32_t       cmd_cycle;

    /* Event Ring */
    uintptr_t      evt_ring_phys;
    xhci_trb_t    *evt_ring;
    uint32_t       evt_dequeue;
    uint32_t       evt_cycle;

    /* ERST (Event Ring Segment Table) */
    uintptr_t      erst_phys;
    erst_entry_t  *erst;

    bool           running;
} xhci_t;

static xhci_t g_xhci;

/* =============================================================================
 *  MMIO helperek
 * ========================================================================== */

static inline uint32_t mmio_r32(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}
static inline void mmio_w32(volatile uint8_t *base, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(base + off) = val;
}
static inline uint64_t mmio_r64(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint64_t *)(base + off);
}
static inline void mmio_w64(volatile uint8_t *base, uint32_t off, uint64_t val) {
    *(volatile uint64_t *)(base + off) = val;
}
static inline uint8_t  mmio_r8 (volatile uint8_t *base, uint32_t off) {
    return *(volatile uint8_t  *)(base + off);
}
static inline uint16_t mmio_r16(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint16_t *)(base + off);
}

/* Rövid busy-wait kb. N * ~1us nagyságrend */
static void usb_udelay(uint32_t us) {
    for (volatile uint32_t i = 0; i < us; i++) {
        (void)inb(0x80);
    }
}

/* =============================================================================
 *  xHCI init
 * ========================================================================== */

static bool xhci_reset_hc(xhci_t *x) {
    kprintf("[xhci] initial USBCMD=0x%x USBSTS=0x%x\n",
            mmio_r32(x->op, OP_USBCMD), mmio_r32(x->op, OP_USBSTS));

    /* Ha még nincs halt-ban: RUN bitet töröljük és várunk */
    uint32_t cmd = mmio_r32(x->op, OP_USBCMD);
    if (cmd & USBCMD_RUN) {
        cmd &= ~USBCMD_RUN;
        mmio_w32(x->op, OP_USBCMD, cmd);
        for (int i = 0; i < 1000000; i++) {
            if (mmio_r32(x->op, OP_USBSTS) & USBSTS_HCH) break;
        }
    }
    /* Néha a HC már halted állapotban van boot után, de HCH még nem olvasódik
     * azonnal 1-re; adjunk kis időt és újra ellenőrizzünk. */
    usb_udelay(1000);
    uint32_t sts = mmio_r32(x->op, OP_USBSTS);
    if (!(sts & USBSTS_HCH)) {
        kprintf("[xhci] HC not halted (USBSTS=0x%x), forcing reset anyway\n", sts);
    }

    /* HC Reset */
    mmio_w32(x->op, OP_USBCMD, USBCMD_HCRST);
    for (int i = 0; i < 2000000; i++) {
        uint32_t c = mmio_r32(x->op, OP_USBCMD);
        uint32_t s = mmio_r32(x->op, OP_USBSTS);
        if (!(c & USBCMD_HCRST) && !(s & USBSTS_CNR)) return true;
    }
    kprintf("[xhci] HC reset timeout\n");
    return false;
}

bool xhci_init(void) {
    /* Keresünk xHCI controllert: class=0x0C, subclass=0x03, prog_if=0x30 */
    const pci_device_t *pd = pci_find_class(0x0C, 0x03, 0x30);
    if (!pd) {
        kprintf("[xhci] no xHCI controller found on PCI\n");
        return false;
    }
    kprintf("[xhci] found controller %04x:%04x at %02x:%02x.%x\n",
            pd->vendor, pd->device, pd->bus, pd->dev, pd->func);

    pci_enable_busmaster(pd);

    uint64_t mmio_phys = pci_get_bar(pd, 0);
    if (!mmio_phys) {
        kprintf("[xhci] BAR0 invalid\n");
        return false;
    }
    /* BAR0 MMIO elérhető HHDM-en keresztül (4 GB alatt van QEMU-ban) */
    g_xhci.mmio = (volatile uint8_t *)phys_to_virt((uintptr_t)mmio_phys);

    /* Capability registers */
    uint8_t caplen = mmio_r8(g_xhci.mmio, CAP_CAPLENGTH);
    uint16_t ver   = mmio_r16(g_xhci.mmio, CAP_HCIVERSION);
    g_xhci.hcsparams1 = mmio_r32(g_xhci.mmio, CAP_HCSPARAMS1);
    g_xhci.hcsparams2 = mmio_r32(g_xhci.mmio, CAP_HCSPARAMS2);
    g_xhci.hccparams1 = mmio_r32(g_xhci.mmio, CAP_HCCPARAMS1);
    uint32_t dboff = mmio_r32(g_xhci.mmio, CAP_DBOFF) & ~0x3u;
    uint32_t rtsoff = mmio_r32(g_xhci.mmio, CAP_RTSOFF) & ~0x1Fu;

    g_xhci.op      = g_xhci.mmio + caplen;
    g_xhci.dbells  = (volatile uint32_t *)(g_xhci.mmio + dboff);
    g_xhci.rt      = g_xhci.mmio + rtsoff;

    g_xhci.max_slots = (uint8_t)(g_xhci.hcsparams1 & 0xFF);
    g_xhci.max_intrs = (uint8_t)((g_xhci.hcsparams1 >> 8) & 0x7FF);
    g_xhci.max_ports = (uint8_t)((g_xhci.hcsparams1 >> 24) & 0xFF);
    g_xhci.context_size = (g_xhci.hccparams1 & (1u << 2)) ? 64 : 32;

    kprintf("[xhci] ver=0x%x caplen=%u hcs1=0x%x hcc1=0x%x\n",
            ver, caplen, g_xhci.hcsparams1, g_xhci.hccparams1);
    kprintf("[xhci] slots=%u ports=%u intrs=%u ctx_size=%u\n",
            g_xhci.max_slots, g_xhci.max_ports, g_xhci.max_intrs,
            g_xhci.context_size);

    /* Reset */
    if (!xhci_reset_hc(&g_xhci)) return false;
    kprintf("[xhci] HC reset OK\n");

    /* --- CONFIG: max device slots enable --------------------------------- */
    mmio_w32(g_xhci.op, OP_CONFIG, g_xhci.max_slots);

    /* --- DCBAA allokálása ------------------------------------------------- */
    /* (max_slots + 1) * 8 bájt, 64 B-aligned. 1 page bőven elég. */
    g_xhci.dcbaa_phys = pmm_alloc_frame();
    if (!g_xhci.dcbaa_phys) return false;
    g_xhci.dcbaa = (volatile uint64_t *)phys_to_virt(g_xhci.dcbaa_phys);
    for (int i = 0; i < 512; i++) g_xhci.dcbaa[i] = 0;
    mmio_w64(g_xhci.op, OP_DCBAAP, g_xhci.dcbaa_phys);

    /* --- Scratchpad buffers (ha kellenek) -------------------------------- */
    uint32_t max_scratch = ((g_xhci.hcsparams2 >> 27) & 0x1F)
                         | (((g_xhci.hcsparams2 >> 21) & 0x1F) << 5);
    if (max_scratch) {
        uintptr_t sp_array_phys = pmm_alloc_frame();
        uint64_t *sp_array = (uint64_t *)phys_to_virt(sp_array_phys);
        for (uint32_t i = 0; i < max_scratch; i++) {
            sp_array[i] = pmm_alloc_frame();
        }
        g_xhci.dcbaa[0] = sp_array_phys;
        kprintf("[xhci] scratchpad %u pages\n", max_scratch);
    }

    /* --- Command Ring ----------------------------------------------------- */
    g_xhci.cmd_ring_phys = pmm_alloc_frame();
    if (!g_xhci.cmd_ring_phys) return false;
    g_xhci.cmd_ring = (xhci_trb_t *)phys_to_virt(g_xhci.cmd_ring_phys);
    for (int i = 0; i < RING_TRB_COUNT; i++) {
        g_xhci.cmd_ring[i].param_lo = 0;
        g_xhci.cmd_ring[i].param_hi = 0;
        g_xhci.cmd_ring[i].status   = 0;
        g_xhci.cmd_ring[i].control  = 0;
    }
    /* Utolsó TRB: Link vissza a ring elejére, toggle cycle bit */
    xhci_trb_t *link = &g_xhci.cmd_ring[RING_TRB_COUNT - 1];
    link->param_lo = (uint32_t)(g_xhci.cmd_ring_phys & 0xFFFFFFFF);
    link->param_hi = (uint32_t)((uint64_t)g_xhci.cmd_ring_phys >> 32);
    link->status   = 0;
    link->control  = (TRB_LINK << 10) | (1u << 1);   /* Toggle Cycle */

    g_xhci.cmd_enqueue = 0;
    g_xhci.cmd_cycle   = 1;

    /* CRCR: base + RCS (Ring Cycle State) = 1 */
    mmio_w64(g_xhci.op, OP_CRCR, g_xhci.cmd_ring_phys | 0x1);

    /* --- Event Ring + ERST ----------------------------------------------- */
    g_xhci.evt_ring_phys = pmm_alloc_frame();
    if (!g_xhci.evt_ring_phys) return false;
    g_xhci.evt_ring = (xhci_trb_t *)phys_to_virt(g_xhci.evt_ring_phys);
    for (int i = 0; i < RING_TRB_COUNT; i++) {
        g_xhci.evt_ring[i].param_lo = 0;
        g_xhci.evt_ring[i].param_hi = 0;
        g_xhci.evt_ring[i].status   = 0;
        g_xhci.evt_ring[i].control  = 0;
    }
    g_xhci.evt_dequeue = 0;
    g_xhci.evt_cycle   = 1;

    g_xhci.erst_phys = pmm_alloc_frame();
    if (!g_xhci.erst_phys) return false;
    g_xhci.erst = (erst_entry_t *)phys_to_virt(g_xhci.erst_phys);
    g_xhci.erst[0].base_lo = (uint32_t)(g_xhci.evt_ring_phys & 0xFFFFFFFF);
    g_xhci.erst[0].base_hi = (uint32_t)((uint64_t)g_xhci.evt_ring_phys >> 32);
    g_xhci.erst[0].size    = RING_TRB_COUNT;
    g_xhci.erst[0].rsv     = 0;

    /* Interrupter 0 beállítás */
    mmio_w32(g_xhci.rt, RT_IR0 + IR_ERSTSZ, 1);
    mmio_w64(g_xhci.rt, RT_IR0 + IR_ERDP,   g_xhci.evt_ring_phys);
    mmio_w64(g_xhci.rt, RT_IR0 + IR_ERSTBA, g_xhci.erst_phys);
    mmio_w32(g_xhci.rt, RT_IR0 + IR_IMOD,   0);
    /* IMAN bit 1 (IE) egyelőre nem kell — polling módban dolgozunk */
    mmio_w32(g_xhci.rt, RT_IR0 + IR_IMAN,   0);

    /* --- Run! ------------------------------------------------------------ */
    uint32_t cmd = mmio_r32(g_xhci.op, OP_USBCMD);
    cmd |= USBCMD_RUN;
    mmio_w32(g_xhci.op, OP_USBCMD, cmd);

    /* CNR letisztul? */
    for (int i = 0; i < 1000000; i++) {
        if (!(mmio_r32(g_xhci.op, OP_USBSTS) & USBSTS_CNR)) break;
    }
    if (mmio_r32(g_xhci.op, OP_USBSTS) & USBSTS_CNR) {
        kprintf("[xhci] CNR stuck, HC not ready\n");
        return false;
    }

    kprintf("[xhci] HC running (USBSTS=0x%x)\n", mmio_r32(g_xhci.op, OP_USBSTS));
    g_xhci.running = true;
    return true;
}

/* =============================================================================
 *  Command Ring submission + Event Ring polling
 * ========================================================================== */

/* Submit egy TRB-t a command ring-re, harangot szólaltunk. */
static void cmd_submit(uint32_t p0, uint32_t p1, uint32_t st, uint32_t ctl_type_flags) {
    xhci_trb_t *trb = &g_xhci.cmd_ring[g_xhci.cmd_enqueue];
    trb->param_lo = p0;
    trb->param_hi = p1;
    trb->status   = st;
    /* Cycle bit a ctl legalsó bitje, típus bit 10-15 */
    trb->control  = ctl_type_flags | g_xhci.cmd_cycle;

    g_xhci.cmd_enqueue++;
    if (g_xhci.cmd_enqueue >= RING_TRB_COUNT - 1) {
        /* Link TRB: cycle bit-et toggle-öljük */
        xhci_trb_t *link = &g_xhci.cmd_ring[RING_TRB_COUNT - 1];
        link->control = (TRB_LINK << 10) | (1u << 1) | g_xhci.cmd_cycle;
        g_xhci.cmd_enqueue = 0;
        g_xhci.cmd_cycle ^= 1;
    }
    /* Doorbell 0 = command ring */
    g_xhci.dbells[0] = 0;
}

/* Megvár egy Command Completion Event-et és visszaadja a completion statusot +
 * slot ID-t. Timeout esetén -1. */
static int cmd_wait_completion(uint32_t *out_slot_id, uint32_t *out_status) {
    for (int i = 0; i < 2000000; i++) {
        xhci_trb_t *ev = &g_xhci.evt_ring[g_xhci.evt_dequeue];
        uint32_t ctl = ev->control;
        if ((ctl & 1) != g_xhci.evt_cycle) { usb_udelay(10); continue; }
        uint32_t type = (ctl >> 10) & 0x3F;
        if (type == TRB_EVENT_CMD_COMPLETION) {
            uint32_t code = (ev->status >> 24) & 0xFF;
            uint32_t slot = (ctl >> 24) & 0xFF;
            if (out_slot_id) *out_slot_id = slot;
            if (out_status)  *out_status  = code;
            /* Dequeue advance */
            g_xhci.evt_dequeue++;
            if (g_xhci.evt_dequeue >= RING_TRB_COUNT) {
                g_xhci.evt_dequeue = 0;
                g_xhci.evt_cycle ^= 1;
            }
            /* Frissítjük az ERDP-t (Event Handler Busy bit törlése is) */
            uint64_t erdp = g_xhci.evt_ring_phys + g_xhci.evt_dequeue * TRB_SIZE;
            mmio_w64(g_xhci.rt, RT_IR0 + IR_ERDP, erdp | (1ULL << 3));
            return (int)code;
        }
        /* Más típusú event — egyszerűen átugorjuk */
        g_xhci.evt_dequeue++;
        if (g_xhci.evt_dequeue >= RING_TRB_COUNT) {
            g_xhci.evt_dequeue = 0;
            g_xhci.evt_cycle ^= 1;
        }
        uint64_t erdp = g_xhci.evt_ring_phys + g_xhci.evt_dequeue * TRB_SIZE;
        mmio_w64(g_xhci.rt, RT_IR0 + IR_ERDP, erdp | (1ULL << 3));
    }
    return -1;
}

/* =============================================================================
 *  Slot / device management
 * ========================================================================== */

/* Egy device állapota — csak HID boot protokollhoz, minimum. */
typedef struct {
    bool     present;
    uint8_t  port;         /* 1-indexed port */
    uint8_t  slot_id;
    uint8_t  dev_class;    /* 3 = HID */
    uint8_t  iface_class;  /* 3 = HID */
    uint8_t  iface_sub;    /* 1 = boot */
    uint8_t  iface_proto;  /* 1 = kbd, 2 = mouse */
    uint8_t  ep_addr;      /* interrupt IN endpoint address (bit 7 = dir) */
    uint16_t ep_mps;       /* max packet size */
    uint8_t  ep_interval;  /* polling interval */
    /* TRB ring a HID interrupt endpointhoz */
    uintptr_t    ep_ring_phys;
    xhci_trb_t  *ep_ring;
    uint32_t     ep_enqueue;
    uint32_t     ep_cycle;
    /* Input + Device context fizikai címek */
    uintptr_t    in_ctx_phys;
    uintptr_t    dev_ctx_phys;
    /* Report buffer + DMA cím (8 bájt HID boot kbd / 4+ mouse) */
    uintptr_t    report_phys;
    uint8_t     *report;
} usb_device_t;

#define MAX_USB_DEVICES 8
static usb_device_t g_devs[MAX_USB_DEVICES];

/* HID boot protokoll állapot — visszaadjuk a keyboard/mouse API-nak */
static char     g_kbd_queue[64];
static uint32_t g_kbd_head = 0, g_kbd_tail = 0;

static int g_mouse_dx = 0, g_mouse_dy = 0;
static uint8_t g_mouse_buttons = 0;
static uint8_t g_prev_kbd_keys[6] = {0,0,0,0,0,0};

/* HID Usage ID -> ASCII konverzió (boot kbd Report ID 1) */
static const char hid_to_ascii[128] = {
    [0x04]='a', [0x05]='b', [0x06]='c', [0x07]='d', [0x08]='e', [0x09]='f',
    [0x0A]='g', [0x0B]='h', [0x0C]='i', [0x0D]='j', [0x0E]='k', [0x0F]='l',
    [0x10]='m', [0x11]='n', [0x12]='o', [0x13]='p', [0x14]='q', [0x15]='r',
    [0x16]='s', [0x17]='t', [0x18]='u', [0x19]='v', [0x1A]='w', [0x1B]='x',
    [0x1C]='y', [0x1D]='z',
    [0x1E]='1', [0x1F]='2', [0x20]='3', [0x21]='4', [0x22]='5', [0x23]='6',
    [0x24]='7', [0x25]='8', [0x26]='9', [0x27]='0',
    [0x28]='\n', [0x29]=0x1B /* ESC */, [0x2A]='\b', [0x2B]='\t',
    [0x2C]=' ',
    [0x2D]='-', [0x2E]='=', [0x2F]='[', [0x30]=']', [0x31]='\\',
    [0x33]=';', [0x34]='\'', [0x35]='`', [0x36]=',', [0x37]='.', [0x38]='/',
};

static void kbd_push(char c) {
    uint32_t next = (g_kbd_head + 1) % 64;
    if (next == g_kbd_tail) return; /* full */
    g_kbd_queue[g_kbd_head] = c;
    g_kbd_head = next;
}

bool xhci_kbd_has_data(void) {
    return g_kbd_head != g_kbd_tail;
}
char xhci_kbd_getc(void) {
    if (g_kbd_head == g_kbd_tail) return 0;
    char c = g_kbd_queue[g_kbd_tail];
    g_kbd_tail = (g_kbd_tail + 1) % 64;
    return c;
}

/* =============================================================================
 *  Port enumeráció és device address flow
 * ========================================================================== */

/* Forward: complete_device_setup a fájl alsóbb részén van definiálva. */
static bool complete_device_setup(usb_device_t *d);

/* 1-indexed port. Visszaad true-t, ha a port enabled és device-csatlakoztatott. */
static bool port_reset(int port1) {
    uint32_t off = 0x400 + (port1 - 1) * 0x10;
    uint32_t portsc = mmio_r32(g_xhci.op, off + PORT_SC);
    if (!(portsc & PORTSC_CCS)) return false;

    /* PORTSC-ben az RW1C bitek: CSC, PEC, PRC. A PED bitet NE írjuk.
     * Bit 5-9 (PLS/PP) rendesen megmarad. Write-back + PR. */
    uint32_t write = (portsc & 0x0E00FFE1u) | PORTSC_PR
                   | PORTSC_CSC | PORTSC_PEC | PORTSC_PRC;
    mmio_w32(g_xhci.op, off + PORT_SC, write);

    /* Várunk PRC-re (Port Reset Change) */
    for (int i = 0; i < 500000; i++) {
        portsc = mmio_r32(g_xhci.op, off + PORT_SC);
        if (portsc & PORTSC_PRC) break;
    }
    /* Töröljük a change biteket */
    mmio_w32(g_xhci.op, off + PORT_SC,
             (portsc & 0x0E00FFE1u) | PORTSC_CSC | PORTSC_PEC | PORTSC_PRC);

    usb_udelay(10000);
    portsc = mmio_r32(g_xhci.op, off + PORT_SC);
    if (!(portsc & PORTSC_PED)) {
        kprintf("[xhci] port %d: reset done but PED=0 (PORTSC=0x%x)\n",
                port1, portsc);
        return false;
    }
    kprintf("[xhci] port %d: enabled after reset (PORTSC=0x%x)\n",
            port1, portsc);
    return true;
}

/* Context méret: 32 vagy 64 bájt. Input Context = Input Control Context (1) +
 * Slot Context (1) + 31 Endpoint Context = 33 * ctx_size. */
static uint32_t ctx_size(void) { return g_xhci.context_size; }

/* Egy device enumerálása: reset + enable slot + address + get descriptors +
 * configure HID interrupt IN endpoint. */
static bool enumerate_device(usb_device_t *d) {
    /* 1. Port reset */
    if (!port_reset(d->port)) return false;

    /* 2. Port Speed elolvasása */
    uint32_t portsc = mmio_r32(g_xhci.op,
                               0x400 + (d->port - 1) * 0x10 + PORT_SC);
    uint32_t speed  = (portsc >> 10) & 0x0F;
    /* speed: 1=Full, 2=Low, 3=High, 4=Super */

    /* 3. Enable Slot command */
    cmd_submit(0, 0, 0, (TRB_ENABLE_SLOT << 10));
    uint32_t slot_id = 0, code = 0;
    if (cmd_wait_completion(&slot_id, &code) < 0 || code != 1) {
        kprintf("[xhci] Enable Slot failed (code=%u)\n", code);
        return false;
    }
    d->slot_id = (uint8_t)slot_id;
    kprintf("[xhci] port %d slot=%u speed=%u\n", d->port, slot_id, speed);

    /* 4. Allokáljunk Input + Device Context-et (4 KB-ra igazítva) */
    uint32_t cs = ctx_size();
    d->in_ctx_phys  = pmm_alloc_frame();
    d->dev_ctx_phys = pmm_alloc_frame();
    if (!d->in_ctx_phys || !d->dev_ctx_phys) return false;
    uint8_t *in_ctx  = (uint8_t *)phys_to_virt(d->in_ctx_phys);
    uint8_t *dev_ctx = (uint8_t *)phys_to_virt(d->dev_ctx_phys);
    for (uint32_t i = 0; i < 4096; i++) { in_ctx[i] = 0; dev_ctx[i] = 0; }

    /* DCBAA-ba berakjuk a device context-et */
    g_xhci.dcbaa[d->slot_id] = d->dev_ctx_phys;

    /* 5. Input Control Context (first cs bytes): Add Context flags A0 és A1 */
    *(uint32_t *)(in_ctx + 0x00) = 0;         /* Drop flags */
    *(uint32_t *)(in_ctx + 0x04) = 0x00000003; /* Add A0 (slot) + A1 (EP0) */

    /* 6. Slot Context (in_ctx + cs) */
    uint8_t *slot_ctx = in_ctx + cs;
    /* DW0: context entries = 1 (csak EP0), speed */
    *(uint32_t *)(slot_ctx + 0x00) =
        (1u << 27) | (speed << 20);
    /* DW1: Root Hub Port = d->port (1-indexed) */
    *(uint32_t *)(slot_ctx + 0x04) = ((uint32_t)d->port << 16);

    /* 7. EP0 Context (in_ctx + 2*cs) */
    uint8_t *ep0 = in_ctx + 2 * cs;
    /* Max Packet Size függ a speedtől */
    uint16_t mps = 8;
    if      (speed == 4) mps = 512;  /* Super */
    else if (speed == 3) mps = 64;   /* High */
    else if (speed == 1) mps = 64;   /* Full */
    else                 mps = 8;    /* Low */
    /* DW0: EP state=0, Mult=0, LSA=0, Interval=0, ... */
    *(uint32_t *)(ep0 + 0x00) = 0;
    /* DW1: EP Type = 4 (Control Bidirectional), CErr=3, MPS */
    *(uint32_t *)(ep0 + 0x04) =
        (4u << 3) | (3u << 1) | ((uint32_t)mps << 16);

    /* 8. EP0 TRB Ring allokálása */
    uintptr_t ep0_ring_phys = pmm_alloc_frame();
    xhci_trb_t *ep0_ring = (xhci_trb_t *)phys_to_virt(ep0_ring_phys);
    for (int i = 0; i < RING_TRB_COUNT; i++) {
        ep0_ring[i].param_lo = 0; ep0_ring[i].param_hi = 0;
        ep0_ring[i].status = 0;   ep0_ring[i].control = 0;
    }
    /* Link TRB az ep0 ring végén */
    ep0_ring[RING_TRB_COUNT - 1].param_lo = (uint32_t)(ep0_ring_phys & 0xFFFFFFFF);
    ep0_ring[RING_TRB_COUNT - 1].param_hi = (uint32_t)((uint64_t)ep0_ring_phys >> 32);
    ep0_ring[RING_TRB_COUNT - 1].control  = (TRB_LINK << 10) | (1u << 1);

    /* TR Dequeue Ptr az EP0 context DW2-DW3 */
    *(uint64_t *)(ep0 + 0x08) = ep0_ring_phys | 0x1;  /* DCS = 1 */

    /* Használat céljára elmentjük az EP0 ring pointert is */
    d->ep_ring_phys = ep0_ring_phys;
    d->ep_ring      = ep0_ring;
    d->ep_enqueue   = 0;
    d->ep_cycle     = 1;

    /* 9. Address Device command */
    cmd_submit((uint32_t)(d->in_ctx_phys & 0xFFFFFFFF),
               (uint32_t)((uint64_t)d->in_ctx_phys >> 32),
               0,
               (TRB_ADDRESS_DEVICE << 10) | ((uint32_t)d->slot_id << 24));
    uint32_t ad_code = 0;
    if (cmd_wait_completion(NULL, &ad_code) < 0 || ad_code != 1) {
        kprintf("[xhci] Address Device failed slot=%u code=%u\n",
                d->slot_id, ad_code);
        return false;
    }
    kprintf("[xhci] slot %u addressed\n", d->slot_id);

    d->present = true;

    /* A device most már rendelkezik default (0-ás) címmel -> folytatjuk:
     * GET_DESCRIPTOR (dev, config), SET_CONFIG, Configure EP, HID poll. */
    if (!complete_device_setup(d)) {
        kprintf("[xhci] slot %u: device setup failed — ignoring\n", d->slot_id);
        d->present = false;
        return false;
    }
    return true;
}

void xhci_enumerate_ports(void) {
    if (!g_xhci.running) return;

    /* HC reset után a portok detektációnak ideje kell */
    usb_udelay(200000);  /* ~200 ms */

    int ndevs = 0;
    for (int p = 1; p <= g_xhci.max_ports; p++) {
        uint32_t portsc = mmio_r32(g_xhci.op, 0x400 + (p - 1) * 0x10);
        kprintf("[xhci] port %d: PORTSC=0x%x CCS=%d PED=%d\n",
                p, portsc, (portsc & PORTSC_CCS) ? 1 : 0,
                (portsc & PORTSC_PED) ? 1 : 0);
        if (!(portsc & PORTSC_CCS)) continue;
        if (ndevs >= MAX_USB_DEVICES) break;

        usb_device_t *d = &g_devs[ndevs];
        for (uint32_t i = 0; i < sizeof(*d); i++) ((uint8_t*)d)[i] = 0;
        d->port = (uint8_t)p;

        if (enumerate_device(d)) {
            ndevs++;
        }
    }
    kprintf("[xhci] enumerated %d device(s)\n", ndevs);
}

/* =============================================================================
 *  Control transfer (EP0)
 * ========================================================================== */

/* Submit egy TRB-t egy device specifikus ring-re (EP0 vagy HID interrupt). */
static void ring_submit(usb_device_t *d, uint32_t p0, uint32_t p1,
                        uint32_t st, uint32_t ctl_type_flags) {
    xhci_trb_t *trb = &d->ep_ring[d->ep_enqueue];
    trb->param_lo = p0;
    trb->param_hi = p1;
    trb->status   = st;
    trb->control  = ctl_type_flags | d->ep_cycle;

    d->ep_enqueue++;
    if (d->ep_enqueue >= RING_TRB_COUNT - 1) {
        xhci_trb_t *link = &d->ep_ring[RING_TRB_COUNT - 1];
        link->control = (TRB_LINK << 10) | (1u << 1) | d->ep_cycle;
        d->ep_enqueue = 0;
        d->ep_cycle ^= 1;
    }
}

/* Várakozás Transfer Event-re a cél slot+ep-re, timeout esetén -1. */
static int wait_transfer_event(uint32_t slot_id, uint32_t *out_code) {
    for (int i = 0; i < 2000000; i++) {
        xhci_trb_t *ev = &g_xhci.evt_ring[g_xhci.evt_dequeue];
        uint32_t ctl = ev->control;
        if ((ctl & 1) != g_xhci.evt_cycle) { usb_udelay(10); continue; }
        uint32_t type = (ctl >> 10) & 0x3F;

        /* Fogyasszuk el */
        g_xhci.evt_dequeue++;
        if (g_xhci.evt_dequeue >= RING_TRB_COUNT) {
            g_xhci.evt_dequeue = 0;
            g_xhci.evt_cycle ^= 1;
        }
        uint64_t erdp = g_xhci.evt_ring_phys + g_xhci.evt_dequeue * TRB_SIZE;
        mmio_w64(g_xhci.rt, RT_IR0 + IR_ERDP, erdp | (1ULL << 3));

        if (type == TRB_EVENT_TRANSFER) {
            uint32_t slot = (ctl >> 24) & 0xFF;
            uint32_t code = (ev->status >> 24) & 0xFF;
            if (slot != slot_id) continue;
            if (out_code) *out_code = code;
            return (int)code;
        }
    }
    return -1;
}

/* Control transfer: IN (device->host) csomag. bmRequestType=0xA0/0x80/..., length max 256. */
static int control_in(usb_device_t *d, uint8_t req_type, uint8_t req,
                      uint16_t value, uint16_t index, uint16_t len,
                      void *buf_phys_and_virt) {
    /* Setup Stage TRB */
    uint32_t setup_ctl = (TRB_SETUP_STAGE << 10)
                       | (1u << 6)    /* IDT: immediate data */
                       | (3u << 16);  /* TRT = 3 (IN Data Stage) */
    uint32_t setup_p0 = (uint32_t)req_type | ((uint32_t)req << 8)
                      | ((uint32_t)value << 16);
    uint32_t setup_p1 = (uint32_t)index | ((uint32_t)len << 16);
    /* Setup status = transfer length (8 bytes, fix) */
    ring_submit(d, setup_p0, setup_p1, 8, setup_ctl);

    /* Data Stage TRB */
    uintptr_t buf_phys = (uintptr_t)buf_phys_and_virt;
    uint32_t data_ctl = (TRB_DATA_STAGE << 10)
                      | (1u << 16);  /* DIR = IN */
    ring_submit(d,
                (uint32_t)(buf_phys & 0xFFFFFFFF),
                (uint32_t)((uint64_t)buf_phys >> 32),
                len,           /* Transfer length */
                data_ctl);

    /* Status Stage TRB: IOC bit, opposite dir */
    uint32_t status_ctl = (TRB_STATUS_STAGE << 10)
                        | (1u << 5);   /* IOC */
    /* Status Stage direction: opposite of data, OUT ha data IN volt */
    ring_submit(d, 0, 0, 0, status_ctl);

    /* Doorbell: EP0 = DCI 1 */
    g_xhci.dbells[d->slot_id] = 1;

    /* Várakozás transfer event-re */
    uint32_t code = 0;
    int r = wait_transfer_event(d->slot_id, &code);
    if (r < 0) return -1;
    if (code != 1 && code != 13) { /* Success = 1, Short Packet = 13 */
        kprintf("[xhci] control_in failed code=%u\n", code);
        return -1;
    }
    return 0;
}

/* USB descriptor típusok */
#define USB_DESC_DEVICE         1
#define USB_DESC_CONFIG         2
#define USB_DESC_INTERFACE      4
#define USB_DESC_ENDPOINT       5

/* USB class requests */
#define USB_REQ_GET_DESCRIPTOR  6
#define USB_REQ_SET_ADDRESS     5
#define USB_REQ_SET_CONFIG      9

/* =============================================================================
 *  HID osztály detektálása + endpoint beállítás
 * ========================================================================== */

static bool parse_config_desc(usb_device_t *d, uint8_t *buf, int total_len) {
    /* Végigjárjuk a descriptor sorozatot */
    int pos = 0;
    bool hid_iface = false;
    while (pos + 2 <= total_len) {
        uint8_t dlen = buf[pos];
        uint8_t dtype = buf[pos + 1];
        if (dlen == 0) break;
        if (dtype == USB_DESC_INTERFACE && dlen >= 9) {
            /* bInterfaceClass = buf[pos+5], SubClass = buf[pos+6], Protocol = buf[pos+7] */
            uint8_t cls  = buf[pos + 5];
            uint8_t sub  = buf[pos + 6];
            uint8_t prot = buf[pos + 7];
            if (cls == 3 /* HID */) {
                hid_iface = true;
                d->iface_class = cls;
                d->iface_sub   = sub;
                d->iface_proto = prot;
            } else {
                hid_iface = false;
            }
        } else if (dtype == USB_DESC_ENDPOINT && dlen >= 7 && hid_iface) {
            uint8_t addr = buf[pos + 2];
            uint8_t attr = buf[pos + 3];
            uint16_t mps = (uint16_t)buf[pos + 4] | ((uint16_t)buf[pos + 5] << 8);
            uint8_t interval = buf[pos + 6];
            /* Interrupt IN endpoint */
            if ((attr & 0x3) == 0x3 /* interrupt */ && (addr & 0x80)) {
                d->ep_addr     = addr;
                d->ep_mps      = mps;
                d->ep_interval = interval;
                return true;
            }
        }
        pos += dlen;
    }
    return false;
}

/* Beállít egy HID interrupt IN endpointot (DCI = 2*ep_num + 1 IN). */
static bool configure_hid_endpoint(usb_device_t *d) {
    uint8_t ep_num = d->ep_addr & 0x0F;
    uint32_t dci = ep_num * 2 + 1;  /* IN direction */

    /* Új TRB ring a HID endpointhoz (EP0 ringet nem használjuk itt) */
    uintptr_t ring_phys = pmm_alloc_frame();
    if (!ring_phys) return false;
    xhci_trb_t *ring = (xhci_trb_t *)phys_to_virt(ring_phys);
    for (int i = 0; i < RING_TRB_COUNT; i++) {
        ring[i].param_lo = 0; ring[i].param_hi = 0;
        ring[i].status = 0;   ring[i].control = 0;
    }
    ring[RING_TRB_COUNT - 1].param_lo = (uint32_t)(ring_phys & 0xFFFFFFFF);
    ring[RING_TRB_COUNT - 1].param_hi = (uint32_t)((uint64_t)ring_phys >> 32);
    ring[RING_TRB_COUNT - 1].control  = (TRB_LINK << 10) | (1u << 1);

    /* Input Context már létezik (d->in_ctx_phys). Újra használjuk. */
    uint32_t cs = ctx_size();
    uint8_t *in_ctx = (uint8_t *)phys_to_virt(d->in_ctx_phys);
    /* Reset input control context */
    *(uint32_t *)(in_ctx + 0x00) = 0;                    /* Drop */
    *(uint32_t *)(in_ctx + 0x04) = (1u << dci) | 0x1;    /* Add A0 + HID EP */

    /* Slot Context: frissíteni a context entries számot */
    uint8_t *slot_ctx = in_ctx + cs;
    uint32_t slot_dw0 = *(uint32_t *)(slot_ctx + 0x00);
    slot_dw0 = (slot_dw0 & ~(0x1Fu << 27)) | (dci << 27);
    *(uint32_t *)(slot_ctx + 0x00) = slot_dw0;

    /* HID Endpoint Context = in_ctx + (dci + 1) * cs */
    uint8_t *ep = in_ctx + (dci + 1) * cs;
    for (uint32_t i = 0; i < cs; i++) ep[i] = 0;
    /* DW0: Interval: USB interrupt endpoints: pow2 encoding.
     * Interrupt EP at Full/Low Speed uses bInterval in ms directly,
     * at High/Super speed uses 2^(bInterval-1) * 125us.
     * Egyszerűsítve: 8 = 1 ms periódus HS-en. */
    uint8_t interval = 8;
    *(uint32_t *)(ep + 0x00) = ((uint32_t)interval << 16);
    /* DW1: EP Type = 7 (Interrupt IN), CErr=3, MPS */
    *(uint32_t *)(ep + 0x04) =
        (7u << 3) | (3u << 1) | ((uint32_t)d->ep_mps << 16);
    /* DW2,3: TR Dequeue Ptr + DCS=1 */
    *(uint64_t *)(ep + 0x08) = ring_phys | 0x1;
    /* DW4: Average TRB Length = MPS */
    *(uint32_t *)(ep + 0x10) = d->ep_mps;

    /* Configure Endpoint parancs */
    cmd_submit((uint32_t)(d->in_ctx_phys & 0xFFFFFFFF),
               (uint32_t)((uint64_t)d->in_ctx_phys >> 32),
               0,
               (TRB_CONFIGURE_ENDPOINT << 10) | ((uint32_t)d->slot_id << 24));
    uint32_t code = 0;
    if (cmd_wait_completion(NULL, &code) < 0 || code != 1) {
        kprintf("[xhci] Configure EP failed slot=%u code=%u\n",
                d->slot_id, code);
        return false;
    }

    /* Cseréljük a d->ep_ring-et a HID ringre (EP0 nem kell többé) */
    d->ep_ring_phys = ring_phys;
    d->ep_ring      = ring;
    d->ep_enqueue   = 0;
    d->ep_cycle     = 1;

    /* Report buffer */
    d->report_phys = pmm_alloc_frame();
    d->report      = (uint8_t *)phys_to_virt(d->report_phys);
    for (uint32_t i = 0; i < 64; i++) d->report[i] = 0;

    kprintf("[xhci] slot %u: HID IN EP 0x%x MPS=%u DCI=%u configured\n",
            d->slot_id, d->ep_addr, d->ep_mps, dci);
    return true;
}

/* Egyetlen interrupt IN transfer indítása (Normal TRB) */
static void hid_queue_transfer(usb_device_t *d) {
    uint32_t ctl = (TRB_NORMAL << 10) | (1u << 5);  /* IOC */
    ring_submit(d,
                (uint32_t)(d->report_phys & 0xFFFFFFFF),
                (uint32_t)((uint64_t)d->report_phys >> 32),
                8,   /* length */
                ctl);
    /* Doorbell = DCI of HID IN EP */
    uint8_t ep_num = d->ep_addr & 0x0F;
    uint32_t dci = ep_num * 2 + 1;
    g_xhci.dbells[d->slot_id] = dci;
}

/* Fő device setup: eddig addressed, még nem konfigurált */
static bool complete_device_setup(usb_device_t *d) {
    /* 1. GET_DESCRIPTOR Device (8 bájt is elég a bMaxPacketSize0-hoz) */
    uintptr_t buf_phys = pmm_alloc_frame();
    uint8_t  *buf      = (uint8_t *)phys_to_virt(buf_phys);
    for (int i = 0; i < 64; i++) buf[i] = 0;

    if (control_in(d, 0x80, USB_REQ_GET_DESCRIPTOR,
                   (USB_DESC_DEVICE << 8), 0, 18, (void *)buf_phys) < 0) {
        kprintf("[xhci] slot %u: GET_DESCRIPTOR(DEV) fail\n", d->slot_id);
        return false;
    }
    d->dev_class = buf[4];
    kprintf("[xhci] slot %u: dev desc class=%u vendor=%04x product=%04x\n",
            d->slot_id, buf[4],
            buf[8] | (buf[9] << 8), buf[10] | (buf[11] << 8));

    /* 2. GET_DESCRIPTOR Config (első 9 bájt -> totalLength) */
    if (control_in(d, 0x80, USB_REQ_GET_DESCRIPTOR,
                   (USB_DESC_CONFIG << 8), 0, 9, (void *)buf_phys) < 0) {
        kprintf("[xhci] slot %u: GET_DESC(CONFIG) short fail\n", d->slot_id);
        return false;
    }
    uint16_t total_len = buf[2] | ((uint16_t)buf[3] << 8);
    uint8_t  config_val = buf[5];
    if (total_len > 4096) total_len = 4096;

    /* 3. GET_DESCRIPTOR Config teljes hossz */
    if (control_in(d, 0x80, USB_REQ_GET_DESCRIPTOR,
                   (USB_DESC_CONFIG << 8), 0, total_len,
                   (void *)buf_phys) < 0) {
        kprintf("[xhci] slot %u: GET_DESC(CONFIG) full fail\n", d->slot_id);
        return false;
    }

    /* 4. Parse -> HID interface + interrupt IN endpoint */
    if (!parse_config_desc(d, buf, total_len)) {
        kprintf("[xhci] slot %u: no HID interrupt IN endpoint\n", d->slot_id);
        return false;
    }
    kprintf("[xhci] slot %u: HID proto=%u (1=kbd, 2=mouse), EP=0x%x MPS=%u\n",
            d->slot_id, d->iface_proto, d->ep_addr, d->ep_mps);

    /* 5. SET_CONFIGURATION */
    /* SET_CONFIG használata control OUT, de egyszerűsítve a control_in-t
     * csak data IN-re írtuk. Ehelyett: Setup Stage (TRT=0), Status Stage (IN).
     * Mivel egyszerű: saját verzió. */
    {
        uint32_t setup_ctl = (TRB_SETUP_STAGE << 10)
                           | (1u << 6)    /* IDT */
                           | (0u << 16);  /* TRT = 0 (no data) */
        uint32_t setup_p0 = (uint32_t)0x00 | ((uint32_t)USB_REQ_SET_CONFIG << 8)
                          | ((uint32_t)config_val << 16);
        uint32_t setup_p1 = 0;
        ring_submit(d, setup_p0, setup_p1, 8, setup_ctl);

        /* Status Stage IN + IOC */
        uint32_t status_ctl = (TRB_STATUS_STAGE << 10)
                            | (1u << 16)   /* DIR = IN */
                            | (1u << 5);   /* IOC */
        ring_submit(d, 0, 0, 0, status_ctl);
        g_xhci.dbells[d->slot_id] = 1;  /* EP0 */

        uint32_t code = 0;
        if (wait_transfer_event(d->slot_id, &code) < 0 || code != 1) {
            kprintf("[xhci] slot %u: SET_CONFIG fail code=%u\n",
                    d->slot_id, code);
            return false;
        }
    }

    /* 6. Configure Endpoint + HID interrupt ring */
    if (!configure_hid_endpoint(d)) return false;

    /* 7. Első interrupt IN kérés queue-olása */
    hid_queue_transfer(d);

    return true;
}

/* =============================================================================
 *  HID report feldolgozás
 * ========================================================================== */

static void hid_handle_kbd(usb_device_t *d) {
    /* Boot kbd report: 8 bájt
     *  [0] modifier bits, [1] reserved, [2..7] keycodes. */
    uint8_t *r = d->report;
    /* ASCII-re konvertálás mindegyik új lenyomott billentyűhöz */
    uint8_t mod = r[0];
    bool shift = (mod & 0x22) != 0;
    for (int i = 2; i < 8; i++) {
        uint8_t kc = r[i];
        if (kc == 0) continue;
        /* Ha korábban is le volt nyomva, skip */
        bool was_down = false;
        for (int j = 0; j < 6; j++) if (g_prev_kbd_keys[j] == kc) was_down = true;
        if (was_down) continue;
        if (kc < 128) {
            char c = hid_to_ascii[kc];
            if (c) {
                if (shift && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
                kbd_push(c);
            }
        }
    }
    for (int i = 0; i < 6; i++) g_prev_kbd_keys[i] = r[2 + i];
}

static void hid_handle_mouse(usb_device_t *d) {
    /* Boot mouse report: 3 bájt (vagy 4 scroll-lal)
     *  [0] buttons, [1] dx (signed), [2] dy (signed). */
    uint8_t *r = d->report;
    g_mouse_buttons = r[0];
    g_mouse_dx += (int8_t)r[1];
    g_mouse_dy += (int8_t)r[2];
}

int xhci_mouse_take(int *dx, int *dy, uint8_t *buttons) {
    if (dx) *dx = g_mouse_dx;
    if (dy) *dy = g_mouse_dy;
    if (buttons) *buttons = g_mouse_buttons;
    int had = (g_mouse_dx != 0 || g_mouse_dy != 0) ? 1 : 0;
    g_mouse_dx = 0;
    g_mouse_dy = 0;
    return had;
}

/* Event ring polling: ha Transfer Event érkezik, feldolgozzuk és újra queue-olunk */
void xhci_poll(void) {
    if (!g_xhci.running) return;

    for (int iter = 0; iter < 32; iter++) {
        xhci_trb_t *ev = &g_xhci.evt_ring[g_xhci.evt_dequeue];
        uint32_t ctl = ev->control;
        if ((ctl & 1) != g_xhci.evt_cycle) return;  /* Nincs több event */

        uint32_t type = (ctl >> 10) & 0x3F;
        if (type == TRB_EVENT_TRANSFER) {
            uint32_t slot = (ctl >> 24) & 0xFF;
            uint32_t ep   = (ctl >> 16) & 0x1F;
            /* Megkeressük a device-ot */
            for (int i = 0; i < MAX_USB_DEVICES; i++) {
                usb_device_t *d = &g_devs[i];
                if (!d->present || d->slot_id != slot) continue;
                uint8_t ep_num = d->ep_addr & 0x0F;
                uint32_t dci = ep_num * 2 + 1;
                if (ep != dci) continue;

                /* Feldolgozzuk a reportot */
                if (d->iface_proto == 1) hid_handle_kbd(d);
                else if (d->iface_proto == 2) hid_handle_mouse(d);

                /* Újra queue-olunk egy transfer-t */
                hid_queue_transfer(d);
                break;
            }
        }

        /* Dequeue advance */
        g_xhci.evt_dequeue++;
        if (g_xhci.evt_dequeue >= RING_TRB_COUNT) {
            g_xhci.evt_dequeue = 0;
            g_xhci.evt_cycle ^= 1;
        }
        uint64_t erdp = g_xhci.evt_ring_phys + g_xhci.evt_dequeue * TRB_SIZE;
        mmio_w64(g_xhci.rt, RT_IR0 + IR_ERDP, erdp | (1ULL << 3));
    }
}
