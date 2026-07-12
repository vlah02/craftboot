#include "core/assets.h"
#include "core/menu.h"
#include "platform/display.h"
#include "platform/input.h"
#include "boot/actions.h"
#include "init/init.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static void msleep(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, NULL); }

/* On a DEBUG image (busybox staged at /bin/sh) drop to a shell instead of
 * rebooting on failure, so a developer can poke around post-mortem. On a
 * release image /bin/sh doesn't exist, so this is a no-op and we fall
 * through to the reboot. */
static void fail_stop(void) {
    if (access("/bin/sh", X_OK) == 0)
        execl("/bin/sh", "sh", NULL);
    reboot(RB_AUTOBOOT);
}

static void load_modules(void) {
    FILE *f = fopen("/modules.list", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        int fd = open(line, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        if (syscall(SYS_finit_module, fd, "", 0) == 0)
            fprintf(stderr, "[craftboot] module: %s\n", line);
        close(fd);
    }
    fclose(f);
}
static int mount_root(const char *uuid_str) {
    unsigned char want[16];
    if (uuid_parse16(uuid_str, want)) return -1;
    for (int attempt = 0; attempt < 40; attempt++) {   /* up to ~8 s */
        DIR *d = opendir("/sys/class/block");
        struct dirent *e;
        if (d) {
            while ((e = readdir(d))) {
                if (e->d_name[0] == '.') continue;
                char dev[sizeof e->d_name + 5]; snprintf(dev, sizeof dev, "/dev/%s", e->d_name);
                if (!ext4_uuid_matches(dev, want)) continue;
                closedir(d);
                if (mount(dev, "/mnt", "ext4", MS_RDONLY, NULL) == 0) {
                    fprintf(stderr, "[craftboot] root: %s ro on /mnt\n", dev);
                    return 0;
                }
                return -1;
            }
            closedir(d);
        }
        msleep(200);
    }
    return -1;
}
static void be_init(void) {
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mkdir("/mnt", 0755);                            /* mount_root's target; build.sh stages it, but don't depend on that */
    /* The kernel's compiled-in default for userspace-to-/dev/kmsg writes is
     * "ratelimit" (burst of ~10 then silence for several seconds) since no
     * normal-boot systemd ever runs in this initramfs to relax it via
     * sysctl.d. Without this, our own diagnostics right after the
     * module-load burst (root:, drm:, evdev:, panorama world:) get
     * silently dropped. The write needs a trailing newline or the kernel's
     * proc_dostring rejects it with EINVAL. */
    int pkfd = open("/proc/sys/kernel/printk_devkmsg", O_WRONLY | O_CLOEXEC);
    if (pkfd >= 0) { ssize_t n = write(pkfd, "on\n", 3); (void)n; close(pkfd); }
    mkdir("/sys/firmware/efi/efivars", 0755);
    mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, NULL);
    int k = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (k >= 0) { dup2(k, 1); dup2(k, 2); if (k > 2) close(k); }
    load_modules();
    msleep(2000);                                  /* USB keyboard enumeration */
}

int main(int argc, char **argv) {
    int is_init = getpid() == 1;
    int live = is_init;                             /* PID-1 boots for real */
    const char *assets = is_init ? "/assets" : "assets";
    const char *cfgpath = is_init ? "/boot_entries.json" : "boot_entries.json";
    const char *root = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--live")) live = 1;
        else if (!strcmp(argv[i], "--dry")) live = 0;
        else if (!strcmp(argv[i], "--assets") && i + 1 < argc) assets = argv[++i];
    }
    config_t cfg;
    if (is_init) be_init();
    if (config_load(&cfg, cfgpath)) {
        fprintf(stderr, "[craftboot] config load failed\n");
        if (is_init) { msleep(15000); fail_stop(); }
        return 1;
    }
    if (is_init && mount_root(cfg.root_uuid) == 0) root = "/mnt";
    else if (is_init) fprintf(stderr, "[craftboot] no root found (kexec unavailable)\n");

    display_t *d = display_open(1920, 1080);
    if (!d) {
        fprintf(stderr, "[craftboot] display open failed\n");
        if (is_init) { msleep(15000); fail_stop(); }
        return 1;
    }
    input_t *in = input_open(d);
    const entry_t *e = menu_run(d, in, &cfg, assets);
    int rc = e ? action_execute(e, root, live) : 0;
    if (rc) menu_show_error(d, "handoff failed", 15);
    input_close(in);
    display_close(d);
    if (is_init) { sync(); msleep(2000); reboot(RB_AUTOBOOT); }
    return rc;
}
