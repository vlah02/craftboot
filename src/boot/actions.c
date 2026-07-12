#include "core/assets.h"
#include "boot/actions.h"
#include <stdio.h>
int action_execute(const entry_t *e, const char *root, int live) {
    (void)root;
    fprintf(stderr, "[craftboot] %s handoff: %s (%s)\n",
            live ? "LIVE" : "dry-run", e->label, e->id);
    return 0;
}
