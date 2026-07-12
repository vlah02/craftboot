#pragma once
#include <stdint.h>
#include <stddef.h>
#include "core/render.h"

typedef enum { E_WINDOWS, E_KEXEC, E_SUBMENU, E_INFO, E_UEFI, E_BACK } etype_t;
typedef struct {
    etype_t type;
    char id[32], label[64], target[16];
    char kernel[128], initrd[128], cmdline[256];
} entry_t;
typedef struct {
    char root_uuid[40];
    entry_t menu[2][8];
    int   nmenu[2];
} config_t;
int config_load(config_t *c, const char *path);
img_t img_load(const char *path);
void  img_free(img_t *s);
