#include "core/render.h"
#include "core/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int main(void) {
    img_t eq = img_load("assets/panoramas/1.16_nether.jpg");
    if (!eq.rgba) { fprintf(stderr, "load failed\n"); return 1; }
    pano_t *p = pano_create(&eq, 1920, 1080, 140.f);
    fb_t fb = { malloc((size_t)1920 * 1080 * 4), 1920, 1080 };
    struct timespec a, b;
    pano_render(p, &fb, 0.1);                      /* warm */
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < 200; i++) pano_render(p, &fb, i / 200.0);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ms = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 200 / 1e6;
    printf("pano 1920x1080: %.2f ms/frame (%.0f fps)\n", ms, 1000 / ms);
    return ms < 3.0 ? 0 : 1;
}
