#include "t.h"
#include "boot/actions.h"
#include <unistd.h>

static int ucs2_match(void) {
    unsigned char w[] = { 'W',0,'i',0,'n',0,0,0 };
    OK(ucs2_equals_ascii(w, sizeof w, "Win") == 1);
    OK(ucs2_equals_ascii(w, sizeof w, "Wia") == 0);
    OK(ucs2_equals_ascii(w, sizeof w, "Windows") == 0);
    return 0;
}

static int parses_load_option(void) {
    /* efivar layout: 4B efivarfs attrs | u32 LoadOption attrs | u16 pathlen | desc UCS-2 NUL */
    unsigned char v[4 + 4 + 2 + 8] = {0};
    v[4+4+2+0]='W'; v[4+4+2+2]='B'; v[4+4+2+4]='M';   /* "WBM" in UCS-2 */
    char out[16];
    OK(efi_load_option_description(v, sizeof v, out, sizeof out) == 0);
    OK(strcmp(out, "WBM") == 0);
    return 0;
}

static int load_option_rejects_short_buffer(void) {
    unsigned char v[9] = {0};   /* one byte short of the 10-byte header */
    char out[16];
    OK(efi_load_option_description(v, sizeof v, out, sizeof out) == -1);
    return 0;
}

static int load_option_rejects_unterminated_description(void) {
    /* description runs to the end of the buffer with no UCS-2 NUL terminator */
    unsigned char v[4 + 4 + 2 + 4] = {0};
    v[10] = 'A'; v[11] = 0; v[12] = 'B'; v[13] = 0;   /* "AB", no trailing 00 00 */
    char out[16];
    OK(efi_load_option_description(v, sizeof v, out, sizeof out) == -1);
    return 0;
}

static int load_option_rejects_too_small_output(void) {
    unsigned char v[4 + 4 + 2 + 8] = {0};
    v[4+4+2+0]='W'; v[4+4+2+2]='B'; v[4+4+2+4]='M';
    char out[2];   /* not enough room for "WBM\0" */
    OK(efi_load_option_description(v, sizeof v, out, sizeof out) == -1);
    return 0;
}

static int load_option_rejects_non_ascii(void) {
    /* description contains a non-ASCII UCS-2 unit (0x0401); decoding just the
     * low byte would silently corrupt the name -- must fail closed */
    unsigned char v[4 + 4 + 2 + 6] = {0};
    v[10] = 'W'; v[11] = 0;
    v[12] = 0x01; v[13] = 0x04;   /* U+0401, hi byte nonzero */
    char out[16];
    OK(efi_load_option_description(v, sizeof v, out, sizeof out) == -1);
    return 0;
}

static int bootnum_from_name(void) {
    unsigned num = 0;
    OK(efi_bootnum_from_name(
        "Boot0001-8be4df61-93ca-11d2-aa0d-00e098032b8c", &num) == 1);
    OK(num == 1);
    OK(efi_bootnum_from_name(
        "BootOrder-8be4df61-93ca-11d2-aa0d-00e098032b8c", &num) == 0);
    OK(efi_bootnum_from_name(
        "Boot001-8be4df61-93ca-11d2-aa0d-00e098032b8c", &num) == 0);
    OK(efi_bootnum_from_name(
        "Boot0001-00000000-0000-0000-0000-000000000000", &num) == 0);
    return 0;
}

static int bootnext_value_bytes(void) {
    unsigned char out[6];
    build_bootnext_value(0x0001, out);
    unsigned char want1[6] = {0x07,0x00,0x00,0x00,0x01,0x00};
    OK(memcmp(out, want1, 6) == 0);

    build_bootnext_value(0x1234, out);
    unsigned char want2[6] = {0x07,0x00,0x00,0x00,0x34,0x12};
    OK(memcmp(out, want2, 6) == 0);
    return 0;
}

static int osindications_fresh_value(void) {
    unsigned char out[12];
    OK(build_osindications_value(NULL, 0, out) == 0);
    unsigned char want[12] = {0x07,0x00,0x00,0x00,0x01,0,0,0,0,0,0,0};
    OK(memcmp(out, want, 12) == 0);

    /* too-short existing buffer is also treated as fresh/zero */
    unsigned char short_existing[4] = {0xFF,0xFF,0xFF,0xFF};
    OK(build_osindications_value(short_existing, sizeof short_existing, out) == 0);
    OK(memcmp(out, want, 12) == 0);
    return 0;
}

static int osindications_preserves_existing_bits(void) {
    /* Existing efivarfs contents: attrs (bytes 0..3, ignored/overwritten)
     * and a u64 value (bytes 4..11) that already has bit 0x10 set plus some
     * high bytes -- those must survive, with only bit 0x01 OR'd in and the
     * attrs forced to ATTRS (07 00 00 00). */
    unsigned char existing[12] = {
        0x00,0x00,0x00,0x00,        /* stale/garbage attrs -- must be overwritten */
        0x10, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x01
    };
    unsigned char out[12];
    OK(build_osindications_value(existing, sizeof existing, out) == 0);
    unsigned char want[12] = {
        0x07,0x00,0x00,0x00,
        0x11, 0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x01
    };
    OK(memcmp(out, want, 12) == 0);
    return 0;
}

