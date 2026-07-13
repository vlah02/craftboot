#pragma once
#include <stdint.h>
#include <stddef.h>
#include "core/render.h"

typedef enum { E_WINDOWS, E_KEXEC, E_SUBMENU, E_INFO, E_UEFI, E_BACK, E_BOOTNEXT } etype_t;
typedef struct {
    etype_t type;
    char id[32], label[64], target[16];
    char kernel[128], initrd[128], cmdline[256];
    char match[64];        /* E_BOOTNEXT: firmware load-option description to match */
} entry_t;
typedef struct {
    char root_uuid[40];
    entry_t menu[2][8];
    int   nmenu[2];
} config_t;
int    config_load_mem(config_t *c, const char *json, long n);
int    config_load(config_t *c, const char *path);
img_t  img_load_mem(const unsigned char *bytes, int len);
img_t  img_load(const char *path);
void   img_free(img_t *s);
int    font_load_mem(font_t *f, const unsigned char *png, int pnglen,
                      const char *json, long jsonlen);
int    font_load(font_t *f, const char *png_path, const char *json_path);
