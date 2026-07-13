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

void  mini_libc_init(void* arena, size_t arena_size);

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

/* ---- minimal double-precision math (documented approximations; see
 * POC-REPORT.md "math shims" section for accuracy notes) ---- */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double m_sqrt(double x);
double m_fmod(double x, double y);
double m_sin(double x);
double m_cos(double x);
double m_tan(double x);
double m_atan(double x);
double m_atan2(double y, double x);
double m_floor(double x);
float  m_fabsf(float x);

/* render.c / pano.c call the "normal" names; map them onto our shims so the
 * ported source reads identically to the repo copy. */
#define sqrt  m_sqrt
#define fmod  m_fmod
#define sin   m_sin
#define cos   m_cos
#define tan   m_tan
#define atan2 m_atan2
#define floor m_floor

#endif
