/* Freestanding shims for the handful of libc/libm calls that render.c
 * (panorama LUT + fixed-point bilinear renderer) and stb_image.h (JPEG-only,
 * STBI_NO_HDR/STBI_NO_LINEAR/STBI_NO_STDIO/STBI_NO_SIMD) need.
 *
 * Backed by a single bump allocator arena obtained once via EFI AllocatePages.
 * No free()/realloc()-in-place reclamation: fine for a one-shot decode +
 * LUT build followed by a steady-state render loop that allocates nothing.
 */
#ifndef MINI_LIBC_H
#define MINI_LIBC_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* The pure, EFI-free formatter core. Declared unconditionally so
 * tests/test_snprintf.c (which defines MINI_LIBC_SNPRINTF_TEST and links the
 * host libc) can prototype it without pulling in the libc-named shims below --
 * whose signatures would clash with glibc's type-generic <string.h> macros. */
int   mini_vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap);

#ifndef MINI_LIBC_SNPRINTF_TEST

void  mini_libc_init(void* arena, size_t arena_size);
/* Wholesale per-frame scratch reclaim (see mini_libc.c). */
void  mini_libc_frame_reset(void);

void *malloc(size_t sz);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t sz);
void  free(void *p);

void *memcpy(void *restrict dst, const void *restrict src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int   abs(int x);

/* ---- strings (core: assets.c/menu.c) ---- */
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *hay, const char *needle);
int   atoi(const char *s);

/* ---- formatted output (core: menu.c on-screen text + asset paths) ----
 * snprintf() wraps mini_vsnprintf (declared above). */
int   snprintf(char *dst, size_t cap, const char *fmt, ...);

/* ---- minimal double-precision math (documented approximations; see
 * POC-REPORT.md "math shims" section for accuracy notes) ---- */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The core (render.c/menu.c) and stb_image include <math.h> and call the
 * standard names. We provide REAL standard-named symbols (below) that the
 * mingw <math.h> declarations resolve to at link -- NOT #define macros, which
 * would corrupt those very declarations in the ported source. */
double sqrt(double x);
double fmod(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double atan2(double y, double x);
double floor(double x);
double ceil(double x);
float  sinf(float x);
float  cosf(float x);
float  fabsf(float x);

#endif /* !MINI_LIBC_SNPRINTF_TEST */

#endif
