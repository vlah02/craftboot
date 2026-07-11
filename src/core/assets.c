#include "core/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define JSMN_STATIC
#include "jsmn.h"

static char *slurp(const char *path, long *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(*n + 1);
    if (fread(b, 1, *n, f) != (size_t)*n) { fclose(f); free(b); return NULL; }
    b[*n] = 0; fclose(f); return b;
}
static int teq(const char *js, const jsmntok_t *t, const char *s) {
    return t->type == JSMN_STRING && (int)strlen(s) == t->end - t->start &&
           !strncmp(js + t->start, s, t->end - t->start);
}
static void tcpy(char *dst, size_t cap, const char *js, const jsmntok_t *t) {
    size_t n = (size_t)(t->end - t->start); if (n >= cap) n = cap - 1;
    memcpy(dst, js + t->start, n); dst[n] = 0;
}
static int skip(const jsmntok_t *t, int i) {          /* index just past token i */
    int end = t[i].end, j = i + 1;
    while (t[j].start != -1 && t[j].start < end) j++;  /* jsmn children are contiguous */
    return j;
}
static etype_t etype(const char *s) {
    if (!strcmp(s, "windows")) return E_WINDOWS;
    if (!strcmp(s, "kexec"))   return E_KEXEC;
    if (!strcmp(s, "submenu")) return E_SUBMENU;
    if (!strcmp(s, "uefi"))    return E_UEFI;
    if (!strcmp(s, "back"))    return E_BACK;
    return E_INFO;
}
static void parse_entry(const char *js, const jsmntok_t *t, int obj, entry_t *e) {
    memset(e, 0, sizeof *e);
    int i = obj + 1;
    for (int k = 0; k < t[obj].size; k++) {
        const jsmntok_t *key = &t[i], *val = &t[i + 1];
        char buf[256]; tcpy(buf, sizeof buf, js, val);
        if      (teq(js, key, "id"))      tcpy(e->id, sizeof e->id, js, val);
        else if (teq(js, key, "label"))   tcpy(e->label, sizeof e->label, js, val);
        else if (teq(js, key, "type"))    e->type = etype(buf);
        else if (teq(js, key, "target"))  tcpy(e->target, sizeof e->target, js, val);
        else if (teq(js, key, "kernel"))  tcpy(e->kernel, sizeof e->kernel, js, val);
        else if (teq(js, key, "initrd"))  tcpy(e->initrd, sizeof e->initrd, js, val);
        else if (teq(js, key, "cmdline")) tcpy(e->cmdline, sizeof e->cmdline, js, val);
        i = skip(t, i + 1);
    }
}
int config_load(config_t *c, const char *path) {
    memset(c, 0, sizeof *c);
    long n; char *js = slurp(path, &n); if (!js) return -1;
    jsmn_parser p; jsmntok_t t[512];
    jsmn_init(&p);
    int nt = jsmn_parse(&p, js, n, t, 512);
    if (nt < 1 || t[0].type != JSMN_OBJECT) { free(js); return -1; }
    int i = 1;
    for (int k = 0; k < t[0].size; k++) {
        const jsmntok_t *key = &t[i];
        if (teq(js, key, "root_uuid")) tcpy(c->root_uuid, sizeof c->root_uuid, js, &t[i + 1]);
        else if (teq(js, key, "menus")) {
            int mo = i + 1, mi = mo + 1;
            for (int m = 0; m < t[mo].size; m++) {
                int which = teq(js, &t[mi], "extras") ? 1 : 0;   /* "main" -> 0 */
                int arr = mi + 1, ei = arr + 1;
                for (int a = 0; a < t[arr].size && a < 8; a++) {
                    parse_entry(js, t, ei, &c->menu[which][a]);
                    c->nmenu[which]++;
                    ei = skip(t, ei);
                }
                mi = skip(t, arr);
            }
        }
        i = skip(t, i + 1);
    }
    free(js);
    return c->nmenu[0] ? 0 : -1;
}
