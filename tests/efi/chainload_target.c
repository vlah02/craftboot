/* Task 9 chainload TARGET: a trivial second EFI app used to prove that
 * craftboot's E_CHAINLOAD handoff (src/efi/actions_efi.c: fs_read + LoadImage
 * + StartImage) actually transfers control to a *different* PE image with
 * zero firmware reboot in between. Adapted from the proven POC-4 target
 * (scratchpad/efipoc/poc4/poc4_target.c): fill the framebuffer a distinct
 * solid color, print a unique serial marker, then loop forever.
 *
 * Freestanding: only efi/efi.h, no mini_libc -- no malloc/strings/math
 * needed for something this small. */
#include "efi/efi.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    (void)ImageHandle;
    EFI_BOOT_SERVICES *BS = SystemTable->BootServices;

    SystemTable->ConOut->OutputString(SystemTable->ConOut,
        (CHAR16*)L"CHAINLOAD_TARGET_OK\r\n");

    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS st = BS->LocateProtocol(&gopGuid, NULL, (VOID**)&gop);
    if (!EFI_ERROR(st) && gop) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
        UINTN width  = info->HorizontalResolution;
        UINTN height = info->VerticalResolution;
        UINTN pitch  = info->PixelsPerScanLine;
        volatile UINT32 *fb = (volatile UINT32*)gop->Mode->FrameBufferBase;
        UINT32 RED = 0x00FF0000; /* distinct from any panorama/menu color */
        for (UINTN y = 0; y < height; y++)
            for (UINTN x = 0; x < width; x++)
                fb[y * pitch + x] = RED;
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut,
        (CHAR16*)L"CHAINLOAD_TARGET_OK: red screen painted, looping\r\n");

    for (;;) BS->Stall(1000000);
    return EFI_SUCCESS;
}
