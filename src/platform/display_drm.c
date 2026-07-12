/* placeholder until Task 11 implements the real KMS/DRM backend */
#include "platform/display.h"
#include <stdio.h>
#include <stdlib.h>
struct display { fb_t fb; };
display_t *display_open(int w, int h) {
    (void)w; (void)h;
    fprintf(stderr, "[craftboot] DRM display backend not implemented until Task 11\n");
    return NULL;
}
fb_t *display_fb(display_t *d) { (void)d; return NULL; }
void display_flip(display_t *d) { (void)d; }
void display_close(display_t *d) { (void)d; }
