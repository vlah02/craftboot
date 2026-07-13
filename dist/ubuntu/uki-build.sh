#!/bin/bash
# Build & sign the craftboot Unified Kernel Image (kernel + initramfs + cmdline)
# for kernel version $1 (default: running), using systemd's ukify (correct
# section placement + signing). Output: /boot/efi/EFI/craftboot/grubx64.efi
# (shim's second-stage name), signed with the MOK key from uki-setup.sh genkey.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HERE/../../build"
KEYDIR="/var/lib/craftboot"
ESPDIR="/boot/efi/EFI/craftboot"
CMDLINE="console=tty1 quiet loglevel=3 vt.global_cursor_default=0"

[[ $EUID -eq 0 ]] || { echo "Please run with sudo."; exit 1; }
KVER="${1:-$(uname -r)}"
KERNEL="/boot/vmlinuz-$KVER"

UKIFY="$(command -v ukify 2>/dev/null || true)"
[[ -x "$UKIFY" ]] || { [[ -x /usr/lib/systemd/ukify ]] && UKIFY=/usr/lib/systemd/ukify; }
[[ -x "${UKIFY:-}" ]]      || { echo "Missing ukify — run: sudo apt install systemd-ukify"; exit 1; }
[[ -f "$KEYDIR/MOK.key" ]] || { echo "No MOK key — run: sudo $HERE/uki-setup.sh genkey"; exit 1; }
[[ -f "$KERNEL" ]]         || { echo "No kernel at $KERNEL"; exit 1; }
[[ -f "$B/craftboot.initrd" ]] || { echo "Build the initramfs first: $HERE/build.sh $KVER"; exit 1; }

# 'ukify build ...' on modern systemd; older versions take no subcommand
SUB=(build); "$UKIFY" build --help >/dev/null 2>&1 || SUB=()

mkdir -p "$ESPDIR"
"$UKIFY" "${SUB[@]}" \
    --linux="$KERNEL" \
    --initrd="$B/craftboot.initrd" \
    --cmdline="$CMDLINE" \
    --secureboot-private-key="$KEYDIR/MOK.key" \
    --secureboot-certificate="$KEYDIR/MOK.crt" \
    --output="$ESPDIR/grubx64.efi"

echo "Signed UKI -> $ESPDIR/grubx64.efi ($(du -h "$ESPDIR/grubx64.efi" | cut -f1)) for kernel $KVER"
