/* TSC-based monotonic timing + RNG for the EFI app (panorama rotation, fps
 * counter, random world selection). Freestanding: no libc, only efi.h + sys.h. */
#ifdef EFI
#include "efi/efi.h"
#include "efi/sys.h"

extern EFI_BOOT_SERVICES *BS;

static uint64_t rdtsc(void) {
    unsigned a, d;
    __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
    return ((uint64_t)d << 32) | a;
}

static uint64_t g_hz; /* calibrated TSC ticks/sec */
static uint64_t g_t0; /* TSC value at sys_init() */

void sys_init(void) {
    uint64_t a = rdtsc();
    BS->Stall(100000); /* 100ms */
    uint64_t b = rdtsc();
    g_hz = (b - a) * 10; /* ticks in 100ms, scaled up to ticks/sec */
    g_t0 = b;
}

double sys_now(void) {
    return (double)(rdtsc() - g_t0) / (double)g_hz;
}

uint64_t sys_rng64(void) {
    EFI_GUID rng_guid = EFI_RNG_PROTOCOL_GUID;
    EFI_RNG_PROTOCOL *r;
    uint64_t v;
    if (BS->LocateProtocol(&rng_guid, NULL, (void**)&r) == 0 &&
        r->GetRNG(r, NULL, sizeof(v), (UINT8*)&v) == 0) {
        return v;
    }
    return rdtsc(); /* fallback: not cryptographically random, but fine for
                        picking a random world at boot */
}
#endif /* EFI */
