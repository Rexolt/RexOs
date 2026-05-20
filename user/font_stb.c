/* =============================================================================
 *  RexOS - stb_truetype.h beépítés (third_party/stb/stb_truetype.h)
 *  Felüldefiniáljuk a libm/libc hívásokat a saját minimal verzióinkkal.
 * ========================================================================== */

#include "libc.h"
#include <stddef.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Saját minimal math (math_min.c). */
extern double m_floor(double x);
extern double m_ceil(double x);
extern double m_sqrt(double x);
extern double m_fabs(double x);
extern double m_fmod(double x, double y);
extern double m_cos(double x);
extern double m_acos(double x);
extern double m_pow(double x, double y);

/* stb_truetype default macrók felülbírálása. */
#define STBTT_ifloor(x)   ((int) m_floor(x))
#define STBTT_iceil(x)    ((int) m_ceil(x))
#define STBTT_sqrt(x)     m_sqrt(x)
#define STBTT_pow(x,y)    m_pow(x,y)
#define STBTT_fmod(x,y)   m_fmod(x,y)
#define STBTT_cos(x)      m_cos(x)
#define STBTT_acos(x)     m_acos(x)
#define STBTT_fabs(x)     m_fabs(x)

#define STBTT_malloc(x,u) ((void)(u), malloc((uint64_t)(x)))
#define STBTT_free(x,u)   ((void)(u), free(x))
#define STBTT_assert(x)   ((void)0)

static int stbtt_strlen_compat(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}
#define STBTT_strlen(x)   stbtt_strlen_compat(x)
#define STBTT_memcpy      memcpy
#define STBTT_memset      memset

/* No SDF, no rasterization version 1, no oversample - tartsuk meg az alapot. */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb/stb_truetype.h"

#pragma GCC diagnostic pop
