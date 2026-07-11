#!/bin/bash
# Build the craftboot initramfs (M3): busybox + python3 + pygame + numpy + SDL2
# + the app, launched in --fb mode (framebuffer output + evdev input).
# evdev and simpledrm (/dev/fb0) are built into the kernel, so no modules needed.
# Builds as your normal user; only staging the root-only kernel needs sudo.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$HERE/../app"
B="$HERE/build"
ROOT="$B/root"
KREL="${1:-$(uname -r)}"              # target kernel version (arg 1); default = running
ROOT_UUID="$(findmnt -no UUID /)"     # baked into init so it can mount the real root
PYBIN="$(readlink -f "$(command -v python3)")"
STDLIB="$(python3 -c 'import sys;print(sys.base_prefix+"/lib/python%d.%d"%sys.version_info[:2])')"
PYGAME_DIR="$(PYGAME_HIDE_SUPPORT_PROMPT=1 python3 -c 'import pygame,os;print(os.path.dirname(pygame.__file__))' 2>/dev/null || true)"
NUMPY_DIR="$(python3 -c 'import numpy,os;print(os.path.dirname(numpy.__file__))')"

mkdir -p "$B"
rm -rf "$ROOT" "$B/craftboot.initrd"     # keep a staged kernel across rebuilds
mkdir -p "$ROOT"/{bin,sbin,proc,sys,dev,tmp,run,etc,usr/bin,usr/sbin,lib,lib64,usr/lib,craftboot}

libs_of() {
    ldd "$1" 2>/dev/null | grep -oE '/[^ ]+\.so[^ ]*' | while read -r lib; do
        [[ -f "$lib" ]] && cp -L --parents "$lib" "$ROOT" 2>/dev/null || true
    done || true
}
copy_bin() { [[ -e "$1" ]] && cp -L --parents "$1" "$ROOT" 2>/dev/null || true; libs_of "$1"; }
copy_tree() {   # copy a dir verbatim, then resolve libs of every .so inside it
    cp -a --parents "$1" "$ROOT" 2>/dev/null || true
    find "$1" -name '*.so*' -print0 2>/dev/null | while IFS= read -r -d '' so; do libs_of "$so"; done || true
}

echo "==> busybox"
copy_bin /usr/bin/busybox
for a in busybox sh mount umount ls cat echo mkdir sleep switch_root poweroff \
         reboot dmesg uname ln cp env findfs sync; do
    ln -sf /usr/bin/busybox "$ROOT/bin/$a"
done

echo "==> python + stdlib ($PYBIN)"
copy_bin "$PYBIN"
ln -sf "$PYBIN" "$ROOT/bin/python3"
copy_tree "$STDLIB"

echo "==> pygame + numpy"
[[ -n "$PYGAME_DIR" ]] && copy_tree "$PYGAME_DIR"
copy_tree "$NUMPY_DIR"

echo "==> SDL2"
sdl="$(ldconfig -p | awk '$1=="libSDL2-2.0.so.0"{print $NF; exit}')"
[[ -n "$sdl" ]] && { cp -L --parents "$sdl" "$ROOT" 2>/dev/null || true; libs_of "$sdl"; }

echo "==> keyboard modules (USB HID) + kmod"
copy_bin /usr/bin/kmod
for t in modprobe insmod rmmod depmod lsmod; do ln -sf /usr/bin/kmod "$ROOT/sbin/$t"; done
MODDIR="/lib/modules/$KREL"
mkdir -p "$ROOT$MODDIR"
for f in modules.dep modules.dep.bin modules.alias modules.alias.bin modules.symbols \
         modules.symbols.bin modules.builtin modules.builtin.bin modules.builtin.modinfo \
         modules.order; do
    [[ -f "$MODDIR/$f" ]] && cp "$MODDIR/$f" "$ROOT$MODDIR/" 2>/dev/null || true
done
for m in usbhid hid_generic hid_asus i2c_hid_acpi nvme; do
    modprobe -S "$KREL" --show-depends "$m" 2>/dev/null | awk '/^insmod/{print $2}' | while read -r ko; do
        [[ -f "$ko" ]] && cp -L --parents "$ko" "$ROOT" 2>/dev/null || true
    done || true
done

echo "==> handoff tools (kexec, efibootmgr)"
copy_bin /usr/sbin/kexec
copy_bin /usr/bin/efibootmgr

echo "==> app"
cp -a "$APP/." "$ROOT/craftboot/"
# drop assets not needed at boot (photos-mode screenshots + panorama source faces)
rm -rf "$ROOT/craftboot/assets/backgrounds" "$ROOT/craftboot/assets/panorama_faces"
find "$ROOT/craftboot" -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null || true

echo "==> /init"
cat > "$ROOT/init" <<'EOF'
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PYTHONPATH=/usr/lib/python3/dist-packages
export PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 HOME=/root
mkdir -p /root /mnt
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null
[ -e /dev/kmsg ] && exec >/dev/kmsg 2>&1   # clean screen: logs -> dmesg, not the display
# modules: USB HID keyboard + nvme disk (xhci/ext4/efivarfs are built into the kernel)
for m in usbhid hid_generic hid_asus i2c_hid_acpi nvme; do modprobe "$m" 2>/dev/null; done
mount -t efivarfs efivarfs /sys/firmware/efi/efivars 2>/dev/null
sleep 3                       # let USB + nvme enumerate
# mount the real Ubuntu root read-only so the app can kexec its kernel
ROOTDEV=$(findfs UUID=__ROOT_UUID__ 2>/dev/null)
if [ -n "$ROOTDEV" ] && mount -o ro "$ROOTDEV" /mnt 2>/dev/null; then
    export CRAFTBOOT_ROOT=/mnt
fi
echo "[craftboot] starting menu (fb+live); root=$ROOTDEV"
python3 /craftboot/main.py --fb --live
rc=$?
[ "$rc" = 0 ] || { dmesg | tail -25 >/dev/tty1; echo "[craftboot] exited rc=$rc" >/dev/tty1; sleep 15; }
sync; sleep 3; reboot -f
EOF
sed -i "s/__ROOT_UUID__/$ROOT_UUID/" "$ROOT/init"
chmod +x "$ROOT/init"

echo "==> pack initramfs"
( cd "$ROOT" && find . | cpio -o -H newc 2>/dev/null | gzip -1 ) > "$B/craftboot.initrd"
echo "    $B/craftboot.initrd ($(du -h "$B/craftboot.initrd" | cut -f1))"

echo "==> stage kernel"
if [[ -f "$B/vmlinuz" ]]; then
    echo "    kernel already staged: $B/vmlinuz"
elif cp "/boot/vmlinuz-$KREL" "$B/vmlinuz" 2>/dev/null; then
    chmod +r "$B/vmlinuz"; echo "    $B/vmlinuz"
else
    echo "    [!] Run once: sudo cp /boot/vmlinuz-$KREL '$B/vmlinuz' && sudo chmod +r '$B/vmlinuz'"
fi
echo "DONE. Now: ./boot/run-qemu.sh"