/* Run action_execute() in DRY-RUN (live=0) with stderr redirected to a
 * tmpfile, returning the captured diagnostics in `log` and the rc via *rc.
 * Dry-run performs no syscalls/reboots and never enters the menu again, so
 * this exercises the real dispatch + path-assembly logic hardware-free. */
static int run_dry(const entry_t *e, const char *root, int *rc,
                   char *log, size_t logcap) {
    FILE *tmp = tmpfile();
    if (!tmp) return -1;
    fflush(stderr);
    int saved = dup(fileno(stderr));
    if (saved < 0 || dup2(fileno(tmp), fileno(stderr)) < 0) { fclose(tmp); return -1; }
    *rc = action_execute(e, root, 0);
    fflush(stderr);
    dup2(saved, fileno(stderr));
    close(saved);
    rewind(tmp);
    size_t n = fread(log, 1, logcap - 1, tmp);
    log[n] = 0;
    fclose(tmp);
    return 0;
}

static int dry_run_dispatch_return_codes(void) {
    entry_t e;
    char log[2048]; int rc;

    /* E_UEFI dry-run: prints plan, returns 0 (no OsIndications write). */
    memset(&e, 0, sizeof e); e.type = E_UEFI;
    OK(run_dry(&e, "", &rc, log, sizeof log) == 0); OK(rc == 0);

    /* E_INFO (and other non-handoff types) return 0. */
    memset(&e, 0, sizeof e); e.type = E_INFO;
    OK(run_dry(&e, "", &rc, log, sizeof log) == 0); OK(rc == 0);

    /* E_WINDOWS dry-run returns 0 regardless of whether the firmware has a
     * "Windows Boot Manager" load option (the match only gates a live boot). */
    memset(&e, 0, sizeof e); e.type = E_WINDOWS; strcpy(e.label, "Windows");
    OK(run_dry(&e, "", &rc, log, sizeof log) == 0); OK(rc == 0);

    /* E_BOOTNEXT with a match string: dry-run returns 0. */
    memset(&e, 0, sizeof e); e.type = E_BOOTNEXT;
    strcpy(e.label, "Ubuntu"); strcpy(e.match, "NoSuchLoadOption_craftboot_test");
    OK(run_dry(&e, "", &rc, log, sizeof log) == 0); OK(rc == 0);

    /* E_BOOTNEXT with NO match string is a config error: -1 even in dry-run. */
    memset(&e, 0, sizeof e); e.type = E_BOOTNEXT; strcpy(e.id, "broken");
    OK(run_dry(&e, "", &rc, log, sizeof log) == 0); OK(rc == -1);
    OK(strstr(log, "no match string") != NULL);
    return 0;
}

static int kexec_root_prefixes_paths(void) {
    /* E_KEXEC dry-run must join root_prefix + kernel/initrd exactly ("%s%s")
     * and echo the assembled paths -- the root prefix is what makes the
     * on-disk /boot/... paths resolve once the real root is mounted at /mnt. */
    entry_t e; memset(&e, 0, sizeof e);
    e.type = E_KEXEC;
    strcpy(e.label, "Recovery"); strcpy(e.id, "rec");
    strcpy(e.kernel, "/boot/vmlinuz");
    strcpy(e.initrd, "/boot/initrd.img");
    strcpy(e.cmdline, "ro quiet");
    char log[2048]; int rc;
    OK(run_dry(&e, "/mnt", &rc, log, sizeof log) == 0);
    OK(rc == 0);
    OK(strstr(log, "kernel=/mnt/boot/vmlinuz") != NULL);
    OK(strstr(log, "initrd=/mnt/boot/initrd.img") != NULL);
    OK(strstr(log, "cmdline=ro quiet") != NULL);
    /* empty root prefix leaves the path unchanged */
    OK(run_dry(&e, "", &rc, log, sizeof log) == 0);
    OK(strstr(log, "kernel=/boot/vmlinuz") != NULL);
    return 0;
}

int main(void) {
    RUN(ucs2_match);
    RUN(parses_load_option);
    RUN(load_option_rejects_short_buffer);
    RUN(load_option_rejects_unterminated_description);
    RUN(load_option_rejects_too_small_output);
    RUN(load_option_rejects_non_ascii);
    RUN(bootnum_from_name);
    RUN(bootnext_value_bytes);
    RUN(osindications_fresh_value);
    RUN(osindications_preserves_existing_bits);
    RUN(dry_run_dispatch_return_codes);
    RUN(kexec_root_prefixes_paths);
    return 0;
}
