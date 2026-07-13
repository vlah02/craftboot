/* See mini_libc.h. Freestanding, no libc, no libm. */
#include "mini_libc.h"

/* ---------------------------------------------------------------- alloc */
static uint8_t *g_arena_base;
static size_t   g_arena_size;
static size_t   g_arena_off;

void mini_libc_init(void *arena, size_t arena_size) {
    g_arena_base = (uint8_t*)arena;
    g_arena_size = arena_size;
    g_arena_off = 0;
}

void *malloc(size_t sz) {
    size_t align = 16;
    size_t off = (g_arena_off + align - 1) & ~(align - 1);
    if (off + sz > g_arena_size) return NULL; /* OOM: caller must handle */
    void *p = g_arena_base + off;
    g_arena_off = off + sz;
    return p;
}

void *calloc(size_t n, size_t sz) {
    size_t total = n * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *p, size_t sz) {
    /* Bump allocator: always allocate fresh and copy. We don't track the
     * original allocation size, so copy the full new size (safe: arena is
     * zero-initialized-ish EFI pool memory and callers only grow buffers
     * they've already sized correctly via mad2/mad3/mad4 helpers). */
    void *n = malloc(sz);
    if (!n) return NULL;
    if (p) memcpy(n, p, sz);
    return n;
}

void free(void *p) { (void)p; /* bump allocator: no-op */ }

/* ---------------------------------------------------------------- mem */
void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    uint8_t *d = (uint8_t*)dst; const uint8_t *s = (const uint8_t*)src;
    while (n >= 8) { *(uint64_t*)d = *(const uint64_t*)s; d += 8; s += 8; n -= 8; }
    while (n--) *d++ = *s++;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dst; const uint8_t *s = (const uint8_t*)src;
    if (d == s || n == 0) return dst;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}
void *memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t*)dst; uint8_t b = (uint8_t)c;
    uint64_t pattern = (uint64_t)b * 0x0101010101010101ULL;
    while (n >= 8) { *(uint64_t*)d = pattern; d += 8; n -= 8; }
    while (n--) *d++ = b;
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = a, *pb = b;
    for (size_t i = 0; i < n; i++) if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int abs(int x) { return x < 0 ? -x : x; }

/* ---------------------------------------------------------------- math */
double m_sqrt(double x) {
    double r;
    __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}
double m_fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    double q = x / y;
    long long iq = (long long)q; /* truncate toward zero */
    return x - (double)iq * y;
}
double m_floor(double x) {
    long long i = (long long)x;
    double t = (double)i;
    if (x < 0.0 && t != x) t -= 1.0;
    return t;
}
float m_fabsf(float x) { return x < 0 ? -x : x; }

/* sin/cos via Taylor series; only ever called with |x| <~ pi/2 in this
 * codebase (half-FOV angles), so no range reduction is implemented.
 * Max error in that domain ~1e-6. */
double m_sin(double x) {
    double x2 = x * x;
    double term = x;
    double sum = x;
    for (int k = 1; k <= 6; k++) {
        term *= -x2 / (double)((2*k) * (2*k+1));
        sum += term;
    }
    return sum;
}
double m_cos(double x) {
    double x2 = x * x;
    double term = 1.0;
    double sum = 1.0;
    for (int k = 1; k <= 6; k++) {
        term *= -x2 / (double)((2*k-1) * (2*k));
        sum += term;
    }
    return sum;
}
double m_tan(double x) { return m_sin(x) / m_cos(x); }

/* atan via Abramowitz & Stegun 4.4.49 minimax polynomial (|error| < 0.0028 rad,
 * i.e. < 0.16 deg) with reciprocal reduction for |x|>1, plus atan2 quadrant
 * logic. Good enough for a projection LUT built once per pano; not used in
 * the per-pixel render hot path. */
double m_atan(double x) {
    int neg = x < 0.0; if (neg) x = -x;
    int inv = x > 1.0; if (inv) x = 1.0 / x;
    double x2 = x * x;
    double r = x * (0.9998660 + x2 * (-0.3302995 + x2 * (0.1801410 + x2 * (-0.0851330 + x2 * 0.0208351))));
    if (inv) r = M_PI / 2.0 - r;
    if (neg) r = -r;
    return r;
}
double m_atan2(double y, double x) {
    if (x > 0.0) return m_atan(y / x);
    if (x < 0.0) return y >= 0.0 ? m_atan(y / x) + M_PI : m_atan(y / x) - M_PI;
    if (y > 0.0) return M_PI / 2.0;
    if (y < 0.0) return -M_PI / 2.0;
    return 0.0;
}
