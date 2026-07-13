#include "t.h"
#include "core/render.h"
#include "core/assets.h"
static int load_and_measure(void) {
    font_t f;
    OK(font_load(&f, "assets/fonts/baked/small.png", "assets/fonts/baked/small.json") == 0);
    OK(f.size == 32);
    OK(text_width(&f, "AB") == f.g['A' - 32].adv + f.g['B' - 32].adv);
    OK(text_width(&f, "") == 0);
    return 0;
}
static int draws_pixels(void) {
    font_t f;
    OK(font_load(&f, "assets/fonts/baked/small.png", "assets/fonts/baked/small.json") == 0);
    static uint32_t buf[200 * 60];
    fb_t fb = { buf, 200, 60 };
    fb_fill(&fb, 0);
    draw_text(&fb, &f, "A", 4, 4, 0xFF0000);
    long red = 0; for (int i = 0; i < 200 * 60; i++) if (buf[i] >> 16 & 0xff) red++;
    OK(red > 10);                                     /* glyph coverage exists */
    fb_fill(&fb, 0);
    draw_text_shadow(&fb, &f, "A", 4, 4, 0xFFFFFF, 0x3F3F3F);
    long grey = 0; for (int i = 0; i < 200 * 60; i++) if (buf[i] == 0x3F3F3F) grey++;
    OK(grey > 0);                                     /* shadow drawn */
    return 0;
}
static int renders_to_img(void) {
    font_t f;
    OK(font_load(&f, "assets/fonts/baked/splash.png", "assets/fonts/baked/splash.json") == 0);
    img_t t = text_render(&f, "Hi!", 0xFFFF00, 2);
    OK(t.w > 0 && t.h > 0);
    long a = 0; for (int i = 0; i < t.w * t.h; i++) a += t.rgba[i * 4 + 3];
    OK(a > 0);
    img_free(&t);
    return 0;
}
static int escaped_glyphs_distinct(void) {
    font_t f;
    OK(font_load(&f, "assets/fonts/baked/small.png", "assets/fonts/baked/small.json") == 0);
    OK(f.g['"' - 32].adv > 0);                    /* quote glyph exists */
    OK(f.g['\\' - 32].adv > 0);                   /* backslash glyph exists */
    OK(text_width(&f, "\"") > 0);
    return 0;
}
/* text_render() sizes its scratch buffers from text_width()+outline; the
 * degenerate empty string and an over-long string must both produce a valid
 * image with no under-allocation (checked under ASan in test-asan/CI). */
static int renders_edge_strings(void) {
    font_t f;
    OK(font_load(&f, "assets/fonts/baked/splash.png", "assets/fonts/baked/splash.json") == 0);
    img_t empty = text_render(&f, "", 0xFFFFFF, 2);
    OK(empty.rgba && empty.w > 0 && empty.h > 0);   /* width = just the outline pad */
    char big[513]; memset(big, 'W', 512); big[512] = 0;
    img_t lng = text_render(&f, big, 0xFFFF00, 2);
    OK(lng.rgba && lng.w > empty.w);
    long a = 0; for (int i = 0; i < lng.w * lng.h; i++) a += lng.rgba[i * 4 + 3];
    OK(a > 0);                                       /* the long string drew ink */
    img_free(&empty); img_free(&lng);
    return 0;
}
int main(void) { RUN(load_and_measure); RUN(draws_pixels); RUN(renders_to_img);
                 RUN(escaped_glyphs_distinct); RUN(renders_edge_strings); return 0; }
