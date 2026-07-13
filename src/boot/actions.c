/* Real boot handoff: EFI BootNext for firmware-matched entries (Windows and
 * any other bootnext target), OsIndications boot-to-firmware-UI for the
 * "UEFI Firmware Settings" entry.
 *
 * Every success path here calls reboot(RB_AUTOBOOT) itself; every failure
 * path returns -1 so src/init/main.c's own sync()+reboot(RB_AUTOBOOT) at the
 * bottom of main() still fires. There is no path back into the menu.
 */
#include "core/assets.h"
#include "boot/actions.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/reboot.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define EFIVARS "/sys/firmware/efi/efivars"
#define EFI_GLOBAL "8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define ATTRS 0x00000007u          /* NV | BS | RT */

#ifndef LINUX_REBOOT_CMD_KEXEC
#define LINUX_REBOOT_CMD_KEXEC 0x45584543
#endif

/* ---- UCS-2 / EFI load-option helpers (unit-tested, see tests/test_actions.c) ---- */

int ucs2_equals_ascii(const unsigned char *u, size_t maxb, const char *a) {
    size_t i = 0;
    for (; a[i]; i++) {
        if (2 * i + 1 >= maxb) return 0;
        if (u[2 * i] != (unsigned char)a[i] || u[2 * i + 1] != 0) return 0;
    }
    return 2 * i + 1 < maxb && u[2 * i] == 0 && u[2 * i + 1] == 0;
}

/* An efivarfs file's contents are: 4 bytes of efivarfs-exposed attributes,
 * then the raw EFI_LOAD_OPTION structure: u32 Attributes, u16
 * FilePathListLength, then the CHAR16 Description[] (NUL-terminated), then
 * the device-path list and optional data (which we don't need). That's a
 * fixed 10-byte header before the description starts.
 *
 * This parses efivarfs contents directly (the fuzz surface is whatever is
 * sitting in NVRAM), so every step is bounds-checked against `n` before any
 * byte is read -- a short buffer, a description with no UCS-2 NUL
 * terminator before the buffer ends, or an output buffer too small must all
 * fail cleanly (-1) rather than read or write out of bounds. */
int efi_load_option_description(const unsigned char *v, size_t n,
                                 char *out, size_t cap) {
    const size_t hdr = 4 + 4 + 2;   /* efivarfs attrs + LoadOption Attributes + FilePathListLength */
    if (n < hdr || cap == 0) return -1;
    const unsigned char *d = v + hdr;
    size_t left = n - hdr;
    size_t i = 0;
    for (;;) {
        size_t b = 2 * i;
        if (b + 1 >= left) return -1;      /* no NUL before the buffer ends */
        unsigned char lo = d[b], hi = d[b + 1];
        if (lo == 0 && hi == 0) break;
        if (hi != 0) return -1;  /* non-ASCII UCS-2: cannot match our ASCII names */
        if (i + 1 >= cap) return -1;       /* description would overflow out[] */
        out[i] = (char)lo;
        i++;
    }
    out[i] = 0;
    return 0;
}

/* Validates a Boot#### efivarfs filename against the EFI_GLOBAL_VARIABLE
 * GUID and, on match, extracts the 4-hex-digit boot number. Split out of
 * find_bootnum_by_desc() so the filename check is independently testable --
 * sscanf("Boot%4x-" EFI_GLOBAL) is not safe here because sscanf's %x
 * conversion can succeed and return 1 even when the trailing GUID literal
 * fails to match, silently accepting a wrong-GUID name. */
int efi_bootnum_from_name(const char *name, unsigned *out) {
    if (strncmp(name, "Boot", 4) != 0) return 0;
    unsigned num = 0;
    for (int i = 0; i < 4; i++) {
        char c = name[4 + i];
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else return 0;
        num = (num << 4) | (unsigned)v;
    }
    if (strcmp(name + 8, "-" EFI_GLOBAL) != 0) return 0;
    *out = num;
    return 1;
}

/* Builds the 6-byte efivarfs content for BootNext: 4 bytes of attributes
 * (ATTRS, LE) followed by the u16 boot number (LE). Pure/testable -- no I/O. */
void build_bootnext_value(unsigned num, unsigned char out[6]) {
    out[0] = (unsigned char)(ATTRS & 0xff);
    out[1] = (unsigned char)(ATTRS >> 8 & 0xff);
    out[2] = (unsigned char)(ATTRS >> 16 & 0xff);
    out[3] = (unsigned char)(ATTRS >> 24 & 0xff);
    out[4] = (unsigned char)(num & 0xff);
    out[5] = (unsigned char)(num >> 8 & 0xff);
}

/* Builds the 12-byte efivarfs content for OsIndications: 4 bytes of
 * attributes (ATTRS, LE) followed by the u64 OsIndications value (LE), with
 * EFI_OS_INDICATIONS_BOOT_TO_FW_UI (bit 0) forced on. Read-modify-write: if
 * `existing` holds a prior efivarfs value of at least 12 bytes, its u64
 * portion (bytes 4..11) is preserved and only bit 0x01 of the low byte is
 * OR'd in -- other firmware/OS-set indicator bits must survive untouched.
 * `existing == NULL` (or too short) is treated as a fresh zero value. Pure/
 * testable -- no I/O. */
