#!/bin/bash
# Build & sign the craftboot Unified Kernel Image (systemd-stub + kernel +
# initramfs + cmdline) for kernel version $1 (default: running kernel).
# Output: /boot/efi/EFI/craftboot/grubx64.efi  (shim's second-stage name),
# signed with the MOK key from ./boot/uki-setup.sh genkey.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HERE/build"
KEYDIR="/var/lib/craftboot"
STUB="/usr/lib/systemd/boot/efi/linuxx64.efi.stub"
ESPDIR="/boot/efi/EFI/craftboot"
CMDLINE="console=tty1 quiet loglevel=3 vt.global_cursor_default=0"

[[ $EUID -eq 0 ]] || { echo "Please run with sudo."; exit 1; }
KVER="${1:-$(uname -r)}"
KERNEL="/boot/vmlinuz-$KVER"

[[ -f "$STUB" ]]            || { echo "Missing $STUB — run: sudo apt install systemd-boot-efi"; exit 1; }
[[ -f "$KEYDIR/MOK.key" ]] || { echo "No MOK key — run: sudo $HERE/uki-setup.sh genkey"; exit 1; }
[[ -f "$KERNEL" ]]         || { echo "No kernel at $KERNEL"; exit 1; }
[[ -f "$B/craftboot.initrd" ]] || { echo "Build the initramfs first: $HERE/build.sh $KVER"; exit 1; }

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
printf '%s' "$CMDLINE" > "$tmp/cmdline"

# Place .initrd 2 MiB-aligned just above the kernel so the sections don't overlap
ksz=$(stat -c%s "$KERNEL")
linux_vma=0x2000000
initrd_vma=$(printf '0x%x' $(( 0x2000000 + ((ksz + 0x1fffff) & ~0x1fffff) )))

objcopy \
    --add-section .osrel="/etc/os-release"        --change-section-vma .osrel=0x20000 \
    --add-section .cmdline="$tmp/cmdline"          --change-section-vma .cmdline=0x30000 \
    --add-section .linux="$KERNEL"                 --change-section-vma .linux="$linux_vma" \
    --add-section .initrd="$B/craftboot.initrd"    --change-section-vma .initrd="$initrd_vma" \
    "$STUB" "$tmp/craftboot-unsigned.efi"

mkdir -p "$ESPDIR"
sbsign --key "$KEYDIR/MOK.key" --cert "$KEYDIR/MOK.crt" \
       --output "$ESPDIR/grubx64.efi" "$tmp/craftboot-unsigned.efi"
echo "Signed UKI -> $ESPDIR/grubx64.efi ($(du -h "$ESPDIR/grubx64.efi" | cut -f1)) for kernel $KVER"
