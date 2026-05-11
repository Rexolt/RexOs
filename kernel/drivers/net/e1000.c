#include <arch/x86_64/irq.h>
#include <drivers/net/e1000.h>
#include <drivers/pci/pci.h>
#include <lib/printf.h>
#include <lib/string.h>
#include <mm/pmm.h>
#include <mm/vmm.h>

#define E1000_VEND 0x8086
#define E1000_DEV_100E 0x100E /* Qemu default E1000 */
#define E1000_DEV_10D3 0x10D3 /* Intel Gigabit CT Desktop Adapter */

static const pci_device_t *s_e1000_pci = NULL;
static uint8_t *s_mmio_base = NULL;
static net_device_t s_net_dev;

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

static e1000_rx_desc_t *rx_descs;
static e1000_tx_desc_t *tx_descs;
static uint8_t *rx_buffers[E1000_NUM_RX_DESC];
static uint16_t rx_cur;
static uint16_t tx_cur;

static uint32_t e1000_read_reg(uint16_t reg) {
  if (!s_mmio_base)
    return 0;
  return *(volatile uint32_t *)(s_mmio_base + reg);
}

static void e1000_write_reg(uint16_t reg, uint32_t value) {
  if (!s_mmio_base)
    return;
  *(volatile uint32_t *)(s_mmio_base + reg) = value;
}

static uint16_t e1000_eeprom_read(uint8_t addr) {
  uint32_t temp = 0;
  e1000_write_reg(E1000_REG_EEPROM, 1 | ((uint32_t)addr << 8));
  /* Timeout: ne ragadjon bele végtelen ciklusba */
  for (int i = 0; i < 1000000; i++) {
    temp = e1000_read_reg(E1000_REG_EEPROM);
    if (temp & (1 << 4))
      break;
  }
  return (uint16_t)((temp >> 16) & 0xFFFF);
}

static void e1000_read_mac(void) {
  /* Először próbáljuk az EEPROM-ot */
  uint16_t t0 = e1000_eeprom_read(0);
  uint16_t t1 = e1000_eeprom_read(1);
  uint16_t t2 = e1000_eeprom_read(2);

  /* Ha az EEPROM nem adott valid adatot (mind null vagy 0xFF), */
  /* akkor olvassuk ki a RAL/RAH regiszterekből (ezek a QEMU-ban működnek) */
  if ((t0 | t1 | t2) == 0 || (t0 == 0xFFFF && t1 == 0xFFFF)) {
    uint32_t ral = e1000_read_reg(E1000_REG_RAL);
    uint32_t rah = e1000_read_reg(E1000_REG_RAH);
    s_net_dev.mac.mac[0] = (ral >> 0) & 0xFF;
    s_net_dev.mac.mac[1] = (ral >> 8) & 0xFF;
    s_net_dev.mac.mac[2] = (ral >> 16) & 0xFF;
    s_net_dev.mac.mac[3] = (ral >> 24) & 0xFF;
    s_net_dev.mac.mac[4] = (rah >> 0) & 0xFF;
    s_net_dev.mac.mac[5] = (rah >> 8) & 0xFF;
    kprintf("[e1000] MAC from RAL/RAH registers\n");
  } else {
    s_net_dev.mac.mac[0] = t0 & 0xFF;
    s_net_dev.mac.mac[1] = t0 >> 8;
    s_net_dev.mac.mac[2] = t1 & 0xFF;
    s_net_dev.mac.mac[3] = t1 >> 8;
    s_net_dev.mac.mac[4] = t2 & 0xFF;
    s_net_dev.mac.mac[5] = t2 >> 8;
  }

  kprintf("[e1000] MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
          s_net_dev.mac.mac[0], s_net_dev.mac.mac[1], s_net_dev.mac.mac[2],
          s_net_dev.mac.mac[3], s_net_dev.mac.mac[4], s_net_dev.mac.mac[5]);
}

static int e1000_send_frame(struct net_device *dev, const void *data,
                            uint32_t len) {
  (void)dev;

  /* Bounce buffer: allocating a physical frame because 'data' might be in virt
   * memory */
  uintptr_t phys = pmm_alloc_frames(1);
  void *bounce = (void *)(phys + hhdm_offset());
  kmemcpy(bounce, data, len);

  tx_descs[tx_cur].addr = phys;
  tx_descs[tx_cur].length = len;
  tx_descs[tx_cur].cmd = (1 << 3) | (1 << 1) | (1 << 0); /* RS, IFCS, EOP */
  tx_descs[tx_cur].status = 0;

  uint8_t old_cur = tx_cur;
  tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
  e1000_write_reg(E1000_REG_TDT, tx_cur);

  while (!(tx_descs[old_cur].status & 0xFF)) {
    /* Wait for TX to finish */
  }

  pmm_free_frames(phys, 1);
  return 0;
}

static void e1000_interrupt(uint8_t irq, struct interrupt_frame *frame) {
  (void)irq;
  (void)frame;

  /* Acknowledge interrupt by reading ICR */
  uint32_t status = e1000_read_reg(E1000_REG_ICR);
  if (!status)
    return;

  /* Check if it's a Receive interrupt (RXT0, bit 7) */
  if (status & (1 << 7)) {
    while (rx_descs[rx_cur].status & (1 << 0)) { /* Descriptor Done bit */
      uint32_t len = rx_descs[rx_cur].length;
      void *data = rx_buffers[rx_cur];

      /* Pass to network stack */
      net_receive_frame(&s_net_dev, data, len);

      /* Clear status and give descriptor back to hardware */
      rx_descs[rx_cur].status = 0;
      e1000_write_reg(E1000_REG_RDT, rx_cur);

      rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;
    }
  }
}

