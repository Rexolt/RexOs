/* Rex OS - x86 port I/O primitives (inline asm)
 *
 * Az x86 architektúrán az I/O portok egy elkülönített címterén keresztül
 * történik a hardveres kommunikáció: in/out utasítások. Minden egy-soros
 * inline asm, hogy a fordító inlinelhassa a hot path-ekbe.
 */
#pragma once
#include <rexos/types.h>

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

/* I/O delay: a 0x80 port a POST-kód portja, ami DOS óta nem használt
 * userland-ben. Egy outb ide ~1 mikroszekundumos késleltetést ad,
 * elég idő a régi hardvernek (pl. PIC, PIT) hogy stabilizálódjon. */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }
static inline void hlt(void) { __asm__ volatile ("hlt"); }