int build_osindications_value(const unsigned char *existing, size_t n,
                               unsigned char out[12]) {
    memset(out, 0, 12);
    if (existing && n >= 12) memcpy(out + 4, existing + 4, 8);
    out[0] = (unsigned char)(ATTRS & 0xff);
    out[1] = (unsigned char)(ATTRS >> 8 & 0xff);
    out[2] = (unsigned char)(ATTRS >> 16 & 0xff);
    out[3] = (unsigned char)(ATTRS >> 24 & 0xff);
    out[4] |= 0x01;   /* EFI_OS_INDICATIONS_BOOT_TO_FW_UI, low bit of the u64 at offset 4 */
    return 0;
}

/* ---- efivarfs plumbing ---- */

/* efivarfs marks NVRAM variables immutable (chattr +i) by default; clear it
 * before (re)writing one. Best-effort: if the path doesn't exist yet (we're
 * about to create it) or the ioctl isn't supported, just proceed. */
static void rw_immutable_off(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    int fl = 0;
    if (ioctl(fd, FS_IOC_GETFLAGS, &fl) == 0 && (fl & FS_IMMUTABLE_FL)) {
        fl &= ~FS_IMMUTABLE_FL;
        ioctl(fd, FS_IOC_SETFLAGS, &fl);
    }
    close(fd);
}

/* Scan the Boot#### load options for one whose UCS-2-decoded description
 * equals `want` exactly. Used by E_BOOTNEXT entries (config-supplied "match"
 * string, e.g. "Windows Boot Manager"). */
static int find_bootnum_by_desc(const char *want, unsigned *out) {
    DIR *d = opendir(EFIVARS);
    if (!d) return -1;
    struct dirent *e;
    int found = -1;
    while ((e = readdir(d))) {
        unsigned num;
        if (!efi_bootnum_from_name(e->d_name, &num)) continue;
        char p[512];
        snprintf(p, sizeof p, EFIVARS "/%s", e->d_name);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        unsigned char buf[4096];               /* long device-path lists */
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        char desc[128];
        if (efi_load_option_description(buf, n, desc, sizeof desc) == 0 &&
            strcmp(desc, want) == 0) {
            *out = num;
            found = 0;
            break;
        }
    }
    closedir(d);
    return found;
}

static int set_bootnext(unsigned num) {
    char p[256];
    snprintf(p, sizeof p, EFIVARS "/BootNext-" EFI_GLOBAL);
    rw_immutable_off(p);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) { perror("[craftboot] BootNext open"); return -1; }
    unsigned char v[6];
    build_bootnext_value(num, v);
    int ok = write(fd, v, sizeof v) == (ssize_t)sizeof v;
    if (!ok) perror("[craftboot] BootNext write");
    close(fd);
    return ok ? 0 : -1;
}

static int set_boot_to_fw(void) {
    char p[256];
    snprintf(p, sizeof p, EFIVARS "/OsIndications-" EFI_GLOBAL);
    unsigned char existing[12] = {0};
    size_t n = 0;
    FILE *f = fopen(p, "rb");
    if (f) {
        n = fread(existing, 1, sizeof existing, f);
        fclose(f);
    }
    unsigned char v[12];
    build_osindications_value(n >= sizeof existing ? existing : NULL, n, v);
    rw_immutable_off(p);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) { perror("[craftboot] OsIndications open"); return -1; }
    int ok = write(fd, v, sizeof v) == (ssize_t)sizeof v;
    if (!ok) perror("[craftboot] OsIndications write");
    close(fd);
    return ok ? 0 : -1;
}

/* Shared handoff for all E_BOOTNEXT entries: find the load option whose
 * UCS-2 description equals `want`, stage its number in BootNext, and reboot
 * into the firmware's own loader. `name` is the short name used in
 * diagnostics (the match string, e.g. "Windows Boot Manager"). Never returns
 * on live success. */
static int do_bootnext(const entry_t *e, const char *want, const char *name,
                       const char *mode, int live) {
    unsigned num = 0;
    int found = find_bootnum_by_desc(want, &num) == 0;
    fprintf(stderr, "[craftboot] %s: %s (id=%s)\n", mode, e->label, e->id);
    if (found) fprintf(stderr, "           match \"%s\" -> BootNext Boot%04X\n", want, num);
    else fprintf(stderr, "           no %s entry\n", name);
    if (!live) return 0;
    if (!found) { fprintf(stderr, "[craftboot] no %s entry\n", name); return -1; }
    if (set_bootnext(num)) return -1;
    sync();
    reboot(RB_AUTOBOOT);
    perror("[craftboot] reboot(RB_AUTOBOOT)");   /* only reached on failure */
    return -1;
}

int action_execute(const entry_t *e, const char *root, int live) {
    (void)root;
    const char *mode = live ? "LIVE handoff" : "dry-run handoff";
    switch (e->type) {
    case E_BOOTNEXT:
        if (!e->match[0]) {
            fprintf(stderr, "[craftboot] %s: %s (id=%s)\n", mode, e->label, e->id);
            fprintf(stderr, "[craftboot] bootnext entry '%s' has no match string\n", e->id);
            return -1;
        }
        return do_bootnext(e, e->match, e->match, mode, live);
    case E_UEFI:
        fprintf(stderr, "[craftboot] %s: %s (id=%s)\n", mode, e->label, e->id);
        fprintf(stderr, "           set OsIndications BOOT_TO_FW_UI, reboot\n");
        if (!live) return 0;
        if (set_boot_to_fw()) return -1;
        sync();
        reboot(RB_AUTOBOOT);
        perror("[craftboot] reboot(RB_AUTOBOOT)");   /* only reached on failure */
        return -1;
    default:
        fprintf(stderr, "[craftboot] %s: no handoff implemented for '%s' yet\n", mode, e->id);
        return 0;
    }
}
