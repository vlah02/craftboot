#include "t.h"
#include "boot/actions.h"

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

int main(void) {
    RUN(ucs2_match);
    RUN(parses_load_option);
    RUN(load_option_rejects_short_buffer);
    RUN(load_option_rejects_unterminated_description);
    RUN(load_option_rejects_too_small_output);
    RUN(load_option_rejects_non_ascii);
    RUN(bootnum_from_name);
    return 0;
}
