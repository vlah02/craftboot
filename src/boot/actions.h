#pragma once
#include <stddef.h>
#include "core/assets.h"

int action_execute(const entry_t *e, int live);

/* exposed for tests */
int efi_load_option_description(const unsigned char *var, size_t n,
                                 char *out, size_t cap);
int efi_bootnum_from_name(const char *name, unsigned *out);

/* Pure efivarfs value builders, split out of set_bootnext()/set_boot_to_fw()
 * so the exact byte layout is unit-testable without touching efivarfs.
 * See tests/test_actions.c. */
void build_bootnext_value(unsigned num, unsigned char out[6]);
int build_osindications_value(const unsigned char *existing, size_t n,
                               unsigned char out[12]);
