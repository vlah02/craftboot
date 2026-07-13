#ifdef DEV
#include "platform/display.h"
#include "platform/input.h"
#include <SDL.h>
#include <stdlib.h>
struct input { int unused; };
input_t *input_open(display_t *d) { (void)d; return calloc(1, sizeof(input_t)); }
action_t input_poll(input_t *in) {
    if (!in) return ACT_NONE;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return ACT_QUIT;
        if (e.type != SDL_KEYDOWN) continue;
        switch (e.key.keysym.sym) {
        case SDLK_UP: case SDLK_w:   return ACT_UP;
        case SDLK_DOWN: case SDLK_s: return ACT_DOWN;
        case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: return ACT_SELECT;
        case SDLK_ESCAPE:            return ACT_BACK;
        }
    }
    return ACT_NONE;
}
void input_close(input_t *in) { free(in); }   /* free(NULL) is a no-op */
#endif
