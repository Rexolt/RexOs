/* Rex OS - NVMe (Non-Volatile Memory Express) driver
 *
 * PCIe NVMe SSD-k támogatása (class=01h, sub=08h, prog_if=02h).
 * Polling mód, 1 Admin queue + 1 I/O queue, LBA48.
 */
#pragma once
#include <rexos/types.h>

/* Detektálja az NVMe kontrollerest a PCI buszon, inicializálja,
 * és block device-ként regisztrálja. */
bool nvme_init(void);
