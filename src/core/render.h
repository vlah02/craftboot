#pragma once
#include <stdint.h>

typedef struct { uint32_t *px; int w, h; } fb_t;
typedef struct { uint8_t *rgba; int w, h; } img_t;

uint32_t mix_xrgb(uint32_t a, uint32_t b, unsigned w);
void fb_fill(fb_t *f, uint32_t c);
void fill_rect(fb_t *f, int x, int y, int w, int h, uint32_t c);
void blit(fb_t *f, const img_t *s, int x, int y);
void blit_scaled(fb_t *f, const img_t *s, int x, int y, int w, int h);
img_t img_scaled(const img_t *s, int w, int h);
img_t img_rotated(const img_t *s, float deg);
void blit_9slice(fb_t *f, const img_t *s, int x, int y, int w, int h);

/* panorama (Task 7) */
typedef struct pano pano_t;
pano_t *pano_create(const img_t *equirect, int out_w, int out_h, float fov_deg);
void pano_render(pano_t *p, fb_t *out, double yaw_turns);
void pano_destroy(pano_t *p);

/* text (Task 6) */
typedef struct { short x, y, w, h, adv; } glyph_t;
typedef struct { img_t atlas; glyph_t g[95]; int size; } font_t;
int  text_width(const font_t *f, const char *s);
void draw_text(fb_t *fb, const font_t *f, const char *s, int x, int y, uint32_t rgb);
void draw_text_shadow(fb_t *fb, const font_t *f, const char *s, int x, int y,
                      uint32_t rgb, uint32_t shadow);
img_t text_render(const font_t *f, const char *s, uint32_t rgb, int outline_px);
