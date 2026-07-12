#!/bin/bash
# Build the craftboot C initramfs: static binary as /init + assets + kernel modules.
#   ./dist/ubuntu/build.sh [kernel-version]
# No sudo needed on a machine with world-readable /lib/modules/*.ko.zst and a
# staged kernel (see below). If your module files or /boot/vmlinuz-* are
# root-only, re-run the kernel-staging step with sudo as printed at the end.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
B="$REPO/build"
ROOT="$B/initroot"
KREL="${1:-$(uname -r)}"
DEBUG="${DEBUG:-0}"

mkdir -p "$B"
if [[ ! -f "$B/vmlinuz" && -f "$REPO/boot/build/vmlinuz" ]]; then
    cp "$REPO/boot/build/vmlinuz" "$B/vmlinuz"
fi

make -C "$REPO"

rm -rf "$ROOT"
mkdir -p "$ROOT"/{dev,proc,sys,mnt,assets}

echo "==> app + assets + config"
install -m755 "$B/craftboot" "$ROOT/init"
cp -a "$REPO/assets/." "$ROOT/assets/"
cp "$REPO/boot_entries.json" "$ROOT/boot_entries.json"

echo "==> kernel modules (resolved + decompressed at build time)"
mkdir -p "$ROOT/modules"
: > "$ROOT/modules.list"
for m in usbhid hid_generic hid_asus i2c_hid_acpi nvme virtio_blk; do
    modprobe -S "$KREL" --show-depends "$m" 2>/dev/null | awk '/^insmod/{print $2}'
done | awk '!seen[$0]++' | while read -r ko; do
    base="$(basename "$ko")"
    out="$ROOT/modules/${base%.zst}"; out="${out%.xz}"
    case "$ko" in
        *.zst) zstd -qd "$ko" -o "$out" ;;
        *.xz)  xz -dc "$ko" > "$out" ;;
        *)     cp "$ko" "$out" ;;
    esac
    echo "/modules/$(basename "$out")" >> "$ROOT/modules.list"
done
echo "    $(wc -l < "$ROOT/modules.list") modules"

if [[ "$DEBUG" == 1 ]]; then
    echo "==> DEBUG: busybox shell"
    mkdir -p "$ROOT/bin"
    cp /usr/bin/busybox "$ROOT/bin/busybox"
    ln -sf busybox "$ROOT/bin/sh"
fi

echo "==> pack (zstd)"
( cd "$ROOT" && find . | cpio -o -H newc 2>/dev/null | zstd -19 -T0 -q ) > "$B/craftboot.initrd"
echo "    $B/craftboot.initrd ($(du -h "$B/craftboot.initrd" | cut -f1))"

if [[ ! -f "$B/vmlinuz" ]]; then
    cp "/boot/vmlinuz-$KREL" "$B/vmlinuz" && chmod +r "$B/vmlinuz" \
        || echo "    [!] stage kernel: sudo cp /boot/vmlinuz-$KREL $B/vmlinuz && sudo chmod +r $B/vmlinuz"
fi
echo "DONE. Test: ./tools/run-qemu.sh"
