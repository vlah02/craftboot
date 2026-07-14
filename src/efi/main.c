/* EFI entry point: set globals, allocate the mini_libc arena, init the
 * timing/RNG + file-system backends, load the boot config, open GOP + input,
 * run the real Minecraft menu, and hand off to the selected entry. */
#include "efi/efi.h"
#include "efi/mini_libc.h"
#include "efi/fs.h"
#include "efi/sys.h"
#include "core/assets.h"
#include "core/menu.h"
#include "platform/display.h"
#include "platform/input.h"
#include "boot/actions.h"

EFI_SYSTEM_TABLE *ST;
EFI_BOOT_SERVICES *BS;
/* Defined (and now assigned) here so src/efi/actions_efi.c's
 * `extern EFI_HANDLE g_image;` resolves for the chainload handoff. */
EFI_HANDLE g_image;

/* Bump-allocator arena for mini_libc. Sized for a 1920x1080 render: decoded
 * panorama (~16 MB), projection LUTs (~30 MB), scaled sprites/fonts, plus
 * stb_image's realloc-copy churn (the bump allocator never reclaims). 512 MB
 * leaves generous headroom on the 2 GB QEMU guest. */
#define ARENA_PAGES (131072u)   /* 131072 * 4 KiB = 512 MiB */

static void fatal(const CHAR16 *msg) {
    if (ST->ConOut) ST->ConOut->OutputString(ST->ConOut, (CHAR16*)msg);
    for (;;) BS->Stall(1000000);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    ST = st;
    BS = st->BootServices;
    g_image = image;
    BS->SetWatchdogTimer(0, 0, 0, (CHAR16*)0);

    /* mini_libc allocator must be live before the first malloc. */
    UINT64 arena = 0;
    if (BS->AllocatePages(AllocateAnyPages, EfiLoaderData, ARENA_PAGES, &arena) != 0 || !arena)
        fatal((CHAR16*)L"arena alloc failed\r\n");
    mini_libc_init((void*)(uintptr_t)arena, (size_t)ARENA_PAGES * 4096u);

    sys_init();
    fs_init(image);

    /* Read config/assets relative to wherever craftboot.efi was actually
     * loaded from (derived from LoadedImage->FilePath), not a hardcoded
     * M-A-era install path -- M-B installs to "\EFI\craftbootv3", so being
     * install-location-agnostic matters. Fall back to M-A's layout if the
     * device path can't be walked (e.g. an odd firmware). */
    char base[192];
    if (self_base_dir(base, sizeof base) != 0)
        snprintf(base, sizeof base, "\\EFI\\craftboot");

    char cfg_path[256];
    snprintf(cfg_path, sizeof cfg_path, "%s\\boot_entries.json", base);
    char assets_dir[256];
    snprintf(assets_dir, sizeof assets_dir, "%s\\assets", base);

    config_t cfg;
    if (config_load(&cfg, cfg_path) != 0)
        fatal((CHAR16*)L"config load failed\r\n");

    display_t *d = display_open(1920, 1080);
    if (!d) fatal((CHAR16*)L"display_open failed\r\n");
    input_t *in = input_open(d);

    const entry_t *e = menu_run(d, in, &cfg, assets_dir);
    if (e) action_execute(e, 1);      /* live handoff (chainload/bootnext/uefi) */

    for (;;) BS->Stall(1000000);       /* handoff returned or menu quit: idle */
    return 0;
}
