/* See mini_libc.h. Freestanding, no libc, no libm. */
#include "mini_libc.h"

/* tests/test_snprintf.c #includes this file with MINI_LIBC_SNPRINTF_TEST
 * defined to host-test the pure mini_vsnprintf core in isolation; that guard
 * drops the allocator/mem/str/math + libc-named snprintf wrappers, which would
 * otherwise clash with the host libc the test itself links against. */
#ifndef MINI_LIBC_SNPRINTF_TEST

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

/* Per-frame scratch reclaim. The core's render loop is NOT allocation-free:
 * blit_scaled/blit_9slice build transient scaled images each frame (~0.8 MB)
 * whose free()s the bump allocator can't reclaim piecemeal. Instead the loop
 * calls this once per iteration: the FIRST call marks the bump offset (right
 * after scene_load's persistent assets -- fonts/logo/pano/splash_rot), and
 * every call rolls back to that mark, wholesale-freeing the previous frame's
 * scratch. Keeps the arena footprint flat under sustained rendering. */
void mini_libc_frame_reset(void) {
    static size_t mark = 0;
    static int    marked = 0;
    if (!marked) { mark = g_arena_off; marked = 1; }
    g_arena_off = mark;
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

/* No-op: the bump allocator reclaims transient scratch wholesale via
 * mini_libc_frame_reset() once per render frame, so individual free()s (which
 * a bump allocator can't honor piecemeal anyway) do nothing. */
void free(void *p) { (void)p; }

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

/* ---------------------------------------------------------------- str */
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
    return 0;
}
char *strchr(const char *s, int c) {
    char ch = (char)c;
    for (;; s++) { if (*s == ch) return (char*)s; if (!*s) return NULL; }
}
char *strrchr(const char *s, int c) {
    char ch = (char)c; const char *last = NULL;
    for (;; s++) { if (*s == ch) last = s; if (!*s) return (char*)last; }
}
char *strstr(const char *hay, const char *needle) {
    if (!*needle) return (char*)hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)hay;
    }
    return NULL;
}
int atoi(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;
    int sign = 1;
    if (*s == '+' || *s == '-') { if (*s == '-') sign = -1; s++; }
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return sign * v;
}

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

/* 6-term Taylor core, accurate to ~1e-6 on [-pi/2, pi/2] and NOT beyond -- the
 * series diverges hard for larger arguments. m_sin/m_cos below MUST reduce
 * their argument into this domain first. */
static double sin_core(double x) {
    double x2 = x * x;
    double term = x;
    double sum = x;
    for (int k = 1; k <= 6; k++) {
        term *= -x2 / (double)((2*k) * (2*k+1));
        sum += term;
    }
    return sum;
}

/* Range-reduce to [-pi/2, pi/2] before the Taylor core. render.c only ever
 * passes small half-FOV angles, but menu.c's splash pulse feeds sin() an
 * UNBOUNDED, monotonically growing argument (t * 1000/180); without reduction
 * the raw series explodes (pulse -> thousands -> a multi-GB splash scale ->
 * OOM/OOB crash in firmware). Reduce mod 2*pi into [-pi, pi], then fold the
 * outer quadrants onto [-pi/2, pi/2] via sin(pi - x) == sin(x). */
double m_sin(double x) {
    const double TWO_PI = 2.0 * M_PI;
    x -= TWO_PI * m_floor(x / TWO_PI + 0.5);   /* -> [-pi, pi] */
    if (x >  M_PI / 2.0) x =  M_PI - x;         /* -> [-pi/2, pi/2] */
    else if (x < -M_PI / 2.0) x = -M_PI - x;
    return sin_core(x);
}
double m_cos(double x) {
    return m_sin(x + M_PI / 2.0);
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

/* Standard-named wrappers: the core includes <math.h> and calls these; they
 * resolve to the m_* shims above at link. (Real symbols, not #defines, so the
 * <math.h> prototypes in the ported source are not corrupted.) */
double sqrt(double x)            { return m_sqrt(x); }
double fmod(double x, double y)  { return m_fmod(x, y); }
double sin(double x)             { return m_sin(x); }
double cos(double x)             { return m_cos(x); }
double tan(double x)             { return m_tan(x); }
double atan2(double y, double x) { return m_atan2(y, x); }
double floor(double x)           { return m_floor(x); }
double ceil(double x)            { double f = m_floor(x); return f == x ? f : f + 1.0; }
float  sinf(float x)             { return (float)m_sin((double)x); }
float  cosf(float x)             { return (float)m_cos((double)x); }
float  fabsf(float x)            { return x < 0.f ? -x : x; }

#endif /* !MINI_LIBC_SNPRINTF_TEST */

/* ---------------------------------------------------------------- printf
 * Minimal snprintf supporting exactly the conversions the core uses:
 *   %s %d %u %c %x %X (with width + '0' pad, e.g. %04X) %f (%.Nf) %%.
 * Split into a pure mini_vsnprintf so it is host-unit-testable with no EFI
 * dependency (see tests/test_snprintf.c). C99 semantics: always NUL-terminate
 * within cap (cap==0 writes nothing), and RETURN the number of chars that
 * WOULD have been written had cap been unbounded (excluding the NUL). */

/* Bounded sink: tracks would-be length independent of the cap. */
typedef struct { char *dst; size_t cap; size_t len; } sink_t;
static void sink_putc(sink_t *s, char c) {
    if (s->len + 1 < s->cap) s->dst[s->len] = c;   /* leave room for NUL */
    s->len++;
}
static void sink_puts(sink_t *s, const char *p) { while (*p) sink_putc(s, *p++); }

/* Unsigned to string in the given base (10 or 16); upper selects A-F. */
static void emit_uint(sink_t *s, uint64_t v, int base, int upper,
                      int width, int zeropad) {
    char tmp[32]; int n = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = dig[v % (unsigned)base]; v /= (unsigned)base; }
    for (int pad = width - n; pad > 0; pad--) sink_putc(s, zeropad ? '0' : ' ');
    while (n) sink_putc(s, tmp[--n]);
}

