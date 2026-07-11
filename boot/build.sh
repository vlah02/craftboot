#!/bin/bash
# Build a craftboot initramfs for QEMU testing (M3, step 1: boot to a shell).
# The initramfs is built as your normal user. Only staging the (root-only)
# kernel needs sudo; if it can't be copied, this prints the one command to run.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HERE/build"
ROOT="$B/root"
KREL="$(uname -r)"

mkdir -p "$B"
rm -rf "$ROOT" "$B/craftboot.initrd"     # keep a staged kernel across rebuilds
mkdir -p "$ROOT"/{bin,sbin,proc,sys,dev,tmp,run,etc,usr/bin,usr/sbin,lib,lib64,usr/lib}

copy_with_libs() {
    local bin="$1"
    [[ -e "$bin" ]] || { echo "   (missing: $bin)"; return; }
    cp -L --parents "$bin" "$ROOT" 2>/dev/null || true
    ldd "$bin" 2>/dev/null | grep -oE '/[^ ]+\.so[^ ]*' | while read -r lib; do
        [[ -f "$lib" ]] && cp -L --parents "$lib" "$ROOT" 2>/dev/null || true
    done || true
}

echo "==> busybox + core applets"
copy_with_libs /usr/bin/busybox
for a in busybox sh mount umount ls cat echo mkdir sleep insmod modprobe mknod \
         switch_root poweroff reboot dmesg uname ln cp; do
    ln -sf /usr/bin/busybox "$ROOT/bin/$a"
done

echo "==> /init"
cat > "$ROOT/init" <<'EOF'
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null
echo
echo "=================================================="
echo "  craftboot initramfs — hello from the VM!"
echo "  kernel: $(uname -r)"
echo "  (type 'poweroff -f' or Ctrl-A X to quit qemu)"
echo "=================================================="
echo
exec sh
EOF
chmod +x "$ROOT/init"

echo "==> pack initramfs"
( cd "$ROOT" && find . | cpio -o -H newc 2>/dev/null | gzip -9 ) > "$B/craftboot.initrd"
echo "    $B/craftboot.initrd ($(du -h "$B/craftboot.initrd" | cut -f1))"

echo "==> stage kernel"
if [[ -f "$B/vmlinuz" ]]; then
    echo "    kernel already staged: $B/vmlinuz"
elif cp "/boot/vmlinuz-$KREL" "$B/vmlinuz" 2>/dev/null; then
    chmod +r "$B/vmlinuz"
    echo "    $B/vmlinuz"
else
    echo "    [!] Could not read /boot/vmlinuz-$KREL (root-only). Run once:"
    echo "        sudo cp /boot/vmlinuz-$KREL '$B/vmlinuz' && sudo chmod +r '$B/vmlinuz'"
fi

echo "DONE. Now: ./boot/run-qemu.sh"
