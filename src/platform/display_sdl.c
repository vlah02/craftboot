#ifdef DEV
#include "platform/display.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct display { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *tex; fb_t fb; };
display_t *display_open(int w, int h) {
    SDL_Init(SDL_INIT_VIDEO);
    display_t *d = calloc(1, sizeof *d);
    d->win = SDL_CreateWindow("craftboot", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              w, h, 0);
    if (!d->win) { SDL_Quit(); free(d); return NULL; }
    d->ren = SDL_CreateRenderer(d->win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (d->ren)
        d->tex = SDL_CreateTexture(d->ren, SDL_PIXELFORMAT_XRGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, w, h);
    d->fb = (fb_t){ malloc((size_t)w * h * 4), w, h };
    return d;
}
fb_t *display_fb(display_t *d) { return &d->fb; }
void display_flip(display_t *d) {
    if (d->ren && d->tex) {
        SDL_UpdateTexture(d->tex, NULL, d->fb.px, d->fb.w * 4);
        SDL_RenderCopy(d->ren, d->tex, NULL, NULL);
        SDL_RenderPresent(d->ren);
    }
    /* CRAFTBOOT_SHOT=path[:N] -> dump staging buffer as raw XRGB after N flips (default 90), then exit */
    static int flips = 0;
    const char *shot = getenv("CRAFTBOOT_SHOT");
    if (shot) {
        flips++;
        char path[256]; int n = 90;
        const char *colon = strrchr(shot, ':');
        if (colon && atoi(colon + 1) > 0) { n = atoi(colon + 1); snprintf(path, sizeof path, "%.*s", (int)(colon - shot), shot); }
        else snprintf(path, sizeof path, "%s", shot);
        if (flips >= n) {
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(d->fb.px, 4, (size_t)d->fb.w * d->fb.h, f); fclose(f); }
            exit(0);
        }
    }
    /* CRAFTBOOT_SHOT_SEQ=dir:first:count -> after flip number `first`, dump
     * every subsequent staging frame as dir/frame_%04d.raw (raw XRGB, no
     * header) until `count` frames have been written, then exit. DEV-only
     * capture hook for tools/make_demo.py; independent of CRAFTBOOT_SHOT. */
    static int seq_flips = 0, seq_written = 0;
    const char *seq = getenv("CRAFTBOOT_SHOT_SEQ");
    if (seq) {
        seq_flips++;
        char dir[200]; int first = 0, count = 0;
        char buf[256]; snprintf(buf, sizeof buf, "%s", seq);
        char *p1 = strchr(buf, ':');
        char *p2 = p1 ? strchr(p1 + 1, ':') : NULL;
        if (p1 && p2) {
            *p1 = 0; *p2 = 0;
            snprintf(dir, sizeof dir, "%.199s", buf);
            first = atoi(p1 + 1);
            count = atoi(p2 + 1);
        }
        if (count > 0 && seq_flips > first && seq_written < count) {
            char path[256];
            snprintf(path, sizeof path, "%.200s/frame_%04d.raw", dir, seq_written % 10000);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(d->fb.px, 4, (size_t)d->fb.w * d->fb.h, f); fclose(f); }
            seq_written++;
        }
        if (count > 0 && seq_written >= count) exit(0);
    }
}
void display_close(display_t *d) {
    if (!d) return;
    if (d->tex) SDL_DestroyTexture(d->tex);
    if (d->ren) SDL_DestroyRenderer(d->ren);
    if (d->win) SDL_DestroyWindow(d->win);
    SDL_Quit();
    free(d->fb.px);
    free(d);
}
#endif
