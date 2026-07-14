#include "core/render.h"
#include "core/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int main(void) {
    img_t eq = img_load("assets/panoramas/1.16_nether.jpg");
    if (!eq.rgba) { fprintf(stderr, "load failed\n"); return 1; }
    pano_t *p = pano_create(&eq, 1920, 1080, 140.f, 0.f);
    fb_t fb = { malloc((size_t)1920 * 1080 * 4), 1920, 1080 };
    struct timespec a, b;
    pano_render(p, &fb, 0.1);                      /* warm */
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < 200; i++) pano_render(p, &fb, i / 200.0);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ms = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 200 / 1e6;
    /* Local default is 3.0 ms (the target-HW regression bar). Shared CI
     * runners are slower than the target hardware, so CI sets a more
     * generous CRAFTBOOT_BENCH_MAX_MS to avoid false-failing on runner
     * jitter while still catching a catastrophic regression. */
    const char *e = getenv("CRAFTBOOT_BENCH_MAX_MS");
    double max = e ? atof(e) : 3.0;
    printf("pano 1920x1080: %.2f ms/frame (%.0f fps), max %.2f ms\n", ms, 1000 / ms, max);
    return ms < max ? 0 : 1;
}
