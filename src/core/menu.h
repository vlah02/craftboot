#pragma once
#include "core/assets.h"
#include "platform/display.h"
#include "platform/input.h"

typedef struct {
    const config_t *cfg;
    int level;               /* 0 = main, 1 = extras */
    int index;
    double countdown;        /* >0 running, ==0 expired (auto-boot), <0 cancelled */
} menustate_t;

void ms_init(menustate_t *m, const config_t *cfg);
void ms_move(menustate_t *m, int delta);
/* returns bootable entry when selection resolves to one, else NULL (submenu/back handled) */
const entry_t *ms_select(menustate_t *m);
int  ms_back(menustate_t *m);                 /* 1 = consumed, 0 = at root (quit) */
void ms_tick(menustate_t *m, double dt);      /* advances countdown */
void ms_cancel_autoboot(menustate_t *m);
const entry_t *ms_default_entry(const menustate_t *m);   /* auto-boot target */
const entry_t *ms_entries(const menustate_t *m, int *n);

const entry_t *menu_run(display_t *d, input_t *in, const config_t *cfg,
                        const char *assets_dir);   /* NULL = quit/esc; else boot it */
void menu_show_error(display_t *d, const char *msg, int seconds);
