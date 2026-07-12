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
static int skip(const jsmntok_t *t, int nt, int i) {   /* index just past token i */
    int end = t[i].end, j = i + 1;
    while (j < nt && t[j].start < end) j++;             /* jsmn children are contiguous */
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
static void parse_entry(const char *js, const jsmntok_t *t, int nt, int obj, entry_t *e) {
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
        i = skip(t, nt, i + 1);
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
                    parse_entry(js, t, nt, ei, &c->menu[which][a]);
                    c->nmenu[which]++;
                    ei = skip(t, nt, ei);
                }
                mi = skip(t, nt, arr);
            }
        }
        i = skip(t, nt, i + 1);
    }
    free(js);
    return c->nmenu[0] ? 0 : -1;
}

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"
#include "core/render.h"

img_t img_load(const char *path) {
    img_t o = {0}; int n;
    o.rgba = stbi_load(path, &o.w, &o.h, &n, 4);
    return o;
}
void img_free(img_t *s) { free(s->rgba); s->rgba = 0; }

int font_load(font_t *f, const char *png_path, const char *json_path) {
    memset(f, 0, sizeof *f);
    f->atlas = img_load(png_path);
    if (!f->atlas.rgba) return -1;
    long n; char *js = slurp(json_path, &n); if (!js) return -1;
    jsmn_parser p; jsmn_init(&p);
    static jsmntok_t t[2048];
    int nt = jsmn_parse(&p, js, n, t, 2048);
    if (nt < 1) { free(js); return -1; }
    int i = 1;
    for (int k = 0; k < t[0].size; k++) {
        if (teq(js, &t[i], "size")) f->size = atoi(js + t[i + 1].start);
        else if (teq(js, &t[i], "glyphs")) {
            int go = i + 1, gi = go + 1;
            for (int g = 0; g < t[go].size; g++) {
                unsigned char ch = (unsigned char)js[t[gi].start];
                glyph_t *gl = (ch >= 32 && ch < 127) ? &f->g[ch - 32] : NULL;
                int obj = gi + 1, fi = obj + 1;
                for (int q = 0; q < t[obj].size; q++) {
                    int v = atoi(js + t[fi + 1].start);
                    if (gl) {
                        if      (teq(js, &t[fi], "x")) gl->x = (short)v;
                        else if (teq(js, &t[fi], "y")) gl->y = (short)v;
                        else if (teq(js, &t[fi], "w")) gl->w = (short)v;
                        else if (teq(js, &t[fi], "h")) gl->h = (short)v;
                        else if (teq(js, &t[fi], "adv")) gl->adv = (short)v;
                    }
                    fi = skip(t, nt, fi + 1);
                }
                gi = skip(t, nt, obj);
            }
        }
        i = skip(t, nt, i + 1);
    }
    free(js);
    return f->size ? 0 : -1;
}
