/* UEFI Simple File System reader: opens the volume craftboot.efi was loaded
 * from and reads files into malloc'd buffers (config/asset loading, Task 8+). */
#ifndef FS_H
#define FS_H

#include "efi/efi.h"

/* Opens the boot volume via LoadedImage->DeviceHandle + Simple File System.
 * Call once from efi_main before fs_read(). Leaves the internal root handle
 * NULL (fs_read always fails) if the image/device/volume can't be opened. */
void fs_init(EFI_HANDLE image);

/* Reads path (rooted at the ESP volume, e.g. L"\\EFI\\craftboot\\config.txt")
 * into a freshly malloc'd buffer and stores its length in *len_out. Returns
 * NULL on any failure (fs_init not called, file not found, GetInfo/Read
 * error) -- *len_out is left untouched on failure. */
unsigned char *fs_read(const CHAR16 *path, UINTN *len_out);

#endif /* FS_H */
