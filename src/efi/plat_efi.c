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

/* ---- multi-core render via MP Services ------------------------------------
 * StartupAllAPs runs mp_ap_entry on every application processor concurrently
 * (BSP blocks inside StartupAllAPs). Each AP enables SIMD on its own core
 * (firmware leaves CR4.OSXSAVE=0 per-core, so AVX2 would #UD otherwise), then
 * work-steals row-slices via an atomic counter until all are claimed -- robust
 * no matter how many APs actually run, and load-balanced. Slices write disjoint
 * back-buffer rows, so no locking; StartupAllAPs' completion is the barrier. */
static void (*g_mp_fn)(void *, int, int);
static void *g_mp_ctx;
static volatile int g_mp_next, g_mp_total;
static EFI_MP_SERVICES_PROTOCOL *g_mp;

static void mp_simd_enable(void) {
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 9) | (1u << 10) | (1u << 18);       /* OSFXSR | OSXMMEXCPT | OSXSAVE */
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
    __asm__ volatile("xsetbv" :: "a"(7u), "d"(0u), "c"(0u));  /* XCR0 = x87|SSE|AVX */
}

static void EFIAPI mp_ap_entry(void *arg) {
    (void)arg;
    mp_simd_enable();
    int s;
    while ((s = __atomic_fetch_add(&g_mp_next, 1, __ATOMIC_SEQ_CST)) < g_mp_total)
        g_mp_fn(g_mp_ctx, s, g_mp_total);
}

void plat_run_on_all(void (*fn)(void *ctx, int idx, int nproc), void *ctx) {
    if (!g_mp) {
        EFI_GUID g = EFI_MP_SERVICES_PROTOCOL_GUID;
        if (ST->BootServices->LocateProtocol(&g, (void*)0, (void**)&g_mp) != 0) g_mp = (void*)0;
    }
    UINTN nproc = 1, nen = 1;
    if (!g_mp || g_mp->GetNumberOfProcessors(g_mp, &nproc, &nen) != 0 || nen < 2) {
        fn(ctx, 0, 1);                 /* single core: render the whole frame here */
        return;
    }
    g_mp_fn = fn; g_mp_ctx = ctx;
    g_mp_next = 0; g_mp_total = (int)(nen - 1);      /* one slice per application processor */
    g_mp->StartupAllAPs(g_mp, mp_ap_entry, FALSE /*concurrent*/, (void*)0 /*blocking*/, 0, (void*)0, (void*)0);
}
#endif /* EFI */
