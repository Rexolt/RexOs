/* Rex OS - AHCI SATA driver
 *
 * Modern (PCI) SATA controller-ek minimális támogatása.
 * MMIO BAR5 (ABAR) -> HBA memóriaregiszterek.
 * Csak az első talált SATA port-ot inicializáljuk és regisztráljuk
 * block device-ként. Bővítés (több port, NCQ, írás) később.
 */
#pragma once
#include <rexos/types.h>

/* PCI scan, controller reset, port discovery + identify.
 * Visszaad true-t, ha legalább egy port használható lemezzel. */
bool ahci_init(void);
