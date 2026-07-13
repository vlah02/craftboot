/* Pure EFI-variable helpers -- see core/efivar.h. Dependency-free (no libc)
 * so this compiles unchanged in both the host build and the freestanding
 * mingw EFI build: every byte loop below is hand-written rather than
 * borrowed from string.h. */
#include "core/efivar.h"

int ucs2_equals_ascii(const unsigned char *u, size_t maxb, const char *a) {
    size_t i = 0;
    for (; a[i]; i++) {
        if (2 * i + 1 >= maxb) return 0;
        if (u[2 * i] != (unsigned char)a[i] || u[2 * i + 1] != 0) return 0;
    }
    return 2 * i + 1 < maxb && u[2 * i] == 0 && u[2 * i + 1] == 0;
}

/* Firmware GetVariable returns the raw EFI_LOAD_OPTION directly (no efivarfs
 * 4-byte attribute prefix): u32 Attributes, u16 FilePathListLength, then the
 * CHAR16 Description[] (NUL-terminated) -- a 6-byte header before the
 * description starts. Every step is bounds-checked against `n` before any
 * byte is read: a short buffer, a description with no UCS-2 NUL terminator
 * before the buffer ends, a non-ASCII UCS-2 unit, or an output buffer too
 * small must all fail cleanly (-1) rather than read/write out of bounds. */
int efi_load_option_desc(const unsigned char *lo, size_t n,
                          char *out, size_t cap) {
    const size_t hdr = 4 + 2;   /* LoadOption Attributes + FilePathListLength */
    if (n < hdr || cap == 0) return -1;
    const unsigned char *d = lo + hdr;
    size_t left = n - hdr;
    size_t i = 0;
    for (;;) {
        size_t b = 2 * i;
        if (b + 1 >= left) return -1;      /* no NUL before the buffer ends */
        unsigned char c0 = d[b], c1 = d[b + 1];
        if (c0 == 0 && c1 == 0) break;
        if (c1 != 0) return -1;  /* non-ASCII UCS-2: cannot match our ASCII names */
        if (i + 1 >= cap) return -1;       /* description would overflow out[] */
        out[i] = (char)c0;
        i++;
    }
    out[i] = 0;
    return 0;
}

/* Read-modify-write the raw 8-byte little-endian OsIndications u64 as
 * firmware GetVariable/SetVariable see it -- no efivarfs 4-byte attribute
 * prefix; SetVariable takes attributes as a separate argument. */
void osindications_rmw(const unsigned char *cur, size_t n, unsigned char out[8]) {
    for (size_t i = 0; i < 8; i++) out[i] = 0;
    if (cur && n >= 8) {
        for (size_t i = 0; i < 8; i++) out[i] = cur[i];
    }
    out[0] |= 0x01;   /* EFI_OS_INDICATIONS_BOOT_TO_FW_UI, low bit of the u64 */
}

/* GetNextVariableName yields CHAR16 names directly (no filename/GUID string
 * to parse) -- validates "Boot" + 4 hex digits + NUL exactly, so
 * "BootOrder"/"BootNext"/"BootCurrent" (anything past the 4th hex digit) and
 * non-hex digits are rejected. Comparing whole uint16_t units against ASCII
 * constants also rejects any non-ASCII (nonzero high byte) unit for free. */
int boot_num_from_ucs2_name(const uint16_t *name, unsigned *out) {
    if (name[0] != 'B' || name[1] != 'o' || name[2] != 'o' || name[3] != 't') return 0;
    unsigned num = 0;
    for (int i = 0; i < 4; i++) {
        uint16_t c = name[4 + i];
        unsigned v;
        if (c >= '0' && c <= '9') v = (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'F') v = (unsigned)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') v = (unsigned)(c - 'a' + 10);
        else return 0;
        num = (num << 4) | v;
    }
    if (name[8] != 0) return 0;
    *out = num;
    return 1;
}
