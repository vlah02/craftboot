#pragma once
#include <stdint.h>
#include <stddef.h>
/* Read a whole file into a malloc'd buffer (NUL-terminated, *n = byte length).
 * Returns NULL on any failure. Caller frees. (Host: fopen; EFI: fs_read.) */
char    *plat_slurp(const char *path, long *n);
/* List files in `dir` whose name ends with `ext` (e.g. ".jpg"); write up to
 * `max` base names into names[i] (each up to 256). Returns the count, or -1 if
 * the directory can't be opened. (Host: opendir/readdir; EFI: SFS enum.) */
int      plat_list_dir(const char *dir, const char *ext, char (*names)[256], int max);
/* A random 64-bit value. (Host: getrandom; EFI: EFI_RNG_PROTOCOL/TSC.) */
uint64_t plat_rand(void);
/* Monotonic seconds since some fixed epoch. (Host: CLOCK_MONOTONIC; EFI: TSC.) */
double   plat_now(void);
/* Sleep for `seconds` (used by the fatal-error screen). (Host: nanosleep; EFI: Stall.) */
void     plat_sleep(double seconds);
/* Emit one diagnostic line (already formatted, no trailing newline required).
 * May be a no-op. (Host: fprintf(stderr,"%s\n"); EFI: ConOut or no-op.) */
void     plat_log(const char *msg);
