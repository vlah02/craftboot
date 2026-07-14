#include "core/render.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef PANO_MP_SERVICES
#include "platform/plat.h"      /* EFI: spread the render across cores via plat_run_on_all */
#else
#include <unistd.h>
#include <pthread.h>
#endif
#ifndef PANO_NO_AVX2
#include <immintrin.h>
#endif

uint32_t mix_xrgb(uint32_t a, uint32_t b, unsigned w) {
    uint32_t rb = ((a & 0xFF00FF) * (256 - w) + (b & 0xFF00FF) * w) >> 8 & 0xFF00FF;
    uint32_t g  = ((a & 0x00FF00) * (256 - w) + (b & 0x00FF00) * w) >> 8 & 0x00FF00;
    return rb | g;
}
void fb_fill(fb_t *f, uint32_t c) { for (long i = 0; i < (long)f->w * f->h; i++) f->px[i] = c; }
void fill_rect(fb_t *f, int x, int y, int w, int h, uint32_t c) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= f->h) continue;
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < f->w) f->px[(long)j * f->w + i] = c;
    }
}
static inline void put_rgba(fb_t *f, int x, int y, const uint8_t *p) {
    if (x < 0 || y < 0 || x >= f->w || y >= f->h || p[3] == 0) return;
    uint32_t src = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    uint32_t *d = &f->px[(long)y * f->w + x];
    *d = p[3] == 255 ? src : mix_xrgb(*d, src, ((unsigned)p[3] * 256) / 255);
}
void blit(fb_t *f, const img_t *s, int x, int y) {
    for (int j = 0; j < s->h; j++)
        for (int i = 0; i < s->w; i++)
            put_rgba(f, x + i, y + j, &s->rgba[((long)j * s->w + i) * 4]);
}
static void sample_bilinear(const img_t *s, float u, float v, uint8_t out[4]) {
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    if (u > s->w - 1) u = (float)s->w - 1;
    if (v > s->h - 1) v = (float)s->h - 1;
    int x0 = (int)u, y0 = (int)v;
    int x1 = x0 + 1 < s->w ? x0 + 1 : x0, y1 = y0 + 1 < s->h ? y0 + 1 : y0;
    float fu = u - x0, fv = v - y0;
    const uint8_t *p00 = &s->rgba[((long)y0 * s->w + x0) * 4], *p01 = &s->rgba[((long)y0 * s->w + x1) * 4];
    const uint8_t *p10 = &s->rgba[((long)y1 * s->w + x0) * 4], *p11 = &s->rgba[((long)y1 * s->w + x1) * 4];
    for (int c = 0; c < 4; c++) {
        float top = p00[c] + (p01[c] - p00[c]) * fu;
        float bot = p10[c] + (p11[c] - p10[c]) * fu;
        out[c] = (uint8_t)(top + (bot - top) * fv + 0.5f);
    }
}
img_t img_scaled(const img_t *s, int w, int h) {
    img_t o = { malloc((size_t)w * h * 4), w, h };
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            sample_bilinear(s, (i + 0.5f) * s->w / w - 0.5f, (j + 0.5f) * s->h / h - 0.5f,
                            &o.rgba[((long)j * w + i) * 4]);
    return o;
}
void blit_scaled(fb_t *f, const img_t *s, int x, int y, int w, int h) {
    img_t t = img_scaled(s, w, h);
    blit(f, &t, x, y);
    free(t.rgba);
}
img_t img_rotated(const img_t *s, float deg) {
    float a = deg * (float)M_PI / 180.f, ca = cosf(a), sa = sinf(a);
    int w = (int)(fabsf(s->w * ca) + fabsf(s->h * sa) + 1);
    int h = (int)(fabsf(s->w * sa) + fabsf(s->h * ca) + 1);
    img_t o = { calloc((size_t)w * h, 4), w, h };
    float cx = s->w / 2.f, cy = s->h / 2.f, ox = w / 2.f, oy = h / 2.f;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            float dx = i - ox, dy = j - oy;
            float u = cx + dx * ca + dy * sa, v = cy - dx * sa + dy * ca;
            if (u < 0 || v < 0 || u > s->w - 1 || v > s->h - 1) continue;
            sample_bilinear(s, u, v, &o.rgba[((long)j * w + i) * 4]);
        }
    return o;
}
void blit_9slice(fb_t *f, const img_t *s, int x, int y, int w, int h) {
    int cap_src = s->h;                       /* square caps, like the Python 9-slice */
    if (s->w < 2 * cap_src + 1 || w < 2) {    /* too narrow to 9-slice: plain stretch */
        blit_scaled(f, s, x, y, w, h);
        return;
    }
    int cap = (int)((float)cap_src * h / s->h);
    if (2 * cap > w) cap = w / 2;
    /* build sub-images by copy (simple + safe) */
    img_t left = { malloc((size_t)cap_src * s->h * 4), cap_src, s->h };
    img_t mid  = { malloc((size_t)(s->w - 2 * cap_src) * s->h * 4), s->w - 2 * cap_src, s->h };
    img_t right= { malloc((size_t)cap_src * s->h * 4), cap_src, s->h };
    for (int j = 0; j < s->h; j++) {
        memcpy(&left.rgba[(long)j*left.w*4],  &s->rgba[((long)j*s->w)*4],                 (size_t)left.w*4);
        memcpy(&mid.rgba[(long)j*mid.w*4],    &s->rgba[((long)j*s->w+cap_src)*4],         (size_t)mid.w*4);
        memcpy(&right.rgba[(long)j*right.w*4],&s->rgba[((long)j*s->w+s->w-cap_src)*4],    (size_t)right.w*4);
    }
    blit_scaled(f, &left, x, y, cap, h);
    blit_scaled(f, &mid, x + cap, y, w - 2 * cap, h);
    blit_scaled(f, &right, x + w - cap, y, cap, h);
    free(left.rgba); free(mid.rgba); free(right.rgba);
}

