#pragma once
#include <stddef.h>
#include "core/assets.h"

int action_execute(const entry_t *e, const char *root_prefix, int live);

/* exposed for tests */
int ucs2_equals_ascii(const unsigned char *ucs2, size_t maxbytes, const char *ascii);
int efi_load_option_description(const unsigned char *var, size_t n,
                                 char *out, size_t cap);
int efi_bootnum_from_name(const char *name, unsigned *out);
