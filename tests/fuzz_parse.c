/* Fuzz-lite harness for the two attacker-facing parsers:
 *   - efi_load_option_description(): decodes whatever happens to be sitting
 *     in efivarfs NVRAM (see src/boot/actions.c).
 *   - config_load(): decodes boot_entries.json, which on a real device is
 *     read from the EFI System Partition (see src/core/assets.c).
 *
 * This is NOT a test_*.c: it is not run by `make test` (too slow for the
 * inner dev loop). It is built and run only by `make fuzz`, which compiles
 * it with -fsanitize=address,undefined -- the pass/fail signal is "the
 * process didn't crash / didn't trip a sanitizer report", not an OK() count.
 *
 * Deterministic by design: a fixed-seed xorshift64 PRNG, never time()/rand().
 * Same seed => same byte streams => same run every time, so a sanitizer
 * finding here is always reproducible.
 */
#include "boot/actions.h"
#include "core/assets.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EFI_ITERS     200000
#define CONFIG_ITERS  2000
#define BUF_CAP       600
#define OUT_CAP       256

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;  /* fixed seed: deterministic */

static uint64_t xorshift64(void) {
    /* xorshift64 is degenerate at state==0 (stays 0 forever); guard it so a
     * bad reseed can never silently turn the "random" stream constant. */
    if (rng_state == 0) rng_state = 0x9E3779B97F4A7C15ULL;
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static void fill_random(unsigned char *b, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint64_t r = xorshift64();
        for (int k = 0; k < 8 && i < n; k++, i++) b[i] = (unsigned char)(r >> (8 * k));
    }
}

static void fuzz_efi_load_option_description(void) {
    unsigned char buf[BUF_CAP];
    char out[OUT_CAP];
    for (int iter = 0; iter < EFI_ITERS; iter++) {
        size_t n = xorshift64() % (BUF_CAP + 1);        /* 0..600 */
        fill_random(buf, n);
        size_t cap = xorshift64() % (OUT_CAP + 1);      /* 0..256, <= sizeof(out) */
        int r = efi_load_option_description(buf, n, out, cap);
        if (r != -1 && r != 0) {
            fprintf(stderr, "fuzz: efi_load_option_description returned %d "
                            "(n=%zu cap=%zu) at iter %d\n", r, n, cap, iter);
            exit(1);
        }
    }
}

static void fuzz_config_load(const char *path) {
    unsigned char buf[BUF_CAP];
    for (int iter = 0; iter < CONFIG_ITERS; iter++) {
        size_t n = xorshift64() % (BUF_CAP + 1);
        fill_random(buf, n);
        FILE *f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "fuzz: could not open %s for writing\n", path); exit(1); }
        size_t wrote = fwrite(buf, 1, n, f);
        fclose(f);
        if (wrote != n) { fprintf(stderr, "fuzz: short write to %s\n", path); exit(1); }
        config_t c;
        (void)config_load(&c, path);   /* return value ignored: only must not crash */
    }
}

int main(void) {
    fuzz_efi_load_option_description();
    fprintf(stderr, "fuzz: efi_load_option_description ok (%d iters)\n", EFI_ITERS);

    char path[] = "/tmp/craftboot_fuzz_config_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { fprintf(stderr, "fuzz: mkstemp failed\n"); return 1; }
    close(fd);
    fuzz_config_load(path);
    unlink(path);
    fprintf(stderr, "fuzz: config_load ok (%d iters)\n", CONFIG_ITERS);

    printf("fuzz: all clear\n");
    return 0;
}
