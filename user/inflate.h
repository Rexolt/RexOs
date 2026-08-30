/* =============================================================================
 *  RexOS - DEFLATE / gzip / zlib dekompresszió (puff-alapú, freestanding)
 *
 *  A modern HTTP szerverek a választ szinte mindig gzip/deflate tömörítéssel
 *  küldik. Ez a modul kicsomagolja a HTTP body-t a böngésző renderelője elé.
 *
 *  Forrás-ihlet: zlib `puff.c` (Mark Adler, public domain). Itt longjmp helyett
 *  explicit hibakódokkal dolgozunk, mert a RexOS user-libc nem ad setjmp-et.
 * ========================================================================== */
#pragma once
#include <stdint.h>

/* Nyers DEFLATE folyam kicsomagolása.
 *   dest/destlen : kimeneti puffer és kapacitása; visszatéréskor a tényleges
 *                  hossz kerül *destlen-be.
 *   source/srclen: tömörített bemenet.
 * Visszatérés:
 *    0  = teljes siker
 *    1  = a kimeneti puffer betelt (csonkolt, de használható eredmény)
 *   <0  = formátum/adathiba
 */
int rex_inflate(uint8_t *dest, unsigned long *destlen,
                const uint8_t *source, unsigned long srclen);

/* Auto-detektál: gzip (0x1f 0x8b), zlib (CMF/FLG), vagy nyers DEFLATE,
 * lefejti a konténer-fejlécet és meghívja a rex_inflate-et.
 * Visszatérési kódok ugyanazok mint a rex_inflate-nél. */
int rex_gunzip(uint8_t *dest, unsigned long *destlen,
               const uint8_t *source, unsigned long srclen);
