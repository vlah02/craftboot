#include "t.h"
#include "core/assets.h"
static int loads_real_config(void) {
    config_t c;
    OK(config_load(&c, "boot_entries.json") == 0);
    OK(strlen(c.root_uuid) == 36);
    OK(c.nmenu[0] == 3);
    OK(c.menu[0][0].type == E_WINDOWS);
    OK(strcmp(c.menu[0][0].label, "Windows") == 0);
    OK(c.menu[0][1].type == E_BOOTNEXT);
    OK(strcmp(c.menu[0][1].match, "Ubuntu") == 0);
    OK(c.menu[0][2].type == E_SUBMENU);
    OK(c.nmenu[1] == 4);
    OK(c.menu[1][0].type == E_KEXEC);           /* recovery keeps kexec */
    OK(strcmp(c.menu[1][0].kernel, "/boot/vmlinuz") == 0);
    OK(c.menu[1][3].type == E_BACK);
    return 0;
}
static int missing_file_fails(void) {
    config_t c; OK(config_load(&c, "/nonexistent.json") != 0); return 0;
}
int main(void) { RUN(loads_real_config); RUN(missing_file_fails); return 0; }
