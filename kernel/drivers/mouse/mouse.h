#pragma once
#include <rexos/types.h>

typedef struct {
    int32_t x, y;
    uint8_t buttons; /* bit 0=left, 1=right, 2=middle */
} mouse_state_t;

void mouse_init(void);
void mouse_get_state(mouse_state_t *out);
void mouse_set_bounds(uint32_t max_x, uint32_t max_y);
