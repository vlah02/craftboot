#include "t.h"
#include "core/menu.h"
static config_t cfg;
static void setup(void) { config_load(&cfg, "boot_entries.json"); }
static int nav_wraps(void) {
    menustate_t m; ms_init(&m, &cfg);
    OK(m.index == 0);
    ms_move(&m, -1); OK(m.index == 2);
    ms_move(&m, 1);  OK(m.index == 0);
    return 0;
}
static int submenu_and_back(void) {
    menustate_t m; ms_init(&m, &cfg);
    m.index = 2;                                  /* Extras */
    OK(ms_select(&m) == NULL);
    OK(m.level == 1 && m.index == 0);
    int n; ms_entries(&m, &n); OK(n == 4);
    m.index = 3;                                  /* Back */
    OK(ms_select(&m) == NULL);
    OK(m.level == 0);
    OK(ms_back(&m) == 0);                         /* at root -> quit signal */
    return 0;
}
static int select_returns_bootable(void) {
    menustate_t m; ms_init(&m, &cfg);
    m.index = 1;                                  /* Ubuntu */
    const entry_t *e = ms_select(&m);
    OK(e && e->type == E_KEXEC);
    return 0;
}
static int countdown_and_default(void) {
    menustate_t m; ms_init(&m, &cfg);
    OK(m.countdown > 14.9 && m.countdown <= 15.0);
    ms_tick(&m, 6.0); OK(m.countdown > 8.9);
    ms_cancel_autoboot(&m); OK(m.countdown < 0);
    const entry_t *d = ms_default_entry(&m);
    OK(d && (d->type == E_WINDOWS || d->type == E_KEXEC));
    OK(d->type == E_WINDOWS);                     /* index 0 is bootable */
    return 0;
}
static int move_large_delta_safe(void) {
    menustate_t m; ms_init(&m, &cfg);
    ms_move(&m, -5);                 /* n=3: ((0-5)%3+3)%3 = 1 */
    OK(m.index == 1);
    ms_move(&m, 300);                /* 300 % 3 = 0 -> stays 1 */
    OK(m.index == 1);
    ms_move(&m, -300);
    OK(m.index == 1);
    return 0;
}
static int countdown_expiry_clamps_to_zero(void) {
    menustate_t m; ms_init(&m, &cfg);
    ms_tick(&m, 20.0);               /* overshoot expiry */
    OK(m.countdown == 0.0);          /* clamped, not negative */
    ms_tick(&m, 1.0);
    OK(m.countdown == 0.0);          /* stays expired */
    ms_cancel_autoboot(&m);
    OK(m.countdown < 0);             /* cancelled distinct from expired */
    ms_tick(&m, 1.0);
    OK(m.countdown < 0);             /* cancel is sticky */
    return 0;
}
int main(void) { setup(); RUN(nav_wraps); RUN(submenu_and_back);
                 RUN(select_returns_bootable); RUN(countdown_and_default);
                 RUN(move_large_delta_safe); RUN(countdown_expiry_clamps_to_zero); return 0; }
