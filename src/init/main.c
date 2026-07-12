#include "core/assets.h"
#include "core/menu.h"
#include "platform/display.h"
#include "platform/input.h"
#include "boot/actions.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int live = 0;
    const char *assets = "assets";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--live")) live = 1;
        else if (!strcmp(argv[i], "--assets") && i + 1 < argc) assets = argv[++i];
    }
    config_t cfg;
    char cfgpath[256];
    snprintf(cfgpath, sizeof cfgpath, "boot_entries.json");
    if (config_load(&cfg, cfgpath)) { fprintf(stderr, "config load failed\n"); return 1; }
    display_t *d = display_open(1920, 1080);
    if (!d) { fprintf(stderr, "display open failed\n"); return 1; }
    input_t *in = input_open(d);
    const entry_t *e = menu_run(d, in, &cfg, assets);
    int rc = 0;
    if (e) rc = action_execute(e, "", live);
    input_close(in);
    display_close(d);
    return rc;
}
