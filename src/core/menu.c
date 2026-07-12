#include "core/menu.h"
#include <string.h>

void ms_init(menustate_t *m, const config_t *cfg) {
    memset(m, 0, sizeof *m);
    m->cfg = cfg;
    m->countdown = 15.0;
}
const entry_t *ms_entries(const menustate_t *m, int *n) {
    *n = m->cfg->nmenu[m->level];
    return m->cfg->menu[m->level];
}
void ms_move(menustate_t *m, int d) {
    int n; ms_entries(m, &n);
    m->index = (m->index + d + n) % n;
}
const entry_t *ms_select(menustate_t *m) {
    int n; const entry_t *e = &ms_entries(m, &n)[m->index];
    if (e->type == E_SUBMENU) { m->level = 1; m->index = 0; return NULL; }
    if (e->type == E_BACK)    { m->level = 0; m->index = 0; return NULL; }
    if (e->type == E_INFO)    return NULL;
    return e;
}
int ms_back(menustate_t *m) {
    if (m->level > 0) { m->level = 0; m->index = 0; return 1; }
    return 0;
}
void ms_tick(menustate_t *m, double dt) { if (m->countdown >= 0) m->countdown -= dt; }
void ms_cancel_autoboot(menustate_t *m) { m->countdown = -1; }
static int bootable(const entry_t *e) { return e->type == E_WINDOWS || e->type == E_KEXEC; }
const entry_t *ms_default_entry(const menustate_t *m) {
    int n; const entry_t *es = m->cfg->menu[0]; n = m->cfg->nmenu[0];
    if (m->level == 0 && bootable(&es[m->index])) return &es[m->index];
    for (int i = 0; i < n; i++) if (bootable(&es[i])) return &es[i];
    return NULL;
}
