/* Link-time stubs for the platform hooks src/core/menu.c references.
 *
 * The host harness exercises the pure menu/render logic (ms_* state machine,
 * draw_scene) and never enters menu_run(), which is the only caller of these.
 * The real implementations are firmware-only (src/efi/display_efi.c,
 * src/efi/input_efi.c) and cannot link or run on the host.
 *
 * They abort instead of returning dummy values so that if a future test ever
 * does reach menu_run(), it fails loudly at the call site rather than silently
 * rendering into a NULL framebuffer. */
#include "platform/display.h"
#include "platform/input.h"
#include <stdio.h>
#include <stdlib.h>

static void unreachable(const char *fn) {
    fprintf(stderr, "stub_platform: %s() called in a host test build "
                    "(menu_run is firmware-only)\n", fn);
    abort();
}

fb_t *display_fb(display_t *d)   { (void)d;  unreachable("display_fb");  return NULL; }
void  display_flip(display_t *d) { (void)d;  unreachable("display_flip"); }
action_t input_poll(input_t *in) { (void)in; unreachable("input_poll");  return ACT_NONE; }
