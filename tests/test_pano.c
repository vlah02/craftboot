#include "t.h"
#include "core/render.h"
#include <stdlib.h>
#include <string.h>

static img_t make_src(int ew, int eh) {          /* column index encoded in R and G */
    img_t s = { malloc((size_t)ew * eh * 4), ew, eh };
    for (int j = 0; j < eh; j++)
        for (int i = 0; i < ew; i++) {
            uint8_t *p = &s.rgba[((long)j * ew + i) * 4];
            p[0] = (uint8_t)i; p[1] = (uint8_t)(i >> 8); p[2] = 99; p[3] = 255;
        }
    return s;
}
static int uniform_in_uniform_out(void) {
    img_t s = { malloc(256 * 128 * 4), 256, 128 };
    for (long i = 0; i < 256 * 128; i++) memcpy(&s.rgba[i * 4], "\x30\x60\x90\xff", 4);
    pano_t *p = pano_create(&s, 80, 45, 140.f);
    uint32_t buf[80 * 45]; fb_t fb = { buf, 80, 45 };
    pano_render(p, &fb, 0.37);
    for (int i = 0; i < 80 * 45; i++) OK(buf[i] == 0x306090);
    pano_destroy(p); free(s.rgba);
    return 0;
}
static int yaw_wraps_full_turn(void) {
    img_t s = make_src(256, 128);
    pano_t *p = pano_create(&s, 80, 45, 140.f);
    uint32_t a[80 * 45], b[80 * 45];
    fb_t fa = { a, 80, 45 }, fbb = { b, 80, 45 };
    pano_render(p, &fa, 0.23);
    pano_render(p, &fbb, 1.23);
    OK(memcmp(a, b, sizeof a) == 0);
    pano_render(p, &fbb, 0.73);
    OK(memcmp(a, b, sizeof a) != 0);      /* different yaw -> different frame */
    pano_destroy(p); free(s.rgba);
    return 0;
}
static int center_maps_to_yaw_column(void) {
    img_t s = make_src(256, 128);
    pano_t *p = pano_create(&s, 81, 45, 140.f);
    uint32_t buf[81 * 45]; fb_t fb = { buf, 81, 45 };
    pano_render(p, &fb, 0.25);            /* center ray lon=0 -> col = 0.25*EW = 64 */
    uint32_t c = buf[22 * 81 + 40];
    int col = (int)(c >> 16 & 0xff) | (int)(c >> 8 & 0xff) << 8;
    OK(col >= 63 && col <= 65);
    pano_destroy(p); free(s.rgba);
    return 0;
}
static int uniform_square_output(void) {
    /* non-16:9 output (square) exercises the vertical-FOV term tv = th*h/w at
     * an aspect the other cases don't cover; a uniform source must still map
     * to a uniform frame with no row-clamp artifacts. 64 is a multiple of 8
     * so every row is pure AVX2 body (no scalar tail). */
    img_t s = { malloc(200 * 100 * 4), 200, 100 };
    for (long i = 0; i < 200 * 100; i++) memcpy(&s.rgba[i * 4], "\x22\x44\x88\xff", 4);
    pano_t *p = pano_create(&s, 64, 64, 100.f);
    uint32_t buf[64 * 64]; fb_t fb = { buf, 64, 64 };
    pano_render(p, &fb, 0.5);
    for (int i = 0; i < 64 * 64; i++) OK(buf[i] == 0x224488);
    pano_destroy(p); free(s.rgba);
    return 0;
}
int main(void) { RUN(uniform_in_uniform_out); RUN(yaw_wraps_full_turn);
                 RUN(center_maps_to_yaw_column); RUN(uniform_square_output); return 0; }
