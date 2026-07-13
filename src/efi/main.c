/* Placeholder EFI entry point (GOP-hello). Proves the freestanding toolchain:
 * locate GOP, fill the framebuffer with a recognizable non-black color, idle.
 * Later tasks replace this with the real display/input/fs/handoff backends. */
#include "efi/efi.h"
#include "efi/mini_libc.h"

EFI_SYSTEM_TABLE *ST;
EFI_BOOT_SERVICES *BS;
/* Declared here (uninitialized) so src/efi/actions_efi.c's `extern
 * EFI_HANDLE g_image;` resolves at link time; efi_main below does not yet
 * assign it -- that wiring, along with the display/input/fs/handoff
 * integration this placeholder efi_main doesn't do yet either, is Task 8. */
EFI_HANDLE g_image;

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    (void)image;
    ST = st;
    BS = st->BootServices;
    BS->SetWatchdogTimer(0, 0, 0, NULL);

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    if (BS->LocateProtocol(&gop_guid, NULL, (void**)&gop) == 0 && gop) {
        uint32_t *fb = (uint32_t*)(uintptr_t)gop->Mode->FrameBufferBase;
        uint32_t n = gop->Mode->Info->PixelsPerScanLine * gop->Mode->Info->VerticalResolution;
        for (uint32_t i = 0; i < n; i++) fb[i] = 0x00204060; /* recognizable fill */
    }

    for (;;) BS->Stall(1000000);
    return 0;
}
