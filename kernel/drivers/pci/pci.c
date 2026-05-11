/* Rex OS - PCI bus enumeration via legacy port I/O.
 *
 * Algoritmus:
 *   - Iterálunk bus 0..255, dev 0..31, func 0..7.
 *   - Minden (bus,dev,0)-ra vendor != 0xFFFF teszt.
 *   - Ha header_type & 0x80, akkor multifunction -> func 1..7 is.
 *   - Bridge-eket (class=0x06, subclass=0x04) NEM követjük rekurzívan
 *     (egyszerű flat enumeration), de a busz-szám iteráció a 0..255
 *     tartományban így is megtalálja a bridge mögötti device-okat,
 *     ha a firmware már beprogramozta a sec/sub bus regisztereket
 *     (QEMU mindig így csinálja).
 */

#include <drivers/pci/pci.h>
#include <rexos/io.h>
#include <lib/printf.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_MAX_DEVICES 64

static pci_device_t s_devices[PCI_MAX_DEVICES];
static size_t       s_count = 0;

static inline uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    return (1u << 31)
         | ((uint32_t)bus  << 16)
         | ((uint32_t)dev  << 11)
         | ((uint32_t)func << 8)
         | ((uint32_t)off  & 0xFC);
}

uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, off));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t v = pci_cfg_read32(bus, dev, func, off & 0xFC);
    return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFF);
}

uint8_t pci_cfg_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t v = pci_cfg_read32(bus, dev, func, off & 0xFC);
    return (uint8_t)((v >> ((off & 3) * 8)) & 0xFF);
}

void pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, off));
    outl(PCI_CONFIG_DATA, val);
}

static const char *pci_class_name(uint8_t cls, uint8_t sub) {
    switch (cls) {
        case 0x00: return "Unclassified";
        case 0x01:
            switch (sub) {
                case 0x01: return "IDE Controller";
                case 0x06: return "SATA AHCI";
                case 0x08: return "NVMe";
                default:   return "Mass Storage";
            }
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x06:
            switch (sub) {
                case 0x00: return "Host bridge";
                case 0x01: return "ISA bridge";
                case 0x04: return "PCI-PCI bridge";
                default:   return "Bridge";
            }
        case 0x0C:
            switch (sub) {
                case 0x03: return "USB controller";
                default:   return "Serial bus";
            }
        default: return "Other";
    }
}

static void pci_register(uint8_t bus, uint8_t dev, uint8_t func) {
    if (s_count >= PCI_MAX_DEVICES) return;

    uint32_t id = pci_cfg_read32(bus, dev, func, 0x00);
    uint16_t vendor = (uint16_t)(id & 0xFFFF);
    if (vendor == PCI_VENDOR_INVALID) return;

    uint32_t cls_reg = pci_cfg_read32(bus, dev, func, 0x08);
    uint32_t hdr_reg = pci_cfg_read32(bus, dev, func, 0x0C);

    pci_device_t *p = &s_devices[s_count++];
    p->bus = bus; p->dev = dev; p->func = func;
    p->vendor      = vendor;
    p->device      = (uint16_t)(id >> 16);
    p->revision    = (uint8_t)(cls_reg & 0xFF);
    p->prog_if     = (uint8_t)((cls_reg >> 8) & 0xFF);
    p->subclass    = (uint8_t)((cls_reg >> 16) & 0xFF);
    p->class_code  = (uint8_t)((cls_reg >> 24) & 0xFF);
    p->header_type = (uint8_t)((hdr_reg >> 16) & 0xFF);

    kprintf("[pci] %02x:%02x.%x %04x:%04x class=%02x:%02x:%02x %s\n",
            bus, dev, func, p->vendor, p->device,
            p->class_code, p->subclass, p->prog_if,
            pci_class_name(p->class_code, p->subclass));
}

static void pci_check_function(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t vendor = pci_cfg_read16(bus, dev, func, 0x00);
    if (vendor == PCI_VENDOR_INVALID) return;
    pci_register(bus, dev, func);
}

void pci_init(void) {
    s_count = 0;
    kprintf("[pci] scanning bus 0..255 ...\n");

    /* Egyszerű, de elegendő scan: minden bus, minden slot, minden func. */
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_cfg_read16(bus, dev, 0, 0x00);
            if (vendor == PCI_VENDOR_INVALID) continue;
            pci_check_function(bus, dev, 0);

            uint8_t hdr = pci_cfg_read8(bus, dev, 0, 0x0E);
            if (hdr & 0x80) {
                /* multifunction */
                for (int f = 1; f < 8; f++) {
                    pci_check_function(bus, dev, f);
                }
            }
        }
    }

    kprintf("[pci] %lu device(s) detected\n", (unsigned long)s_count);
}

const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    for (size_t i = 0; i < s_count; i++) {
        const pci_device_t *p = &s_devices[i];
        if (p->class_code != class_code) continue;
        if (p->subclass   != subclass)   continue;
        if (prog_if != 0xFF && p->prog_if != prog_if) continue;
        return p;
    }
    return NULL;
}

uint64_t pci_get_bar(const pci_device_t *d, int bar) {
    if (bar < 0 || bar > 5) return 0;
    uint8_t off = 0x10 + 4 * bar;
    uint32_t lo = pci_cfg_read32(d->bus, d->dev, d->func, off);
    if (lo & 0x1) {
        /* I/O space BAR */
        return (uint64_t)(lo & 0xFFFFFFFCu);
    }
    /* Memory BAR */
    uint32_t type = (lo >> 1) & 0x3;
    uint64_t addr = lo & 0xFFFFFFF0u;
    if (type == 0x2 && bar < 5) {
        /* 64 bites BAR — a következő DWORD a magasabb 32 bit */
        uint32_t hi = pci_cfg_read32(d->bus, d->dev, d->func, off + 4);
        addr |= ((uint64_t)hi) << 32;
    }
    return addr;
}

void pci_enable_busmaster(const pci_device_t *d) {
    uint32_t cmd = pci_cfg_read32(d->bus, d->dev, d->func, 0x04);
    /* Bit 0: I/O space, bit 1: memory space, bit 2: bus master */
    cmd |= (1 << 0) | (1 << 1) | (1 << 2);
    pci_cfg_write32(d->bus, d->dev, d->func, 0x04, cmd);
}

size_t pci_device_count(void) { return s_count; }

const pci_device_t *pci_device_at(size_t i) {
    if (i >= s_count) return NULL;
    return &s_devices[i];
}
