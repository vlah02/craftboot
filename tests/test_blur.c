/* fb_blur(): the uniform screen-space box blur applied to low-res panoramas
 * (see src/core/menu.c -- the 1.12 classic is a 256^2-face source that would
 * otherwise go sharp-centre / smeared-sides at a wide FOV). */
#include "t.h"
#include "core/render.h"

#define W 64
#define H 8

static void fill(uint32_t *px, uint32_t c) {
    for (int i = 0; i < W * H; i++) px[i] = c;
}

/* A box average over a constant field is that same constant, everywhere --
 * including the borders, where the window clamp-extends instead of shrinking
 * (a shrinking window with a fixed divisor would darken the edges). */
static int uniform_stays_uniform(void) {
    uint32_t px[W * H], tmp[W * H];
    fb_t fb = { px, W, H };
    fill(px, 0x102030);
    fb_blur(&fb, 5, tmp);
    for (int i = 0; i < W * H; i++) OK(px[i] == 0x102030);
    return 0;
}

static int noop_cases_leave_the_frame_alone(void) {
    uint32_t px[W * H], ref[W * H], tmp[W * H];
    fb_t fb = { px, W, H };
    for (int i = 0; i < W * H; i++) px[i] = ref[i] = (uint32_t)(i * 2654435761u) >> 8;
    fb_blur(&fb, 0, tmp);                       /* radius < 1 */
    OK(memcmp(px, ref, sizeof px) == 0);
    fb_blur(&fb, -3, tmp);
    OK(memcmp(px, ref, sizeof px) == 0);
    fb_blur(&fb, 5, NULL);                      /* no scratch buffer */
    OK(memcmp(px, ref, sizeof px) == 0);
    return 0;
}

/* Half white / half black: only the band within `radius` of the seam may
 * change. Beyond it the window sees one solid colour, so those pixels must
 * come back bit-identical -- this is what makes the blur local rather than a
 * whole-frame wash. */
static int step_edge_blurs_only_near_the_seam(void) {
    uint32_t px[W * H], tmp[W * H];
    fb_t fb = { px, W, H };
    const int radius = 4, seam = W / 2;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            px[y * W + x] = x < seam ? 0xFFFFFF : 0x000000;
    fb_blur(&fb, radius, tmp);

    for (int y = 0; y < H; y++) {
        OK(px[y * W + 0] == 0xFFFFFF);                       /* far left, clamped */
        OK(px[y * W + (seam - radius - 1)] == 0xFFFFFF);     /* last fully-white window */
        OK(px[y * W + (W - 1)] == 0x000000);                 /* far right, clamped */
        OK(px[y * W + (seam + radius)] == 0x000000);         /* first fully-black window */
        uint32_t mid = px[y * W + (seam - 1)];               /* straddles the seam */
        unsigned r = mid >> 16 & 0xff;
        OK(r > 0 && r < 0xff);
    }
    return 0;
}

/* Radius far larger than the image exercises every clamp path at once; the
 * result must stay in bounds (ASan/UBSan enforce that in `make test-asan`)
 * and a constant field must still come back unchanged. */
static int radius_larger_than_the_image_is_safe(void) {
    uint32_t px[W * H], tmp[W * H];
    fb_t fb = { px, W, H };
    fill(px, 0x448866);
    fb_blur(&fb, 200, tmp);
    for (int i = 0; i < W * H; i++) OK(px[i] == 0x448866);
    return 0;
}

/* Channels must stay independent: a blur that leaked across the packed byte
 * lanes would tint the frame rather than soften it. */
static int channels_do_not_bleed(void) {
    uint32_t px[W * H], tmp[W * H];
    fb_t fb = { px, W, H };
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            px[y * W + x] = x < W / 2 ? 0xFF0000 : 0x0000FF;   /* pure red | pure blue */
    fb_blur(&fb, 3, tmp);
    for (int i = 0; i < W * H; i++) OK((px[i] >> 8 & 0xff) == 0);  /* green stays 0 */
    return 0;
}

int main(void) {
    RUN(uniform_stays_uniform);
    RUN(noop_cases_leave_the_frame_alone);
    RUN(step_edge_blurs_only_near_the_seam);
    RUN(radius_larger_than_the_image_is_safe);
    RUN(channels_do_not_bleed);
    return 0;
}
