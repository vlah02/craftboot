/* EFI backing for the platform runtime interface (platform/plat.h, Task 8a).
 * Bridges the platform-agnostic core to the EFI primitives: SFS file reads,
 * TSC timing, firmware RNG, BS->Stall, ConOut logging. Freestanding. */
#ifdef EFI
#include "efi/efi.h"
#include "efi/fs.h"
#include "efi/sys.h"
#include "efi/mini_libc.h"
#include "platform/plat.h"

extern EFI_SYSTEM_TABLE *ST;

/* menu.c builds "/"-separated logical paths on an EFI base like
 * "\EFI\craftboot\assets"; convert every '/' to '\' then read via SFS. */
static void to_ucs2_bs(const char *a, CHAR16 *w, int cap) {
    int i = 0;
    for (; a[i] && i < cap - 1; i++)
        w[i] = (CHAR16)(a[i] == '/' ? '\\' : (unsigned char)a[i]);
    w[i] = 0;
}

char *plat_slurp(const char *path, long *n) {
    CHAR16 w[320]; to_ucs2_bs(path, w, 320);
    UINTN len = 0; unsigned char *b = fs_read(w, &len);
    if (!b) return NULL;
    *n = (long)len;
    return (char*)b;   /* fs_read buffer is malloc'd -> caller-freeable via free() */
}

int plat_list_dir(const char *dir, const char *ext, char (*names)[256], int max) {
    CHAR16 w[320]; to_ucs2_bs(dir, w, 320);
    return fs_list(w, ext, names, max);
}

uint64_t plat_rand(void) { return sys_rng64(); }
double   plat_now(void)  { return sys_now(); }

void plat_scratch_reset(void) { mini_libc_frame_reset(); }

void plat_sleep(double s) {
    if (s > 0) ST->BootServices->Stall((UINTN)(s * 1000000.0));
}

void plat_log(const char *msg) {
    /* best-effort console line; ConOut is usable pre-ExitBootServices */
    CHAR16 w[256]; int i = 0;
    for (; msg[i] && i < 253; i++) w[i] = (CHAR16)(unsigned char)msg[i];
    w[i++] = '\r'; w[i++] = '\n'; w[i] = 0;
    if (ST->ConOut) ST->ConOut->OutputString(ST->ConOut, w);
}
#endif /* EFI */
