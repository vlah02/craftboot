#include "t.h"
#include "core/render.h"
#include "core/assets.h"
static uint32_t buf[64 * 64];
static fb_t fb = { buf, 64, 64 };

static int mix_endpoints(void) {
    OK(mix_xrgb(0x000000, 0xFFFFFF, 0) == 0x000000);
    OK(mix_xrgb(0x000000, 0xFFFFFF, 256) == 0xFFFFFF);
    OK(mix_xrgb(0x000000, 0x0000FF, 128) == 0x00007F);
    return 0;
}
static int fill_and_rect(void) {
    fb_fill(&fb, 0x112233);
    OK(buf[0] == 0x112233 && buf[64*64-1] == 0x112233);
    fill_rect(&fb, 2, 2, 4, 4, 0xFF0000);
    OK(buf[2*64+2] == 0xFF0000 && buf[6*64+6] == 0x112233);
    return 0;
}
static int alpha_blit(void) {
    fb_fill(&fb, 0x000000);
    uint8_t px[4*4] = { 255,0,0,255,  0,255,0,128,  0,0,255,0,  255,255,255,255 };
    img_t s = { px, 2, 2 };
    blit(&fb, &s, 0, 0);
    OK(buf[0] == 0xFF0000);                       /* opaque red */
    OK((buf[1] >> 8 & 0xff) >= 0x7e && (buf[1] >> 8 & 0xff) <= 0x81); /* half green */
    OK(buf[64] == 0x000000);                      /* transparent -> untouched */
    OK(buf[65] == 0xFFFFFF);
    return 0;
}
static int scale_solid_stays_solid(void) {
    uint8_t px[4*4]; for (int i = 0; i < 16; i += 4) { px[i]=10; px[i+1]=200; px[i+2]=30; px[i+3]=255; }
    img_t s = { px, 2, 2 };
    img_t big = img_scaled(&s, 8, 8);
    OK(big.w == 8 && big.h == 8);
    OK(big.rgba[0] == 10 && big.rgba[1] == 200 && big.rgba[2] == 30 && big.rgba[3] == 255);
    OK(big.rgba[(8*7+7)*4+1] == 200);
    img_free(&big);
    return 0;
}
static int rotate_keeps_content(void) {
    uint8_t px[4*4] = {0}; px[0]=255; px[3]=255;   /* one red px, rest transparent */
    img_t s = { px, 2, 2 };
    img_t r = img_rotated(&s, 18.0f);
    OK(r.w >= 2 && r.h >= 2);
    long acc = 0; for (int i = 0; i < r.w*r.h; i++) acc += r.rgba[i*4+3];
    OK(acc > 0);                                   /* alpha survived rotation */
    img_free(&r);
    return 0;
}
static int loads_repo_png(void) {
    img_t l = img_load("assets/button.png");
    OK(l.rgba && l.w == 200 && l.h == 20);
    img_free(&l);
    img_t j = img_load("assets/panoramas/1.16_nether.jpg");
    OK(j.rgba && j.w == 2800 && j.h == 1400);
    img_free(&j);
    return 0;
}
static int nineslice_narrow_sprite_safe(void) {
    static uint32_t big[64 * 64];
    fb_t f = { big, 64, 64 };
    fb_fill(&f, 0);
    uint8_t px[4 * 2 * 8];                    /* 2 wide, 8 tall: w < 2*h */
    for (int i = 0; i < 2 * 8; i++) { px[i*4]=200; px[i*4+1]=10; px[i*4+2]=10; px[i*4+3]=255; }
    img_t s = { px, 2, 8 };
    blit_9slice(&f, &s, 4, 4, 30, 12);        /* must not crash */
    OK(big[8 * 64 + 8] != 0);                 /* drew something inside the rect */
    /* normal case still works: 200x20-like wide sprite */
    static uint8_t wide[4 * 40 * 8];
    for (int i = 0; i < 40 * 8; i++) { wide[i*4]=10; wide[i*4+1]=200; wide[i*4+2]=10; wide[i*4+3]=255; }
    img_t s2 = { wide, 40, 8 };
    blit_9slice(&f, &s2, 0, 40, 50, 16);
    OK((big[48 * 64 + 25] >> 8 & 0xff) > 100);
    return 0;
}
/* blit()/put_rgba() must clip per-pixel: negative coords, a sprite that
 * straddles the framebuffer edge, and one placed entirely off-screen must
 * all be safe (no OOB read/write) and must leave out-of-bounds framebuffer
 * pixels untouched. This is the memory-safety surface exercised under ASan
 * in CI (tests/fuzz_parse.c and the sanitizer CI job). */
static int blit_bounds_safety(void) {
    /* 4x4 opaque sprite, each pixel a distinct color so mapping is checkable */
    uint8_t px[4 * 4 * 4];
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) {
            uint8_t *p = &px[(j * 4 + i) * 4];
            p[0] = p[1] = p[2] = (uint8_t)((j * 4 + i + 1) * 15);
            p[3] = 255;
        }
    img_t s = { px, 4, 4 };
    uint32_t sentinel = 0x445566;

    /* negative coords: only sprite pixel (3,3) lands in-bounds at fb (0,0) */
    fb_fill(&fb, sentinel);
    blit(&fb, &s, -3, -3);
    uint32_t c33 = px[(3 * 4 + 3) * 4] * 0x010101u;
    OK(buf[0] == c33);
    OK(buf[1] == sentinel && buf[64] == sentinel);

    /* partially past the right edge: columns 0-1 visible, 2-3 clipped */
    fb_fill(&fb, sentinel);
    blit(&fb, &s, fb.w - 2, 0);
    uint32_t c00 = px[0] * 0x010101u, c10 = px[1 * 4] * 0x010101u;
    OK(buf[fb.w - 2] == c00);
    OK(buf[fb.w - 1] == c10);
    OK(buf[1] == sentinel);   /* untouched elsewhere */

    /* fully off-screen: no visible pixel changes, no crash */
    fb_fill(&fb, sentinel);
    blit(&fb, &s, fb.w + 50, 0);
    OK(buf[0] == sentinel && buf[64 * 64 - 1] == sentinel);
    blit(&fb, &s, -50, -50);
    OK(buf[0] == sentinel && buf[64 * 64 - 1] == sentinel);
    return 0;
}

static int fill_rect_straddles_edge(void) {
    fb_fill(&fb, 0);
    /* rect from (-2,-2) sized 68x68 covers the whole 64x64 fb and overflows
     * every edge; must clip cleanly (no OOB write) and fill everything visible */
    fill_rect(&fb, -2, -2, 68, 68, 0xABCDEF);
    OK(buf[0] == 0xABCDEF);
    OK(buf[64 * 64 - 1] == 0xABCDEF);
    OK(buf[32 * 64 + 32] == 0xABCDEF);
    return 0;
}

int main(void) {
    RUN(mix_endpoints); RUN(fill_and_rect); RUN(alpha_blit);
    RUN(scale_solid_stays_solid); RUN(rotate_keeps_content); RUN(loads_repo_png);
    RUN(nineslice_narrow_sprite_safe);
    RUN(blit_bounds_safety); RUN(fill_rect_straddles_edge);
    return 0;
}
