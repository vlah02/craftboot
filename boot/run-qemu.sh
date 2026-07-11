#!/bin/bash
# Boot the craftboot initramfs in QEMU under UEFI (OVMF), serial console.
# Quit qemu with: Ctrl-A then X.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HERE/build"

[[ -f "$B/vmlinuz" ]] || { echo "No kernel staged. Run: sudo cp /boot/vmlinuz-$(uname -r) '$B/vmlinuz' && sudo chmod +r '$B/vmlinuz'"; exit 1; }
[[ -f "$B/craftboot.initrd" ]] || { echo "No initramfs. Run: ./boot/build.sh"; exit 1; }

# writable copy of UEFI NVRAM vars
cp -f /usr/share/OVMF/OVMF_VARS_4M.fd "$B/OVMF_VARS.fd"

KVM=()
[[ -w /dev/kvm ]] && KVM=(-enable-kvm -cpu host)

exec qemu-system-x86_64 "${KVM[@]}" -machine q35 -m 2048 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,unit=1,file="$B/OVMF_VARS.fd" \
    -kernel "$B/vmlinuz" -initrd "$B/craftboot.initrd" \
    -append "console=ttyS0" \
    -nographic
