/* Differential harness: scalar vs AVX2 panorama renderer.
 *
 * Built twice by `make diff-pano` — once normally (AVX2 vector body) and once
 * with -DPANO_NO_AVX2 (scalar only). Each build writes the raw framebuffer
 * bytes of the same frames to stdout; the make target cmp's the two dumps.
 * W=1923 = 240*8 + 3 so every row exercises both the 8-wide vector body and
 * a 3-pixel scalar tail; the yaw set includes values that force the wrap.
 */
#include "core/render.h"
#include "core/assets.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    img_t eq = img_load("assets/panoramas/1.16_nether.jpg");
    if (!eq.rgba) { fprintf(stderr, "diff_pano: load failed\n"); return 1; }
    int W = 1923, H = 45;
    pano_t *p = pano_create(&eq, W, H, 140.f);
    uint32_t *buf = malloc((size_t)W * H * 4);
    fb_t fb = { buf, W, H };
    const double yaws[] = { 0.0, 0.37, 0.73 };
    for (int y = 0; y < 3; y++) {
        pano_render(p, &fb, yaws[y]);
        if (fwrite(buf, 4, (size_t)W * H, stdout) != (size_t)W * H) {
            fprintf(stderr, "diff_pano: short write\n"); return 1;
        }
    }
    pano_destroy(p);
    free(buf);
    free(eq.rgba);
    return 0;
}
