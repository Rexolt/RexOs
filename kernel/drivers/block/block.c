#include <drivers/block/block.h>
#include <lib/string.h>
#include <lib/printf.h>

#define BLOCK_MAX_DEVICES 8

static block_device_t s_devices[BLOCK_MAX_DEVICES];
static size_t         s_count = 0;

block_device_t *block_register(const block_device_t *dev) {
    if (!dev) return NULL;
    if (s_count >= BLOCK_MAX_DEVICES) {
        kprintf("[block] table full, ignoring '%s'\n", dev->name);
        return NULL;
    }
    block_device_t *slot = &s_devices[s_count++];
    *slot = *dev;
    kprintf("[block] registered '%s' (%lu sectors x %u bytes = %lu MB)\n",
            slot->name, slot->sector_count, slot->sector_size,
            (slot->sector_count * (uint64_t)slot->sector_size) / (1024 * 1024));
    return slot;
}

size_t block_count(void) { return s_count; }

block_device_t *block_at(size_t i) {
    if (i >= s_count) return NULL;
    return &s_devices[i];
}

block_device_t *block_get_first(void) {
    return s_count > 0 ? &s_devices[0] : NULL;
}

block_device_t *block_find(const char *name) {
    for (size_t i = 0; i < s_count; i++) {
        if (kstrcmp(s_devices[i].name, name) == 0) return &s_devices[i];
    }
    return NULL;
}
