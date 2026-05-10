#include "libc.h"

static uint32_t *fb = 0;
static uint64_t width = 0;
static uint64_t height = 0;
static uint64_t pitch = 0;

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb) return;
    for (uint32_t dy = 0; dy < h; dy++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb + (y + dy) * pitch);
        for (uint32_t dx = 0; dx < w; dx++) {
            row[x + dx] = color;
        }
    }
}

void _start() {
    print("User GUI app starting...\n");
    
    uint64_t fb_vaddr = (uint64_t)get_fb(&width, &height, &pitch);
    fb = (uint32_t *)fb_vaddr;
    
    if (!fb) {
        print("Failed to get framebuffer!\n");
        exit(1);
    }
    
    // Draw some rectangles!
    draw_rect(100, 100, 200, 150, 0x00FF0000); // Red
    draw_rect(150, 150, 200, 150, 0x0000FF00); // Green
    draw_rect(200, 200, 200, 150, 0x000000FF); // Blue
    
    // Draw a moving box
    uint32_t box_x = 0;
    for (int i = 0; i < 500; i++) {
        draw_rect(box_x, 400, 50, 50, 0x00000000); // Erase
        box_x += 2;
        draw_rect(box_x, 400, 50, 50, 0x00FFFFFF); // Draw
        for(int j=0; j<500000; j++) __asm__ volatile("nop"); // Delay
        yield();
    }
    
    print("User GUI app exiting.\n");
    exit(0);
}
