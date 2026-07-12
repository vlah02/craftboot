#pragma once
#include "platform/display.h"
typedef enum { ACT_NONE, ACT_UP, ACT_DOWN, ACT_SELECT, ACT_BACK, ACT_QUIT } action_t;
typedef struct input input_t;
input_t *input_open(display_t *d);          /* evdev ignores d */
action_t input_poll(input_t *in);           /* nonblocking; ACT_NONE when drained */
void input_close(input_t *in);
