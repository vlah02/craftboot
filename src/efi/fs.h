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

/* Enumerates directory `dir` on the boot volume. For each regular file whose
 * ASCII name ends with `ext` (case-sensitive suffix), copies the name (<=255
 * chars) into names[n] and increments n, up to `max`. Skips subdirectories
 * and "."/"..". Returns the count (0 if none), or -1 if `dir` can't be opened. */
int fs_list(const CHAR16 *dir, const char *ext, char (*names)[256], int max);

/* Builds a full device path to `path` (rooted at the ESP volume, e.g.
 * L"\\EFI\\craftboot\\target.efi") on the same boot volume craftboot.efi was
 * loaded from: craftboot's own volume device path (via
 * LoadedImage->DeviceHandle) with a Media FilePath node for `path` appended
 * before the terminating END node. Used to hand a chainloaded image a real
 * device path (LoadedImage->FilePath) instead of NULL, so a real shim/GRUB
 * second stage can locate its own next stage relative to itself.
 * Returns a malloc'd, caller-freed device path, or NULL on failure. */
EFI_DEVICE_PATH_PROTOCOL *dp_for_esp_path(const CHAR16 *path);

/* Derives craftboot.efi's own install directory from
 * LoadedImage->FilePath (M-B Task 2): walks the device path for the first
 * Media(0x04)/FilePath(0x04) node, converts its CHAR16 path to ASCII, and
 * strips the trailing leaf (everything after the last '\') -- e.g.
 * "\EFI\craftbootv3\grubx64.efi" -> "\EFI\craftbootv3". Lets config/asset
 * loading work no matter where craftboot.efi is installed (M-A hardcoded
 * "\EFI\craftboot"). Writes the result into out (cap bytes, NUL-terminated)
 * and returns 0 on success; on any failure (no LoadedImage, no FilePath, no
 * FilePath node, path with no directory component, or result too large for
 * cap) leaves out empty ("") and returns -1. */
int self_base_dir(char *out, int cap);

#endif /* FS_H */
