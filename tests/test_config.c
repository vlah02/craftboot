#include "t.h"
#include "core/assets.h"
#include <stdio.h>
#include <unistd.h>
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
/* config_t holds entry_t menu[2][8]; config_load caps each menu at 8 entries
 * (the `a < 8` loop guard). A JSON menu with more than 8 entries must be
 * truncated to 8, never written past the array -- under ASan a missing cap
 * would surface as a global/heap overflow here. */
static int caps_entries_at_eight(void) {
    char path[] = "/tmp/craftboot_test_cfg_XXXXXX";
    int fd = mkstemp(path); OK(fd >= 0);
    FILE *f = fdopen(fd, "w"); OK(f != NULL);
    fputs("{\"root_uuid\":\"c36a4c56-487b-4aee-946b-f7fa2dc7f001\","
          "\"menus\":{\"main\":[", f);
    for (int i = 0; i < 12; i++)                 /* 12 > the cap of 8 */
        fprintf(f, "%s{\"id\":\"e%d\",\"label\":\"E%d\",\"type\":\"info\"}",
                i ? "," : "", i, i);
    fputs("]}}", f);
    fclose(f);
    config_t c;
    OK(config_load(&c, path) == 0);
    OK(c.nmenu[0] == 8);                          /* truncated, not overflowed */
    unlink(path);
    return 0;
}
int main(void) { RUN(loads_real_config); RUN(missing_file_fails);
                 RUN(caps_entries_at_eight); return 0; }