static void e1000_init_rx(void) {
  uintptr_t rx_phys = pmm_alloc_frames(1);
  rx_descs = (e1000_rx_desc_t *)(rx_phys + hhdm_offset());
  kmemset(rx_descs, 0, 4096);

  for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
    uintptr_t buf_phys = pmm_alloc_frames(2); /* 8KB */
    rx_buffers[i] = (uint8_t *)(buf_phys + hhdm_offset());
    rx_descs[i].addr = buf_phys;
    rx_descs[i].status = 0;
  }

  e1000_write_reg(E1000_REG_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFF));
  e1000_write_reg(E1000_REG_RDBAH, (uint32_t)(rx_phys >> 32));
  e1000_write_reg(E1000_REG_RDLEN, E1000_NUM_RX_DESC * 16);
  e1000_write_reg(E1000_REG_RDH, 0);
  e1000_write_reg(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);

  rx_cur = 0;
  /* RCTL: Enable, Broadcast Accept, Unicast Promiscuous, 8192-byte buffers,
   * Long Packet Enable, Buffer Size Extension, Strip CRC. Without BSEX the
   * (2 << 16) size encoding means 512 bytes, not the 8 KiB we allocate. */
  e1000_write_reg(E1000_REG_RCTL, (1 << 1) | (1 << 15) | (1 << 4) | (1 << 2) |
                                      (1 << 25) | (1 << 26) | (2 << 16));
}

static void e1000_init_tx(void) {
  uintptr_t tx_phys = pmm_alloc_frames(1);
  tx_descs = (e1000_tx_desc_t *)(tx_phys + hhdm_offset());
  kmemset(tx_descs, 0, 4096);

  e1000_write_reg(E1000_REG_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFF));
  e1000_write_reg(E1000_REG_TDBAH, (uint32_t)(tx_phys >> 32));
  e1000_write_reg(E1000_REG_TDLEN, E1000_NUM_TX_DESC * 16);
  e1000_write_reg(E1000_REG_TDH, 0);
  e1000_write_reg(E1000_REG_TDT, 0);

  tx_cur = 0;
  /* TCTL: Enable, Pad Short Packets, Collision Threshold, Collision Distance */
  e1000_write_reg(E1000_REG_TCTL,
                  (1 << 1) | (1 << 3) | (0x0F << 4) | (0x40 << 12));
}

bool e1000_init(void) {
  size_t count = pci_device_count();
  for (size_t i = 0; i < count; i++) {
    const pci_device_t *d = pci_device_at(i);
    if (d->vendor == E1000_VEND &&
        (d->device == E1000_DEV_100E || d->device == E1000_DEV_10D3)) {
      s_e1000_pci = d;
      break;
    }
  }

  if (!s_e1000_pci) {
    return false;
  }

  kprintf("[e1000] Found Intel PRO/1000 Ethernet Controller at %02x:%02x.%x\n",
          s_e1000_pci->bus, s_e1000_pci->dev, s_e1000_pci->func);

  /* Olvassuk be a BAR0-át (MMIO base) */
  uint64_t bar0 = pci_get_bar(s_e1000_pci, 0);
  if (!bar0) {
    kprintf("[e1000] BAR0 is null, aborting init\n");
    return false;
  }
  s_mmio_base = (uint8_t *)(bar0 + hhdm_offset());

  /* Engedélyezzük a DMA-t és az MMIO-t a PCI command regiszterben */
  pci_enable_busmaster(s_e1000_pci);

  /* Eszköz reset */
  e1000_write_reg(E1000_REG_CTRL, e1000_read_reg(E1000_REG_CTRL) | (1 << 26));
  /* Várjunk a reset befejeződésére */
  for (volatile int i = 0; i < 100000; i++)
    ;

  /* Hálózat absztrakció beállítása */
  kstrcpy(s_net_dev.name, "eth0");
  s_net_dev.send_frame = e1000_send_frame;
  e1000_read_mac();

  e1000_init_rx();
  e1000_init_tx();

  /* IRQ regisztrálás - ellenőrizzük hogy érvényes (0-15) */
  uint8_t irq = pci_cfg_read8(s_e1000_pci->bus, s_e1000_pci->dev,
                              s_e1000_pci->func, 0x3C);
  if (irq < 16) {
    kprintf("[e1000] Registering IRQ %u\n", irq);
    irq_register(irq, e1000_interrupt);
    /* Enable Receive Timer (RXT0) and Link Status Change (LSC) interrupts */
    e1000_write_reg(E1000_REG_IMS, (1 << 7) | (1 << 2));
  } else {
    kprintf("[e1000] IRQ=0x%02x invalid or not assigned, polling mode only\n",
            irq);
  }

  net_register_device(&s_net_dev);

  kprintf("[e1000] Initialization complete (MMIO: 0x%lx).\n",
          (uint64_t)s_mmio_base);
  return true;
}
