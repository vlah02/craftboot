#pragma once
#include <stdint.h>
#include <stddef.h>

/* Pure, dependency-free EFI-variable helpers shared by the host build (Linux
 * efivarfs path in src/boot/actions.c, host-tested here) and the freestanding
 * EFI app (src/efi/actions_efi.c, firmware runtime-service path). No libc:
 * every byte loop is hand-written so this compiles unchanged under both the
 * host toolchain and the mingw -ffreestanding EFI build. */

/* Compares a UCS-2 (CHAR16) buffer `u` (maxb bytes available) against an
 * ASCII NUL-terminated string `a`: returns 1 iff every unit of `u` up to and
 * including its own NUL terminator matches `a` unit-for-unit (high byte 0),
 * 0 otherwise (including out-of-bounds reads that would be needed to keep
 * comparing). */
int ucs2_equals_ascii(const unsigned char *u, size_t maxb, const char *a);

/* Parses a RAW EFI_LOAD_OPTION as firmware GetVariable returns it: u32
 * Attributes, u16 FilePathListLength, then a NUL-terminated CHAR16
 * Description[] -- 6-byte header (NOT the 10-byte efivarfs form, which also
 * carries a 4-byte efivarfs attribute prefix). Bounds-checks every read
 * against `n`; decodes ASCII only (any CHAR16 unit with a nonzero high byte
 * fails closed). Returns 0 on success (`out` NUL-terminated ASCII), -1 on any
 * short/malformed/oversized input. */
int efi_load_option_desc(const unsigned char *lo, size_t n,
                          char *out, size_t cap);

/* Read-modify-write the raw 8-byte little-endian OsIndications u64 as the
 * firmware GetVariable/SetVariable pair sees it (no efivarfs 4-byte
 * attribute prefix -- SetVariable takes attributes as a separate argument).
 * `cur`/`n` is the prior GetVariable result (NULL/too-short treated as a
 * fresh zero value); `out` is always fully written with
 * EFI_OS_INDICATIONS_BOOT_TO_FW_UI (bit 0) forced on, other bits preserved. */
void osindications_rmw(const unsigned char *cur, size_t n, unsigned char out[8]);

/* Given a CHAR16 variable name as GetNextVariableName yields it, returns 1
 * and sets *out to the parsed boot number iff the name is exactly "Boot" +
 * 4 hex digits + NUL (so "BootOrder"/"BootNext"/"BootCurrent" and anything
 * else past the 4th hex digit are rejected). 0 otherwise. */
int boot_num_from_ucs2_name(const uint16_t *name, unsigned *out);
