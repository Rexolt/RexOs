/* Rex OS - linear framebuffer driver (via Limine) */
#pragma once
#include <rexos/types.h>

/* xRGB-8888 színek */
#define FB_BLACK    0x00000000u
#define FB_WHITE    0x00FFFFFFu
#define FB_RED      0x00CC2020u
#define FB_GREEN    0x0020CC20u
#define FB_BLUE     0x002040CCu
#define FB_ORANGE   0x00FF8800u
#define FB_DARKBG   0x00101820u
#define FB_BANNER   0x001E3A5Fu

bool     fb_init(void);
uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_bpp(void);
uint32_t fb_pitch(void);
void *   fb_address(void);

void fb_clear(uint32_t color);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/* Bitmap font rendering (8x16) */
#define FONT_W 8
#define FONT_H 16
extern const uint8_t font_8x16[256][16];

void fb_put_char(uint32_t x, uint32_t y, uint8_t ch, uint32_t fg, uint32_t bg);

/* Pixel-szintű sor másolás (scrolling-hoz). Forrás/cél: y koordináták. */
void fb_blit_rows(uint32_t dst_y, uint32_t src_y, uint32_t rows);
