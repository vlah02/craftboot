#include "t.h"
#include "core/efivar.h"

static int ucs2_match(void) {
    unsigned char w[] = { 'W',0,'i',0,'n',0,0,0 };
    OK(ucs2_equals_ascii(w, sizeof w, "Win") == 1);
    OK(ucs2_equals_ascii(w, sizeof w, "Wia") == 0);
    OK(ucs2_equals_ascii(w, sizeof w, "Windows") == 0);
    return 0;
}

static int load_option_desc_parses(void) {
    /* raw firmware layout: u32 Attributes | u16 FilePathListLength | desc UCS-2 NUL */
    unsigned char lo[4 + 2 + 8] = {0};
    lo[4 + 2 + 0] = 'W';
    lo[4 + 2 + 2] = 'i';
    lo[4 + 2 + 4] = 'n';
    char out[16];
    OK(efi_load_option_desc(lo, sizeof lo, out, sizeof out) == 0);
    OK(strcmp(out, "Win") == 0);
    return 0;
}

static int load_option_desc_rejects(void) {
    char out[16];

    /* n < 6: buffer shorter than the fixed header */
    unsigned char too_short[5] = {0};
    OK(efi_load_option_desc(too_short, sizeof too_short, out, sizeof out) == -1);

    /* unterminated description: runs to the end of the buffer with no
     * UCS-2 NUL terminator */
    unsigned char unterminated[4 + 2 + 4] = {0};
    unterminated[6] = 'A'; unterminated[7] = 0;
    unterminated[8] = 'B'; unterminated[9] = 0;
    OK(efi_load_option_desc(unterminated, sizeof unterminated, out, sizeof out) == -1);

    /* non-ASCII UCS-2 unit (high byte nonzero) must fail closed */
    unsigned char non_ascii[4 + 2 + 4] = {0};
    non_ascii[6] = 'W'; non_ascii[7] = 0;
    non_ascii[8] = 0x01; non_ascii[9] = 0x04;   /* U+0401 */
    OK(efi_load_option_desc(non_ascii, sizeof non_ascii, out, sizeof out) == -1);

    /* output buffer too small for "Win\0" */
    unsigned char lo[4 + 2 + 8] = {0};
    lo[4 + 2 + 0] = 'W'; lo[4 + 2 + 2] = 'i'; lo[4 + 2 + 4] = 'n';
    char tiny[2];
    OK(efi_load_option_desc(lo, sizeof lo, tiny, sizeof tiny) == -1);
    return 0;
}

static int osindications_fresh(void) {
    unsigned char out[8];
    osindications_rmw(NULL, 0, out);
    unsigned char want[8] = {0x01,0,0,0,0,0,0,0};
    OK(memcmp(out, want, 8) == 0);
    return 0;
}

static int osindications_preserves(void) {
    unsigned char cur[8] = {0x02,0,0,0,0,0,0,0x80};
    unsigned char out[8];
    osindications_rmw(cur, sizeof cur, out);
    unsigned char want[8] = {0x03,0,0,0,0,0,0,0x80};
    OK(memcmp(out, want, 8) == 0);
    return 0;
}

static int boot_num_parses(void) {
    uint16_t n1[] = {'B','o','o','t','0','0','0','3',0};
    unsigned out = 0;
    OK(boot_num_from_ucs2_name(n1, &out) == 1);
    OK(out == 3);

    uint16_t n2[] = {'B','o','o','t','1','A','2','b',0};
    OK(boot_num_from_ucs2_name(n2, &out) == 1);
    OK(out == 0x1A2B);
    return 0;
}

static int boot_num_rejects(void) {
    unsigned out = 0;
    uint16_t order[] = {'B','o','o','t','O','r','d','e','r',0};
    OK(boot_num_from_ucs2_name(order, &out) == 0);

    uint16_t bad_hex[] = {'B','o','o','t','0','0','0','G',0};
    OK(boot_num_from_ucs2_name(bad_hex, &out) == 0);

    uint16_t short_num[] = {'B','o','o','t','0','0','3',0};   /* only 3 hex digits */
    OK(boot_num_from_ucs2_name(short_num, &out) == 0);
    return 0;
}

int main(void) {
    RUN(ucs2_match);
    RUN(load_option_desc_parses);
    RUN(load_option_desc_rejects);
    RUN(osindications_fresh);
    RUN(osindications_preserves);
    RUN(boot_num_parses);
    RUN(boot_num_rejects);
    return 0;
}
