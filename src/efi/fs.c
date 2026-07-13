/* UEFI Simple File System reader. Opens the boot volume (the ESP
 * craftboot.efi was loaded from, reached via LoadedImage->DeviceHandle) and
 * reads files into malloc'd buffers for the config/asset loaders (Task 8+).
 * Freestanding: no libc, only efi.h + mini_libc + fs.h. */
#ifdef EFI
#include "efi/efi.h"
#include "efi/mini_libc.h"
#include "efi/fs.h"

extern EFI_BOOT_SERVICES *BS;

#ifndef EFI_FILE_DIRECTORY
#define EFI_FILE_DIRECTORY 0x0000000000000010ULL
#endif

static EFI_FILE_PROTOCOL *g_root; /* set by fs_init(image); NULL on failure */

void fs_init(EFI_HANDLE image) {
    EFI_GUID li_guid  = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fsp;

    if (BS->HandleProtocol(image, &li_guid, (void**)&loaded) != 0) return;
    if (BS->HandleProtocol(loaded->DeviceHandle, &sfs_guid, (void**)&fsp) != 0) return;
    fsp->OpenVolume(fsp, &g_root); /* leaves g_root NULL on failure */
}

unsigned char *fs_read(const CHAR16 *path, UINTN *len_out) {
    if (!g_root) return NULL;

    EFI_FILE_PROTOCOL *f;
    if (g_root->Open(g_root, &f, (CHAR16*)path, EFI_FILE_MODE_READ, 0) != 0) return NULL;

    /* File size via GetInfo(EFI_FILE_INFO_ID) rather than
     * SetPosition(end)+GetPosition: GetPosition is declared as an untyped
     * void* in efi.h, so calling through it would be undefined behavior.
     * GetInfo is fully typed and spec-correct, and EFI_FILE_INFO already
     * carries a fixed CHAR16 FileName[256] tail, so a plain stack
     * EFI_FILE_INFO is large enough for any path this project uses. */
    EFI_GUID info_guid = EFI_FILE_INFO_ID;
    EFI_FILE_INFO info;
    UINTN info_sz = sizeof(info);
    if (f->GetInfo(f, &info_guid, &info_sz, &info) != 0) { f->Close(f); return NULL; }

    UINTN n = (UINTN)info.FileSize;
    unsigned char *buf = malloc(n);
    if (!buf) { f->Close(f); return NULL; }

    if (f->Read(f, &n, buf) != 0) { f->Close(f); free(buf); return NULL; }

    f->Close(f);
    *len_out = n;
    return buf;
}

int fs_list(const CHAR16 *dir, const char *ext, char (*names)[256], int max) {
    if (!g_root) return -1;

    EFI_FILE_PROTOCOL *d;
    if (g_root->Open(g_root, &d, (CHAR16*)dir, EFI_FILE_MODE_READ, 0) != 0) return -1;

    size_t extlen = strlen(ext);
    int n = 0;
    for (;;) {
        EFI_FILE_INFO info;
        UINTN sz = sizeof(info);
        if (d->Read(d, &sz, &info) != 0) break;   /* read error: stop */
        if (sz == 0) break;                        /* end of directory */
        if (info.Attribute & EFI_FILE_DIRECTORY) continue;

        /* CHAR16 FileName -> ASCII (names are plain ASCII on this ESP). */
        char name[256]; int i = 0;
        for (; info.FileName[i] && i < 255; i++) name[i] = (char)(info.FileName[i] & 0xff);
        name[i] = 0;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;

        size_t nl = (size_t)i;
        if (nl < extlen || strcmp(name + nl - extlen, ext) != 0) continue;

        if (n < max) {
            for (int j = 0; j <= i; j++) names[n][j] = name[j];
            n++;
        }
        if (n >= max) break;
    }
    d->Close(d);
    return n;
}
#endif /* EFI */
