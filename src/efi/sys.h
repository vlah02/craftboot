/* TSC-based monotonic timing + RNG for the EFI app (panorama rotation, fps
 * counter, random world selection). */
#ifndef SYS_H
#define SYS_H

#include <stdint.h>

/* Calibrates the TSC frequency via a 100ms BS->Stall busy-wait. Call once
 * from efi_main before sys_now(). */
void sys_init(void);

/* Seconds elapsed since sys_init(), derived from RDTSC. */
double sys_now(void);

/* Random 64-bit value: EFI_RNG_PROTOCOL if the firmware exposes one, else an
 * RDTSC-based fallback (not cryptographically random, but fine for picking a
 * random world at boot). */
uint64_t sys_rng64(void);

#endif /* SYS_H */
