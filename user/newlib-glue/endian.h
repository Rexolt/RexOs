/* Minimál glibc-kompatibilis <endian.h> RexOS-hez (x86_64: little-endian). */
#ifndef _ENDIAN_H
#define _ENDIAN_H

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412
#define __BYTE_ORDER    __LITTLE_ENDIAN

#define LITTLE_ENDIAN __LITTLE_ENDIAN
#define BIG_ENDIAN    __BIG_ENDIAN
#define PDP_ENDIAN    __PDP_ENDIAN
#define BYTE_ORDER    __BYTE_ORDER

#define htole16(x) ((__uint16_t)(x))
#define le16toh(x) ((__uint16_t)(x))
#define htole32(x) ((__uint32_t)(x))
#define le32toh(x) ((__uint32_t)(x))
#define htole64(x) ((__uint64_t)(x))
#define le64toh(x) ((__uint64_t)(x))
#define htobe16(x) __builtin_bswap16(x)
#define be16toh(x) __builtin_bswap16(x)
#define htobe32(x) __builtin_bswap32(x)
#define be32toh(x) __builtin_bswap32(x)
#define htobe64(x) __builtin_bswap64(x)
#define be64toh(x) __builtin_bswap64(x)

#endif /* _ENDIAN_H */
