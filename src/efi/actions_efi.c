/* EFI boot handoff: the freestanding counterpart to src/boot/actions.c's
 * Linux/efivarfs implementation. Three entry types:
 *   - E_CHAINLOAD: build a real device path to the target .efi on the boot
 *     volume (dp_for_esp_path) and LoadImage via the device-path form (no
 *     SourceBuffer) + StartImage -- zero reboot. The device-path form leaves
 *     the child's LoadedImage->FilePath populated, so a real shim/GRUB
 *     second stage can locate its own next stage (M-B Task 1). M-A proved
 *     the handoff itself via the SourceBuffer form (poc4.c/Task 9), which
 *     left FilePath NULL -- fine for a self-contained target, not for shim.
 *   - E_BOOTNEXT: enumerate firmware Boot#### load options via
 *     GetNextVariableName/GetVariable, match the UCS-2 description against
 *     e->match, set BootNext, ResetSystem(EfiResetCold).
 *   - E_UEFI: read-modify-write OsIndications (EFI_OS_INDICATIONS_BOOT_TO_FW_UI),
 *     SetVariable, ResetSystem(EfiResetCold).
 * E_SUBMENU/E_INFO/E_BACK: no handoff, the menu itself handles them.
 */
#include "efi/efi.h"
#include "efi/mini_libc.h"
#include "efi/fs.h"
#include "core/assets.h"
#include "core/efivar.h"
#include "core/actions.h"

extern EFI_SYSTEM_TABLE *ST;
extern EFI_BOOT_SERVICES *BS;
extern EFI_HANDLE g_image;

/* mini_libc has memcmp/strlen but not strcmp. */
static int streq(const char *a, const char *b) {
    size_t la = strlen(a);
    return la == strlen(b) && memcmp(a, b, la) == 0;
}

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b) {
    return memcmp(a, b, sizeof(EFI_GUID)) == 0;
}

int action_execute(const entry_t *e, int live) {
    if (e->type == E_CHAINLOAD) {
        if (!e->path[0]) return -1;
        CHAR16 w[160]; int i = 0;                 /* ASCII path, '/' -> '\' -> CHAR16 */
        for (; e->path[i] && i < 159; i++) w[i] = (CHAR16)(e->path[i] == '/' ? '\\' : (unsigned char)e->path[i]);
        w[i] = 0;
        EFI_DEVICE_PATH_PROTOCOL *dp = dp_for_esp_path(w);
        if (!dp) return -1;
        EFI_HANDLE child = 0;
        EFI_STATUS s = BS->LoadImage(FALSE, g_image, dp, (void*)0, 0, &child);  /* device-path form */
        free(dp);
        if (s != 0 || !child) return -1;
        return BS->StartImage(child, NULL, NULL) == 0 ? 0 : -1;   /* zero-reboot handoff */
    }
    if (e->type == E_BOOTNEXT) {
        if (!e->match[0]) return -1;
        EFI_GUID GLOBAL = EFI_GLOBAL_VARIABLE_GUID;
        unsigned num = 0; int found = 0;
        static CHAR16 name[512]; name[0] = 0;      /* GetNextVariableName starts from empty name */
        for (;;) {
            UINTN nsz = sizeof name; EFI_GUID g;
            if (ST->RuntimeServices->GetNextVariableName(&nsz, name, &g) != 0) break;  /* NOT_FOUND ends */
            unsigned bn;
            if (!guid_eq(&g, &GLOBAL)) continue;
            if (!boot_num_from_ucs2_name((uint16_t*)name, &bn)) continue;
            unsigned char val[1024]; UINTN vsz = sizeof val; UINT32 at;
            if (ST->RuntimeServices->GetVariable(name, &GLOBAL, &at, &vsz, val) != 0) continue;
            char desc[128];
            if (efi_load_option_desc(val, vsz, desc, sizeof desc) == 0 && streq(desc, e->match)) {
                num = bn; found = 1; break;
            }
        }
        if (!live) return found ? 0 : -1;          /* dry-run reports match only */
        if (!found) return -1;
        uint16_t bn16 = (uint16_t)num;
        if (ST->RuntimeServices->SetVariable((CHAR16*)L"BootNext", &GLOBAL, 7, 2, &bn16) != 0) return -1;
        ST->RuntimeServices->ResetSystem(EfiResetCold, 0, 0, NULL);
        return -1;                                 /* ResetSystem never returns */
    }
    if (e->type == E_UEFI) {
        EFI_GUID GLOBAL = EFI_GLOBAL_VARIABLE_GUID;
        unsigned char cur[8]; UINTN sz = sizeof cur; UINT32 at;
        if (ST->RuntimeServices->GetVariable((CHAR16*)L"OsIndications", &GLOBAL, &at, &sz, cur) != 0) sz = 0;
        unsigned char out[8]; osindications_rmw(sz ? cur : (const unsigned char*)0, sz, out);
        if (!live) return 0;
        if (ST->RuntimeServices->SetVariable((CHAR16*)L"OsIndications", &GLOBAL, 7, 8, out) != 0) return -1;
        ST->RuntimeServices->ResetSystem(EfiResetCold, 0, 0, NULL);
        return -1;
    }
    return 0;   /* E_SUBMENU/E_INFO/E_BACK: no handoff (menu handles them) */
}
