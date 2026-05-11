/* Rex OS - PCI bus enumeration & config space access
 *
 * Klasszikus port-alapú PCI config space (0xCF8 / 0xCFC).
 * MMCONFIG (PCIe ECAM) később jöhet ACPI MCFG táblával.
 */
#pragma once
#include <rexos/types.h>

#define PCI_VENDOR_INVALID 0xFFFF

/* Egy detektált PCI eszköz */
typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t vendor;
    uint16_t device;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
} pci_device_t;

void pci_init(void);

/* Config space olvasás/írás. */
uint8_t  pci_cfg_read8 (uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void     pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);

/* Megkeresi az első olyan eszközt, ahol (class,subclass,prog_if) egyezik.
 * prog_if = 0xFF wildcard. NULL ha nincs ilyen. */
const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if);

/* Visszaadja egy adott BAR (0..5) tartalmát, low+high egyesítve 64 bites cím
 * esetén. A flag biteket maszkoljuk. */
uint64_t pci_get_bar(const pci_device_t *d, int bar);

/* Bus mastering bekapcsolása + memory space enable (Command register). */
void pci_enable_busmaster(const pci_device_t *d);

/* Listázás debug céllal */
size_t pci_device_count(void);
const pci_device_t *pci_device_at(size_t i);
