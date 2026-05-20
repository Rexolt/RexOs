/* =============================================================================
 *  RexOS - Font subsystem (TTF + glyph cache)
 *  Public API a desktop renderelőhöz.
 * ========================================================================== */
#pragma once
#include <stdint.h>

/* Font init - a beépített DejaVu Sans Mono TTF betöltése. Egyszer kell hívni. */
int font_init(void);

/* Egy glyph rajzolása alpha-blending-gel egy framebuffer pufferre.
 * `dst` az ARGB/0xRRGGBB formátumú backbuffer (32-bit/pixel), `dst_w/dst_h` mérete.
 * (x, y) = baseline pozíció pixelekben.
 * `pixel_height` a font magasság pixelekben (pl. 14 = UI, 18 = nagyobb szöveg).
 * `color` = 0x00RRGGBB.
 * Visszatér: a karakter advance (mennyivel kell előrelépni az x-en). */
int font_draw_char(uint32_t *dst, int dst_w, int dst_h,
                   int x, int y, int codepoint, int pixel_height, uint32_t color);

/* Egy egész string rajzolása. Visszaadja a teljes szélességet pixelekben. */
int font_draw_text(uint32_t *dst, int dst_w, int dst_h,
                   int x, int y, const char *text, int pixel_height, uint32_t color);

/* Csak méret-számítás rajzolás nélkül. */
int font_measure_text(const char *text, int pixel_height);

/* Egyetlen karakter advance (rajzolás nélkül) - pl. szövegszerkesztéshez. */
int font_char_advance(int codepoint, int pixel_height);

/* Font ascent/descent/line-gap pixelben adott magasságra. */
int font_ascent(int pixel_height);
int font_descent(int pixel_height);
int font_line_height(int pixel_height);

/* Bold variáns (újra-rajzolja a glyph-et 1px offsettel). */
int font_draw_text_bold(uint32_t *dst, int dst_w, int dst_h,
                        int x, int y, const char *text, int pixel_height, uint32_t color);
