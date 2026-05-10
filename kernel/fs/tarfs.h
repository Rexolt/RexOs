#pragma once

#include <rexos/types.h>

/* TARFS inicializálása egy adott memóriacímen lévő TAR archívumból.
 * Ezt a modult a kmain-ből fogjuk meghívni, miután megtaláltuk a modult.
 */
void tarfs_init(uint64_t address, uint64_t size);
