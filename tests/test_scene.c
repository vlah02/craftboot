/* Covers the visual half of menu.c -- scene_load, draw_scene, draw_loading and
 * the asset pickers. Those are all `static`, so this file #includes menu.c
 * directly to reach them; the Makefile links it WITHOUT menu.o to avoid
 * duplicate symbols (see the test_scene rule).
 *
 * This is the code path that is otherwise only verifiable by booting and
 * looking at it, so the assertions here are about composition invariants
 * rather than exact pixels: a frame gets drawn, and the UI is drawn ON TOP of
 * the (optionally blurred) panorama rather than being blurred with it. */
#include "t.h"
#include "core/menu.c"

#define W 640
#define H 360

static config_t cfg;
static scene_t  scene;
static int      loaded;

static int load_once(void) {
    if (!loaded) {
        OK(config_load(&cfg, "boot_entries.json") == 0);
        OK(scene_load(&scene, &cfg, "assets", W, H) == 0);
        loaded = 1;
    }
    return 0;
}

static int frame_is_not_flat(const uint32_t *px) {
    for (long i = 1; i < (long)W * H; i++) if (px[i] != px[0]) return 1;
    return 0;
}

/* scene_load pulls real fonts/logo/button art off disk; if any of it silently
 * failed to load the menu would render as bare rectangles at boot. */
static int scene_loads_real_assets(void) {
    if (load_once()) return 1;
    OK(scene.small.atlas.rgba != NULL);
    OK(scene.button.atlas.rgba != NULL);
    OK(scene.btn.rgba != NULL && scene.btn_hi.rgba != NULL);
    OK(scene.splash_txt[0] != '\0');
    return 0;
}

static int draw_scene_renders_a_frame(void) {
    if (load_once()) return 1;
    static uint32_t px[W * H];
    fb_t fb = { px, W, H };
    menustate_t m; ms_init(&m, &cfg);
    draw_scene(&fb, &scene, &m, 1.0, 0.25);
    OK(frame_is_not_flat(px));
    return 0;
}

/* The ordering guarantee: fb_blur runs on the panorama BEFORE the logo/buttons
 * /text are drawn, so the UI stays crisp no matter how soft the background is.
 * Rendering the same scene with and without blur must therefore change the
 * background while leaving an opaque button pixel bit-identical. If fb_blur
 * ever moves after the UI drawing, that pixel changes and this fails. */
static int blur_softens_the_panorama_under_the_ui(void) {
    if (load_once()) return 1;
    static uint32_t sharp[W * H], soft[W * H];
    menustate_t m; ms_init(&m, &cfg);

    int saved_blur = scene.pano_blur;
    uint32_t *saved_tmp = scene.blur_tmp;

    fb_t a = { sharp, W, H };
    scene.pano_blur = 0;
    draw_scene(&a, &scene, &m, 1.0, 0.25);

    fb_t b = { soft, W, H };
    scene.pano_blur = 12;
    scene.blur_tmp = malloc((size_t)W * H * 4);
    OK(scene.blur_tmp != NULL);
    draw_scene(&b, &scene, &m, 1.0, 0.25);

    OK(memcmp(sharp, soft, sizeof sharp) != 0);        /* the blur did something */

    /* interior of the first button, left of its centred label (same geometry
     * draw_scene uses) -- opaque 9-slice art, so it must survive untouched */
    int bw = W * 44 / 100 > 620 ? 620 : W * 44 / 100;
    int bh = H * 85 / 1000 < 34 ? 34 : H * 85 / 1000;
    int by = H * 45 / 100, cx = W / 2;
    long ui = (long)(by + bh / 2) * W + (cx - bw / 2 + 6);
    OK(sharp[ui] == soft[ui]);

    free(scene.blur_tmp);
    scene.pano_blur = saved_blur; scene.blur_tmp = saved_tmp;
    return 0;
}

static int draw_loading_renders_a_frame(void) {
    if (load_once()) return 1;
    static uint32_t px[W * H];
    fb_t fb = { px, W, H };
    draw_loading(&fb, &scene, "Ubuntu", 0.5);
    OK(frame_is_not_flat(px));
    return 0;
}

/* pick_logo is a substring lookup into assets/logo_map.json; a mismatch means
 * a world boots with the wrong wordmark. */
static int pick_logo_maps_a_known_world(void) {
    char out[256];
    pick_logo("assets", "assets/panoramas/1.16_nether.jpg", out, sizeof out);
    OK(strstr(out, "minecraft_nether.png") != NULL);
    pick_logo("assets", "assets/panoramas/9.99_not_a_world.jpg", out, sizeof out);
    OK(strstr(out, "minecraft_classic.png") != NULL);   /* unmapped -> fallback */
    return 0;
}

static int pick_splash_returns_text(void) {
    char out[128];
    OK(pick_splash("assets", out, sizeof out) == 0);
    OK(out[0] != '\0');
    return 0;
}

int main(void) {
    RUN(scene_loads_real_assets);
    RUN(draw_scene_renders_a_frame);
    RUN(blur_softens_the_panorama_under_the_ui);
    RUN(draw_loading_renders_a_frame);
    RUN(pick_logo_maps_a_known_world);
    RUN(pick_splash_returns_text);
    return 0;
}
