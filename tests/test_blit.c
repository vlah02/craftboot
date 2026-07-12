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
int main(void) {
    RUN(mix_endpoints); RUN(fill_and_rect); RUN(alpha_blit);
    RUN(scale_solid_stays_solid); RUN(rotate_keeps_content); RUN(loads_repo_png);
    return 0;
}
