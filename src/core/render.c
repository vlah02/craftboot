#include "core/render.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
