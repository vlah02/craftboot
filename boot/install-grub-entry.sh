#!/bin/bash
# M4: add a NON-DEFAULT "Craftboot" GRUB entry that boots the signed Ubuntu
# kernel + our craftboot initramfs, so we can test it on the real machine.
#
# Safe & reversible:
#   - Your GRUB default (normal Ubuntu boot) is NOT changed.
#   - The entry is placed LAST so it can never become the default by accident.
#   - Secure Boot is fine: GRUB (already trusted) loads the Canonical-signed
#     kernel; the initramfs needs no signing.
#   - First run is DRY-RUN: it only renders the menu (selecting won't boot an OS),
#     then drops to a shell after the 15s auto-boot.
#
#   Install:    sudo ./boot/install-grub-entry.sh
#   Test once:  sudo grub-reboot craftboot && sudo reboot   (reverts next boot)
#   Uninstall:  sudo ./boot/install-grub-entry.sh --uninstall
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$HERE/build"
GRUBD="/etc/grub.d/45_craftboot"
INITRD="/boot/craftboot.initrd"

if [[ $EUID -ne 0 ]]; then echo "Please run with sudo."; exit 1; fi

if [[ "${1:-}" == "--uninstall" ]]; then
    rm -f "$GRUBD" "$INITRD"
    update-grub
    echo "Removed Craftboot entry and $INITRD. Back to normal."
    exit 0
fi

[[ -f "$B/craftboot.initrd" ]] || { echo "Build first: ./boot/build.sh"; exit 1; }

ROOT_UUID="$(findmnt -no UUID /)"
KVER="$(uname -r)"
echo "==> root UUID: $ROOT_UUID ; kernel: $KVER"

echo "==> copying initramfs -> $INITRD"
cp "$B/craftboot.initrd" "$INITRD"

echo "==> writing $GRUBD"
cat > "$GRUBD" <<EOF
#!/bin/sh
exec tail -n +3 \$0
menuentry 'Craftboot (Minecraft boot menu)' --id craftboot {
    load_video
    insmod gzio
    insmod part_gpt
    insmod ext2
    search --no-floppy --fs-uuid --set=root ${ROOT_UUID}
    linux  /boot/vmlinuz-${KVER} console=tty1
    initrd ${INITRD}
}
EOF
chmod +x "$GRUBD"

echo "==> update-grub"
update-grub

cat <<EOF

======================= INSTALLED (non-default) =======================
Your normal boot is unchanged. To test Craftboot ONCE on the next boot:

    sudo grub-reboot craftboot && sudo reboot

It will render the menu on the real screen. It's DRY-RUN, so:
  - leave it ~15s -> it "auto-boots" the default, plays the loading
    animation, prints the intended action, and drops to a shell.
  - a normal reboot returns you to Ubuntu as usual.

If the internal keyboard doesn't navigate, that's expected for now (USB-HID
modules may be needed) - the render test still works via the auto-boot.

Uninstall anytime:  sudo ./boot/install-grub-entry.sh --uninstall
=======================================================================
EOF
