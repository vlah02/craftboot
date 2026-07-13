#pragma once
#include "core/render.h"
typedef struct display display_t;
display_t *display_open(int w, int h);      /* DRM ignores w/h, uses native mode */
fb_t *display_fb(display_t *d);             /* cached staging buffer to draw into */
void display_flip(display_t *d);            /* present + vsync/pace */
void display_close(display_t *d);
