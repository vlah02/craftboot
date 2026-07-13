#pragma once
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define OK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)
#define RUN(name) do { fprintf(stderr, "== %s\n", #name); int r = name(); if (r) return r; } while (0)
