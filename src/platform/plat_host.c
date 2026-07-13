#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "platform/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/random.h>
char *plat_slurp(const char *path, long *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    *n = sz;
    char *b = malloc((size_t)sz + 1); if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) { fclose(f); free(b); return NULL; }
    b[*n] = 0; fclose(f); return b;
}
int plat_list_dir(const char *dir, const char *ext, char (*names)[256], int max) {
    DIR *d = opendir(dir); if (!d) return -1;
    struct dirent *e; int n = 0;
    while ((e = readdir(d)) && n < max) {
        const char *dot = strrchr(e->d_name, '.');
        if (dot && !strcmp(dot, ext)) { snprintf(names[n], 256, "%s", e->d_name); n++; }
    }
    closedir(d); return n;
}
uint64_t plat_rand(void) { uint64_t r; if (getrandom(&r, sizeof r, 0) < 0) r = 0; return r; }
double plat_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
void plat_sleep(double s) { struct timespec ts = { (time_t)s, (long)((s - (double)(time_t)s) * 1e9) }; nanosleep(&ts, NULL); }
void plat_log(const char *msg) { fprintf(stderr, "%s\n", msg); }
