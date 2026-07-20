/* UEFI Simple Text Input backend implementing platform/input.h. Adapted from
 * the POC's keyboard polling (poc3/poc3.c): poll the console's Simple Text
 * Input protocol via the global system table and map scan codes / unicode
 * chars to action_t. ReadKeyStroke returns EFI_NOT_READY (nonzero) when the
 * input queue is empty, which is exactly the non-blocking ACT_NONE case
 * menu_run expects on every backend. Freestanding: no libc, only efi.h and
 * display.h/input.h. */
#ifdef EFI
#include "efi/efi.h"
#include "platform/display.h"
#include "platform/input.h"

extern EFI_SYSTEM_TABLE *ST;

struct input { int unused; };

input_t *input_open(display_t *d) {
    (void)d;
    static input_t in;
    return &in;
}

action_t input_poll(input_t *in) {
    (void)in;
    EFI_INPUT_KEY k;
    if (ST->ConIn->ReadKeyStroke(ST->ConIn, &k) != 0) return ACT_NONE; /* queue empty */
    if (k.ScanCode == 0x01) return ACT_UP;
    if (k.ScanCode == 0x02) return ACT_DOWN;
    if (k.ScanCode == 0x17) return ACT_BACK;
    if (k.UnicodeChar == L'\r') return ACT_SELECT;
    return ACT_NONE;
}

void input_close(input_t *in) { (void)in; }

#endif /* EFI */
