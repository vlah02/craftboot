#include "t.h"
#include "core/menu.h"
static config_t cfg;
static void setup(void) {
    if (config_load(&cfg, "boot_entries.json") != 0) {
        fprintf(stderr, "config_load(\"boot_entries.json\") failed\n");
        exit(1);
    }
}
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
    int n; ms_entries(&m, &n); OK(n == 2);
    m.index = 1;                                  /* Back */
    OK(ms_select(&m) == NULL);
    OK(m.level == 0);
    OK(ms_back(&m) == 0);                         /* at root -> quit signal */
    return 0;
}
static int select_returns_bootable(void) {
    menustate_t m; ms_init(&m, &cfg);
    m.index = 1;                                  /* Ubuntu (chainload since v3) */
    const entry_t *e = ms_select(&m);
    OK(e && e->type == E_CHAINLOAD);
    OK(strcmp(e->path, "\\EFI\\ubuntudirect\\shimx64.efi") == 0);
    return 0;
}
static int chainload_is_default_bootable(void) {
    menustate_t m; ms_init(&m, &cfg);
    m.index = 1;                                  /* highlight Ubuntu */
    const entry_t *d = ms_default_entry(&m);
    OK(d && d->type == E_CHAINLOAD);              /* countdown boots the highlight */
    return 0;
}
static int countdown_and_default(void) {
    menustate_t m; ms_init(&m, &cfg);
    OK(m.countdown > 14.9 && m.countdown <= 15.0);
    ms_tick(&m, 6.0); OK(m.countdown > 8.9);
    ms_cancel_autoboot(&m); OK(m.countdown < 0);
    const entry_t *d = ms_default_entry(&m);
    OK(d && d->type == E_BOOTNEXT);               /* index 0 (Windows) is bootable */
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
static int default_entry_skips_nonbootable(void) {
    /* highlight the Extras submenu (index 2, non-bootable): auto-boot must
     * fall through to the first bootable entry, not return the submenu. */
    menustate_t m; ms_init(&m, &cfg);
    m.index = 2;                                  /* Extras (E_SUBMENU) */
    const entry_t *d = ms_default_entry(&m);
    OK(d && d->type == E_BOOTNEXT);                /* first bootable = index 0 */
    OK(d == &cfg.menu[0][0]);
    return 0;
}
static int default_entry_none_bootable(void) {
    /* a menu with no bootable entry at all must yield NULL, not a stray
     * pointer -- menu_run would otherwise auto-boot garbage on expiry. */
    config_t c = {0};
    c.nmenu[0] = 2;
    c.menu[0][0] = (entry_t){ .type = E_INFO };
    c.menu[0][1] = (entry_t){ .type = E_SUBMENU };
    menustate_t m; ms_init(&m, &c);
    m.index = 0;
    OK(ms_default_entry(&m) == NULL);
    return 0;
}
static int empty_menu_safe(void) {
    config_t empty = {0};
    strcpy(empty.root_uuid, "c36a4c56-487b-4aee-946b-f7fa2dc7f001");
    empty.nmenu[0] = 1;
    empty.menu[0][0] = (entry_t){ .type = E_SUBMENU };
    strcpy(empty.menu[0][0].label, "Extras");
    strcpy(empty.menu[0][0].target, "extras");   /* extras: nmenu[1]==0 */
    menustate_t m; ms_init(&m, &empty);
    OK(ms_select(&m) == NULL);        /* enters empty submenu */
    ms_move(&m, 1);                   /* must not SIGFPE */
    OK(ms_select(&m) == NULL);        /* must not index OOB */
    OK(ms_back(&m) == 1);             /* can still leave */
    return 0;
}
int main(void) { setup(); RUN(nav_wraps); RUN(submenu_and_back);
                 RUN(select_returns_bootable); RUN(chainload_is_default_bootable);
                 RUN(countdown_and_default);
                 RUN(move_large_delta_safe); RUN(countdown_expiry_clamps_to_zero);
                 RUN(default_entry_skips_nonbootable); RUN(default_entry_none_bootable);
                 RUN(empty_menu_safe); return 0; }