static void draw_glyph(fb_t *fb, const font_t *f, const glyph_t *g,
                       int x, int y, uint32_t rgb) {
    for (int j = 0; j < g->h; j++)
        for (int i = 0; i < g->w; i++) {
            const uint8_t *p = &f->atlas.rgba[(((long)g->y + j) * f->atlas.w + g->x + i) * 4];
            if (!p[3]) continue;
            uint8_t px[4] = { (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, p[3] };
            put_rgba(fb, x + i, y + j, px);
        }
}
int text_width(const font_t *f, const char *s) {
    int w = 0;
    for (; *s; s++) if (*s >= 32 && *s < 127) w += f->g[*s - 32].adv;
    return w;
}
void draw_text(fb_t *fb, const font_t *f, const char *s, int x, int y, uint32_t rgb) {
    for (; *s; s++) {
        if (*s < 32 || *s >= 127) continue;
        const glyph_t *g = &f->g[*s - 32];
        draw_glyph(fb, f, g, x, y, rgb);
        x += g->adv;
    }
}
void draw_text_shadow(fb_t *fb, const font_t *f, const char *s, int x, int y,
                      uint32_t rgb, uint32_t shadow) {
    int off = f->size >= 40 ? 3 : 2;
    draw_text(fb, f, s, x + off, y + off, shadow);
    draw_text(fb, f, s, x, y, rgb);
}
img_t text_render(const font_t *f, const char *s, uint32_t rgb, int outline_px) {
    int w = text_width(f, s) + 2 * outline_px + 2, h = f->g[0].h + 2 * outline_px + 2;
    img_t o = { calloc((size_t)w * h, 4), w, h };
    fb_t tmp_fb = { malloc((size_t)w * h * 4), w, h };
    if (!o.rgba || !tmp_fb.px) {          /* OOM: no splash beats a crash */
        free(o.rgba); free(tmp_fb.px);
        return (img_t){0};
    }
    /* draw into an offscreen fb with sentinel bg, then convert to RGBA */
    uint32_t BG = 0x123456;
    fb_fill(&tmp_fb, BG);
    for (int dy = -outline_px; dy <= outline_px; dy++)
        for (int dx = -outline_px; dx <= outline_px; dx++)
            if (dx || dy) draw_text(&tmp_fb, f, s, outline_px + dx + 1, outline_px + dy + 1, 0x000000);
    draw_text(&tmp_fb, f, s, outline_px + 1, outline_px + 1, rgb);
    for (long i = 0; i < (long)w * h; i++) {
        uint32_t c = tmp_fb.px[i];
        if (c == BG) continue;
        o.rgba[i * 4 + 0] = (uint8_t)(c >> 16);
        o.rgba[i * 4 + 1] = (uint8_t)(c >> 8);
        o.rgba[i * 4 + 2] = (uint8_t)c;
        o.rgba[i * 4 + 3] = 255;
    }
    free(tmp_fb.px);
    return o;
}

struct pano {
    uint32_t *src;          /* RGBX equirect, row-major */
    int ew, eh;             /* equirect dims */
    int w, h;               /* output dims */
    int nthreads;
    int32_t *baselon;       /* per output px, 16.16 fixed, in [0, EW<<16) */
    int32_t *row0, *row1;   /* per output px, source row offsets (already *ew) */
    uint16_t *wv;           /* per output px, vertical weight 0..256 */
};

pano_t *pano_create(const img_t *eq, int out_w, int out_h, float fov_deg) {
    pano_t *p = calloc(1, sizeof *p);
    p->ew = eq->w; p->eh = eq->h; p->w = out_w; p->h = out_h;
#ifdef PANO_MP_SERVICES
    p->nthreads = 1;                    /* unused: plat_run_on_all uses all CPUs */
#else
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    p->nthreads = ncpu > 8 ? 8 : (ncpu < 1 ? 1 : (int)ncpu);
#endif
    p->src = malloc((size_t)eq->w * eq->h * 4);
    for (long i = 0; i < (long)eq->w * eq->h; i++)
        p->src[i] = (uint32_t)eq->rgba[i*4] << 16 | (uint32_t)eq->rgba[i*4+1] << 8 | eq->rgba[i*4+2];
    long n = (long)out_w * out_h;
    p->baselon = malloc(n * 4); p->row0 = malloc(n * 4); p->row1 = malloc(n * 4);
    p->wv = malloc(n * 2);
    double th = tan(fov_deg * M_PI / 360.0), tv = th * out_h / out_w;
#ifdef PANO_CYLINDRICAL
    double half_fov = fov_deg * M_PI / 360.0;         /* fov/2 in radians (cylindrical) */
#endif
    for (int j = 0; j < out_h; j++) {
        double y = (out_h / 2.0 - (j + 0.5)) / (out_h / 2.0) * tv;
        for (int i = 0; i < out_w; i++) {
#ifdef PANO_CYLINDRICAL
            /* Cylindrical: longitude LINEAR in screen-x -> uniform horizontal
             * sampling density, so the sides stay as sharp as the centre even
             * at a wide FOV (no tan-magnification). Vertical kept perspective. */
            double lon = ((i + 0.5) - out_w / 2.0) / (out_w / 2.0) * half_fov;
            double lat = atan2(y, 1.0);
#else
            double x = ((i + 0.5) - out_w / 2.0) / (out_w / 2.0) * th;
            double lon = atan2(x, 1.0);
            double lat = atan2(y, sqrt(x * x + 1.0));
#endif
            double rowf = (0.5 - lat / M_PI) * p->eh;
            if (rowf < 0) rowf = 0;
            if (rowf > p->eh - 1) rowf = p->eh - 1;
            int r0 = (int)rowf, r1 = r0 + 1 < p->eh ? r0 + 1 : r0;
            double lonpx = lon / (2 * M_PI) * p->ew;      /* [-EW/2, EW/2) */
            if (lonpx < 0) lonpx += p->ew;
            long k = (long)j * out_w + i;
            p->baselon[k] = (int32_t)(lonpx * 65536.0);
            p->row0[k] = r0 * p->ew;
            p->row1[k] = r1 * p->ew;
            p->wv[k]  = (uint16_t)((rowf - r0) * 256.0);
        }
    }
    return p;
}

typedef struct { pano_t *p; fb_t *out; int64_t yawfx; int j0, j1; } slice_t;
static void *render_slice(void *arg) {
    slice_t *s = arg; pano_t *p = s->p;
    const int64_t EWFX = (int64_t)p->ew << 16;
    for (int j = s->j0; j < s->j1; j++) {
        long k = (long)j * p->w;
        int i = 0;
#ifndef PANO_NO_AVX2
        /* col/EWFX stay positive and well under 2^31 for realistic equirect
         * widths (EW <= a few thousand, so 2*EWFX << INT32_MAX), so the
         * epi32 arithmetic below is safe and cmpgt_epi32 behaves as an
         * unsigned compare. */
        const __m256i EW    = _mm256_set1_epi32(p->ew);
        const __m256i EWFXv = _mm256_set1_epi32(p->ew << 16);
        const __m256i yawv  = _mm256_set1_epi32((int32_t)s->yawfx);
        const __m256i m8    = _mm256_set1_epi32(0xff);
        const __m256i rbm   = _mm256_set1_epi32(0xFF00FF), gm = _mm256_set1_epi32(0x00FF00);
        const __m256i one   = _mm256_set1_epi32(1);
        const __m256i c256  = _mm256_set1_epi32(256);
        for (; i + 8 <= p->w; i += 8, k += 8) {
            __m256i col = _mm256_add_epi32(_mm256_loadu_si256((const __m256i *)&p->baselon[k]), yawv);
            __m256i ge  = _mm256_cmpgt_epi32(col, _mm256_sub_epi32(EWFXv, one));   /* col >= EWFX */
            col = _mm256_sub_epi32(col, _mm256_and_si256(ge, EWFXv));
            __m256i c0 = _mm256_srli_epi32(col, 16);
            __m256i c1 = _mm256_add_epi32(c0, one);
            c1 = _mm256_sub_epi32(c1, _mm256_and_si256(_mm256_cmpeq_epi32(c1, EW), EW));
            __m256i wu = _mm256_and_si256(_mm256_srli_epi32(col, 8), m8);
            __m256i r0 = _mm256_loadu_si256((const __m256i *)&p->row0[k]);
            __m256i r1 = _mm256_loadu_si256((const __m256i *)&p->row1[k]);
            __m256i p00 = _mm256_i32gather_epi32((const int *)p->src, _mm256_add_epi32(r0, c0), 4);
            __m256i p01 = _mm256_i32gather_epi32((const int *)p->src, _mm256_add_epi32(r0, c1), 4);
            __m256i p10 = _mm256_i32gather_epi32((const int *)p->src, _mm256_add_epi32(r1, c0), 4);
            __m256i p11 = _mm256_i32gather_epi32((const int *)p->src, _mm256_add_epi32(r1, c1), 4);
            __m256i iwu = _mm256_sub_epi32(c256, wu);
            #define MIXV(a, b, w, iw) _mm256_or_si256( \
                _mm256_and_si256(_mm256_srli_epi32(_mm256_add_epi32( \
                    _mm256_mullo_epi32(_mm256_and_si256(a, rbm), iw), \
                    _mm256_mullo_epi32(_mm256_and_si256(b, rbm), w)), 8), rbm), \
                _mm256_and_si256(_mm256_srli_epi32(_mm256_add_epi32( \
                    _mm256_mullo_epi32(_mm256_and_si256(a, gm), iw), \
                    _mm256_mullo_epi32(_mm256_and_si256(b, gm), w)), 8), gm))
            __m256i top = MIXV(p00, p01, wu, iwu);
            __m256i bot = MIXV(p10, p11, wu, iwu);
            __m256i wvv = _mm256_cvtepu16_epi32(_mm_loadu_si128((const __m128i *)&p->wv[k]));
            __m256i iwv = _mm256_sub_epi32(c256, wvv);
            _mm256_storeu_si256((__m256i *)&s->out->px[k], MIXV(top, bot, wvv, iwv));
            #undef MIXV
        }
#endif
        for (; i < p->w; i++, k++) {
            int64_t col = p->baselon[k] + s->yawfx;
            if (col >= EWFX) col -= EWFX;
            int c0 = (int)(col >> 16);
            int c1 = c0 + 1 == p->ew ? 0 : c0 + 1;
            unsigned wu = (unsigned)(col >> 8) & 0xff;
            const uint32_t *r0 = p->src + p->row0[k], *r1 = p->src + p->row1[k];
            uint32_t top = mix_xrgb(r0[c0], r0[c1], wu);
            uint32_t bot = mix_xrgb(r1[c0], r1[c1], wu);
            s->out->px[k] = mix_xrgb(top, bot, p->wv[k]);
        }
    }
    return NULL;
}
#ifdef PANO_MP_SERVICES
typedef struct { pano_t *p; fb_t *out; int64_t yawfx; } mpctx_t;
static void render_slice_mp(void *v, int idx, int nproc) {
    mpctx_t *c = (mpctx_t *)v;
    long h = c->p->h;
    slice_t sl = { c->p, c->out, c->yawfx, (int)(h * idx / nproc), (int)(h * (idx + 1) / nproc) };
    render_slice(&sl);
}
#endif
void pano_render(pano_t *p, fb_t *out, double yaw_turns) {
    double t = fmod(yaw_turns, 1.0); if (t < 0) t += 1.0;
    int64_t yawfx = (int64_t)(t * p->ew * 65536.0);
#ifdef PANO_MP_SERVICES
    mpctx_t c = { p, out, yawfx };
    plat_run_on_all(render_slice_mp, &c);   /* every core renders a row-slice */
#else
    int nt = p->nthreads;
    pthread_t th[8]; slice_t sl[8];
    for (int i = 0; i < nt; i++) {
        sl[i] = (slice_t){ p, out, yawfx, p->h * i / nt, p->h * (i + 1) / nt };
        if (i < nt - 1) pthread_create(&th[i], NULL, render_slice, &sl[i]);
    }
    render_slice(&sl[nt - 1]);
    for (int i = 0; i < nt - 1; i++) pthread_join(th[i], NULL);
#endif
}
void pano_destroy(pano_t *p) {
    free(p->src); free(p->baselon); free(p->row0); free(p->row1); free(p->wv); free(p);
}