static void emit_int(sink_t *s, int64_t v) {
    if (v < 0) { sink_putc(s, '-'); emit_uint(s, (uint64_t)(-v), 10, 0, 0, 0); }
    else emit_uint(s, (uint64_t)v, 10, 0, 0, 0);
}

/* Fixed-point float with `prec` fractional digits, rounded half-away-from-zero.
 * Handles the sign, integer part, and (if prec>0) a '.' plus prec digits. */
static void emit_float(sink_t *s, double v, int prec) {
    if (v < 0) { sink_putc(s, '-'); v = -v; }
    /* scale, round, split */
    double scale = 1.0; for (int i = 0; i < prec; i++) scale *= 10.0;
    double scaled = v * scale + 0.5;            /* round half up */
    uint64_t iv = (uint64_t)scaled;             /* truncate */
    uint64_t p10 = (uint64_t)scale;
    uint64_t ipart = prec ? iv / p10 : iv;
    uint64_t frac  = prec ? iv % p10 : 0;
    emit_uint(s, ipart, 10, 0, 0, 0);
    if (prec > 0) {
        sink_putc(s, '.');
        /* zero-pad the fractional part to exactly prec digits */
        char fbuf[24]; int fn = 0;
        if (frac == 0) fbuf[fn++] = '0';
        while (frac) { fbuf[fn++] = (char)('0' + frac % 10); frac /= 10; }
        for (int pad = prec - fn; pad > 0; pad--) sink_putc(s, '0');
        while (fn) sink_putc(s, fbuf[--fn]);
    }
}

int mini_vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap) {
    sink_t s = { dst, cap, 0 };
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') { sink_putc(&s, *f); continue; }
        f++;                                    /* past '%' */
        if (*f == '%') { sink_putc(&s, '%'); continue; }
        int zeropad = 0, width = 0, prec = -1;
        if (*f == '0') { zeropad = 1; f++; }
        while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
        if (*f == '.') { f++; prec = 0; while (*f >= '0' && *f <= '9') { prec = prec * 10 + (*f - '0'); f++; } }
        switch (*f) {
            case 's': { const char *p = va_arg(ap, const char*); sink_puts(&s, p ? p : "(null)"); break; }
            case 'd': case 'i': emit_int(&s, (int64_t)va_arg(ap, int)); break;
            case 'u': emit_uint(&s, (uint64_t)va_arg(ap, unsigned), 10, 0, width, zeropad); break;
            case 'x': emit_uint(&s, (uint64_t)va_arg(ap, unsigned), 16, 0, width, zeropad); break;
            case 'X': emit_uint(&s, (uint64_t)va_arg(ap, unsigned), 16, 1, width, zeropad); break;
            case 'c': sink_putc(&s, (char)va_arg(ap, int)); break;
            case 'f': emit_float(&s, va_arg(ap, double), prec < 0 ? 6 : prec); break;
            default:  sink_putc(&s, '%'); if (*f) sink_putc(&s, *f); break;
        }
    }
    if (s.cap) s.dst[s.len < s.cap ? s.len : s.cap - 1] = 0;
    return (int)s.len;
}

#ifndef MINI_LIBC_SNPRINTF_TEST
int snprintf(char *dst, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = mini_vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    return r;
}

/* mingw's <stdio.h> compiles snprintf() call sites as an inline that forwards
 * to this internal (with -D__USE_MINGW_ANSI_STDIO=0). Route it to our tested
 * core so the on-screen/path formatting is exactly what test_snprintf covers. */
int __ms_vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap) {
    return mini_vsnprintf(dst, cap, fmt, ap);
}
#endif /* !MINI_LIBC_SNPRINTF_TEST */
