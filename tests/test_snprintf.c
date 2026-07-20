/* Host unit test for the EFI mini snprintf (src/efi/mini_libc.c).
 *
 * snprintf lives in mini_libc.c, which is compiled only for the freestanding
 * EFI target. To exercise its formatting logic on the host WITHOUT linking the
 * rest of mini_libc (whose malloc/memcpy/strlen/... would clash with the host
 * libc this test links against), we textually include mini_libc.c with
 * MINI_LIBC_SNPRINTF_TEST defined -- that guard drops everything except the
 * pure `mini_vsnprintf` core (and its static helpers).
 *
 * A snprintf bug corrupts on-screen menu text and asset paths in firmware,
 * where QEMU screenshots won't reliably reveal a subtle format error -- so the
 * formatter is pinned here, on the host, against exact expected strings. */
#include "t.h"
#include <stdarg.h>

#define MINI_LIBC_SNPRINTF_TEST
#include "efi/mini_libc.c"

/* Variadic front-end over the va_list core, mirroring the real snprintf(). */
static int tf(char *dst, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = mini_vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    return r;
}

#define EXPECT(want, ...) do {                          \
    char _b[128]; int _r = tf(_b, sizeof _b, __VA_ARGS__); \
    OK(strcmp(_b, want) == 0);                          \
    OK(_r == (int)strlen(want));                        \
} while (0)

static int strings(void) {
    EXPECT("hello", "%s", "hello");
    EXPECT("", "%s", "");
    EXPECT("(null)", "%s", (char*)0);
    EXPECT("a/b", "%s/%s", "a", "b");
    EXPECT("\\EFI\\craftboot\\assets/fonts/baked/small.png",
           "%s/fonts/baked/small.png", "\\EFI\\craftboot\\assets");
    return 0;
}

static int integers(void) {
    EXPECT("0", "%d", 0);
    EXPECT("42", "%d", 42);
    EXPECT("-5", "%d", -5);
    EXPECT("-2147483648", "%d", (int)(-2147483647 - 1)); /* INT_MIN */
    EXPECT("4294967295", "%u", 4294967295u);
    EXPECT("0", "%u", 0u);
    return 0;
}

static int hex_and_char(void) {
    EXPECT("2a", "%x", 0x2au);
    EXPECT("2A", "%X", 0x2au);
    EXPECT("002A", "%04X", 0x2au);        /* width + zero-pad + uppercase */
    EXPECT("00ff", "%04x", 0xffu);
    EXPECT("deadbeef", "%x", 0xdeadbeefu);
    EXPECT("X", "%c", 'X');
    EXPECT("100%", "100%%");
    return 0;
}

static int floats(void) {
    EXPECT("60", "%.0f", 59.6);           /* rounds up, no decimal point */
    EXPECT("60", "%.0f", 59.96);
    EXPECT("0", "%.0f", 0.4);
    EXPECT("60.0", "%.1f", 59.96);        /* carry into integer part */
    EXPECT("0.1", "%.1f", 0.05);          /* rounds half up */
    EXPECT("3.1", "%.1f", 3.14);
    EXPECT("-2.5", "%.1f", -2.45);
    EXPECT("719 fps", "%.0f fps", 719.3);
    EXPECT("Joining world in 12 seconds", "Joining world in %d seconds", 12);
    return 0;
}

static int truncation(void) {
    char b[4];
    int r = tf(b, sizeof b, "%s", "hello");   /* cap 4: 3 chars + NUL */
    OK(strcmp(b, "hel") == 0);
    OK(r == 5);                                /* would-be length (C99) */

    char b2[1];
    r = tf(b2, sizeof b2, "%d", 12345);        /* cap 1: only NUL fits */
    OK(b2[0] == 0);
    OK(r == 5);

    /* cap == 0: write nothing at all, still report would-be length. */
    char sentinel = 0x7f;
    r = tf(&sentinel, 0, "%d", 99);
    OK(sentinel == 0x7f);
    OK(r == 2);
    return 0;
}

int main(void) {
    RUN(strings);
    RUN(integers);
    RUN(hex_and_char);
    RUN(floats);
    RUN(truncation);
    fprintf(stderr, "test_snprintf: ALL PASS\n");
    return 0;
}
