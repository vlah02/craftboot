/* placeholder until Task 12 implements the real evdev backend */
#include "platform/display.h"
#include "platform/input.h"
#include <stdio.h>
struct input { int unused; };
input_t *input_open(display_t *d) {
    (void)d;
    fprintf(stderr, "[craftboot] evdev input backend not implemented until Task 12\n");
    return NULL;
}
action_t input_poll(input_t *in) { (void)in; return ACT_QUIT; }
void input_close(input_t *in) { (void)in; }
