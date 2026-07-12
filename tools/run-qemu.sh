#!/bin/bash
# Boot the craftboot initramfs in QEMU under UEFI (OVMF), in a graphical window
# so /dev/fb0 renders. Kernel/app logs + QEMU monitor go to this terminal.
# Quit: close the window, or Ctrl-A X isn't available here — use the monitor
# (Ctrl-A C toggles) or just close the QEMU window.
set -euo pipefail
# Snap-launched shells leak /snap/.../lib into LD_LIBRARY_PATH, which breaks the
# system qemu (glibc mismatch). Clear it so qemu uses the system libraries.
unset LD_LIBRARY_PATH LD_PRELOAD
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HERE/../build"

[[ -f "$B/vmlinuz" ]] || { echo "No kernel staged. Run: sudo cp /boot/vmlinuz-$(uname -r) '$B/vmlinuz' && sudo chmod +r '$B/vmlinuz'"; exit 1; }
[[ -f "$B/craftboot.initrd" ]] || { echo "No initramfs (run dist/ubuntu/build.sh)"; exit 1; }

# test root disk with the config's UUID so the ext4 probe finds it;
# regenerate if the existing image's UUID no longer matches the config
UUID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["root_uuid"])' "$HERE/../boot_entries.json")"
CUR=""
[[ -f "$B/testroot.img" ]] && CUR=$(dumpe2fs -h "$B/testroot.img" 2>/dev/null | awk -F': *' '/Filesystem UUID/{print $2}')
if [[ "$CUR" != "$UUID" ]]; then
    dd if=/dev/zero of="$B/testroot.img" bs=1M count=64 status=none
    mkfs.ext4 -q -F -U "$UUID" "$B/testroot.img"
fi

cp -f /usr/share/OVMF/OVMF_VARS_4M.fd "$B/OVMF_VARS.fd"

KVM=()
[[ -w /dev/kvm ]] && KVM=(-enable-kvm -cpu host)

# Launch qemu in a CLEAN environment so nothing (snap LD_LIBRARY_PATH, GTK paths)
# leaks in and breaks its library loading. Keep only what the GUI needs.
exec env -i \
    PATH=/usr/bin:/bin:/usr/sbin:/sbin \
    HOME="${HOME:-/root}" \
    DISPLAY="${DISPLAY:-}" \
    WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" \
    XAUTHORITY="${XAUTHORITY:-}" \
    XDG_SESSION_TYPE="${XDG_SESSION_TYPE:-}" \
    /usr/bin/qemu-system-x86_64 "${KVM[@]}" -machine q35 -m 2048 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,unit=1,file="$B/OVMF_VARS.fd" \
    -drive file="$B/testroot.img",format=raw,if=virtio \
    -kernel "$B/vmlinuz" -initrd "$B/craftboot.initrd" \
    -append "console=ttyS0" \
    -vga std -serial mon:stdio
