/* Rex OS - xHCI (USB 3.x) Host Controller driver
 *
 * Minimális implementáció a modern gépek USB alapjához.
 * Csak egy xHCI controllert kezel (az első PCI-n talált).
 * HID osztály billentyűzet + egér boot protokollal elég a shell / desktop
 * működéséhez PS/2 portok nélkül is.
 *
 * Fő referenciák:
 *   - xHCI 1.2 spec (Intel)
 *   - osdev.org/xHCI
 */
#pragma once
#include <rexos/types.h>

/* Inicializálás: PCI -> MMIO -> reset -> command/event ring setup.
 * Visszaad true-t, ha a HC fut és kész a port enumerációra. */
bool xhci_init(void);

/* Port enumeráció: minden csatlakoztatott device-ot konfigurál.
 * Meghívható bootkor, vagy késleltetve. */
void xhci_enumerate_ports(void);

/* Periodikus polling: minden HID endpoint-ról lehúzza a friss riportokat,
 * és a megfelelő keyboard/mouse puffert frissíti. Kell hívni rendszeresen
 * (pl. a desktop main loopból, vagy egy dedikált kernel taszkból). */
void xhci_poll(void);

/* --- USB HID kimenetek, amikkel a PS/2 driverek egyesülnek --- */

/* Visszaadja a következő ASCII karaktert az USB HID boot kbd pufferből,
 * vagy 0-t ha üres. */
bool xhci_kbd_has_data(void);
char xhci_kbd_getc(void);

/* Kinyeri az aktuális USB egér delta-kat. A delta-k a visszaadás után nullázódnak.
 * Visszatérés: 1 ha volt mozgás, 0 különben. */
int  xhci_mouse_take(int *dx, int *dy, uint8_t *buttons);
